//Platform.h for the PlayStation, built with PSn00bSDK (see psx/CMakeLists.txt):
//  display  320x240, the game's 128x128 screen buffer goes into video memory as a 16 bit texture
//           and the GPU draws it scaled to 240x240 in the middle
//  buttons  d-pad, Cross, Circle, L1 and R1
//  sound    a square wave on one of the SPU's voices, built as a single looping ADPCM block
//  saves    nothing yet, the game starts fresh every time
//  time     the vertical blanks the GPU counts, 59.94 of them a second
//
//main() here starts the game and runs Game_Loop over and over, the game holds its frames to its own
//frame rate itself.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_PSX

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psxgpu.h>
#include <psxetc.h>
#include <psxapi.h>
#include <psxpad.h>
#include <psxspu.h>
#include <hwregs_c.h>
#include "PlatformGamebuinoFont.h"

#define SCREEN_PSX_WIDTH 320
#define SCREEN_PSX_HEIGHT 240
//where the game's frame lives in video memory, next to the two screens the GPU draws in
#define TEXTURE_X 512
#define TEXTURE_Y 0
//the ordering table only ever holds the one quad the frame is drawn with
#define OT_LENGTH 8

static PlatformPSXDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and handed to the GPU when the frame is done
PlatformBuffer screenBuffer;
#endif

#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as a 16 bit pixel
static uint16_t bufferPalette[256];
#endif

//the two screens the GPU works with: one is shown while the next frame is drawn in the other
static DISPENV dispEnv[2];
static DRAWENV drawEnv[2];
static uint32_t orderingTable[2][OT_LENGTH];
static uint8_t primBuffer[2][256];
static uint8_t screenPage = 0;
//the frame as the GPU wants it: 16 bit pixels, blue in the top bits
static uint16_t frameTexture[WINDOW_WIDTH * WINDOW_HEIGHT];
//where the frame sits on screen and how big it is
static int16_t frameX = 0, frameY = 0, frameSize = WINDOW_WIDTH;

//the microsecond a tone ends at, 0 while none has to end
static uint32_t toneEnd = 0;

//RGB565 to the GPU's 16 bit pixel, blue in the top bits. Bit 15 is the mask bit, which stays clear
static inline uint16_t ToBGR555(uint16_t color)
{
	return (uint16_t)(((color >> 11) & 0x1F) | (((color >> 6) & 0x1F) << 5) | ((color & 0x1F) << 10));
}

static void ToneOff(void);

// ===========================================================================
// Program start
// ===========================================================================

int main(void)
{
	Game_Setup();
	while (1)
	{
		//a tone that has played long enough stops here, the ones with no length of their own play
		//until the next tone
		if (toneEnd && ((int32_t)(Platform_Micros() - toneEnd) >= 0))
			ToneOff();
		Game_Loop();
	}
	return 0;
}

// ===========================================================================
// Display
// ===========================================================================

//clips x, y, w, h to the game's screen, false when nothing of it is left
static bool ClipRect(int32_t& x, int32_t& y, int32_t& w, int32_t& h)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > WINDOW_WIDTH) w = WINDOW_WIDTH - x;
	if (y + h > WINDOW_HEIGHT) h = WINDOW_HEIGHT - y;
	return (w > 0) && (h > 0);
}

#if SCREENBUFFER
//everything is drawn into the screen buffer, these are here for the paths that ask for them
void PlatformPSXDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformPSXDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformPSXDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformPSXDisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the frame the GPU is handed, which is kept in
//the GPU's own colours: a pixel is converted once, where it is drawn, instead of the whole frame
//being converted again every time it is sent. The area the pixels go into and where the next one
//goes, in game coordinates
static int32_t windowLeft = 0, windowRight = -1, cursorX = 0, cursorY = 0;

void PlatformPSXDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	windowLeft = x;
	windowRight = x + w - 1;
	cursorX = x;
	cursorY = y;
}

//the next pixel of the window, what falls outside the game's screen is left out
static inline void WritePixel(uint16_t bgr)
{
	if ((cursorX >= 0) && (cursorX < WINDOW_WIDTH) && (cursorY >= 0) && (cursorY < WINDOW_HEIGHT))
		frameTexture[cursorY * WINDOW_WIDTH + cursorX] = bgr;
	if (++cursorX > windowRight)
	{
		cursorX = windowLeft;
		cursorY++;
	}
}

void PlatformPSXDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8));
		WritePixel(ToBGR555(value));
	}
}

void PlatformPSXDisplay::writeColor(uint16_t color, uint32_t length)
{
	const uint16_t bgr = ToBGR555(color);
	while (length--)
		WritePixel(bgr);
}

void PlatformPSXDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	const uint16_t bgr = ToBGR555(color);
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &frameTexture[row * WINDOW_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = bgr;
	}
}
#endif

void PlatformPSXGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if ((w <= 0) || (h <= 0))
		return;
	fillRect(x, y, w, 1, color);
	fillRect(x, y + h - 1, w, 1, color);
	if (h > 2)
	{
		fillRect(x, y + 1, 1, h - 2, color);
		fillRect(x + w - 1, y + 1, 1, h - 2, color);
	}
}

//Draws the character the way LovyanGFX draws its default font: a 6x8 cell times the text size, the
//5 columns of the glyph and a column of background, the background only when it differs from the
//text colour. Every column goes out as runs of one colour
size_t PlatformPSXGFX::drawChar(uint16_t c, int32_t x, int32_t y)
{
	const int32_t size = textSize;
	//the 'classic' character set LovyanGFX uses unless told otherwise
	if (c >= 176)
		c++;
	if (c > 255)
		return 6 * size;
	const bool fillBackground = (textBackground != textColor);
	const uint8_t* glyph = &platformFont[c * 5];
	for (int32_t column = 0; column < 5; column++)
	{
		const uint8_t bits = glyph[column];
		int32_t row = 0;
		while (row < 8)
		{
			const bool set = ((bits >> row) & 1) != 0;
			int32_t end = row + 1;
			while ((end < 8) && ((((bits >> end) & 1) != 0) == set))
				end++;
			if (set || fillBackground)
				fillRect(x + column * size, y + row * size, size, (end - row) * size, set ? textColor : textBackground);
			row = end;
		}
	}
	if (fillBackground)
		fillRect(x + 5 * size, y, size, 8 * size, textBackground);
	return 6 * size;
}

#if SCREENBUFFER
//the screen buffer, read and written for every pixel the game draws and again when the frame is sent
bool PlatformPSXBuffer::createSprite(int32_t w, int32_t h)
{
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	if (pixels)
		free(pixels);
	pixels = (uint8_t*)malloc(bytes);
	if (!pixels)
		return false;
	memset(pixels, 0, bytes);
	return true;
}

void PlatformPSXBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!pixels || !ClipRect(x, y, w, h))
		return;
	(void)depth;
#if SCREENBUFFER == 16
	//a 16 bpp sprite keeps its pixels byte swapped
	const uint16_t value = (uint16_t)((color >> 8) | (color << 8));
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &((uint16_t*)(void*)pixels)[row * WINDOW_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = value;
	}
#elif SCREENBUFFER == 8
	const uint8_t value = (uint8_t)(((color & 0xE000) >> 8) | ((color & 0x0700) >> 6) | ((color & 0x0018) >> 3));
	for (int32_t row = y; row < y + h; row++)
		memset(&pixels[row * WINDOW_WIDTH + x], value, w);
#else
	for (int32_t row = y; row < y + h; row++)
		for (int32_t column = x; column < x + w; column++)
			SetBufferBit(pixels, column, row, color);
#endif
}

#endif

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	bufferSetColor = setColor;
	bufferClearColor = clearColor;
#else
	(void)setColor;
	(void)clearColor;
#endif
}

#if SCREENBUFFER
//bytes of one row of the screen buffer
#if SCREENBUFFER == 16
#define BUFFER_ROW_BYTES (WINDOW_WIDTH * 2)
#elif SCREENBUFFER == 8
#define BUFFER_ROW_BYTES WINDOW_WIDTH
#else
#define BUFFER_ROW_BYTES ((WINDOW_WIDTH + 7) / 8)
#endif

//the 16 bit pixel of game pixel x of a buffer row
static inline uint16_t BufferPixel(const uint8_t* row, uint16_t x)
{
#if SCREENBUFFER == 16
	//byte swapped in the buffer
	return ToBGR555((uint16_t)((row[x * 2] << 8) | row[x * 2 + 1]));
#elif SCREENBUFFER == 8
	return bufferPalette[row[x]];
#else
	//most significant bit first as SetBufferBit writes
	return (row[x >> 3] & (0x80 >> (x & 7))) ? bufferSetColor : bufferClearColor;
#endif
}
#endif

//Sends the frame the game drew to the GPU: the pixels go into video memory as a texture and one
//quad is drawn with it, scaled to where the frame belongs on screen
void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	//the buffer holds the frame in the colours a sprite keeps, which the GPU does not take
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	for (uint16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * BUFFER_ROW_BYTES];
		uint16_t* d = &frameTexture[y * WINDOW_WIDTH];
		for (uint16_t x = 0; x < WINDOW_WIDTH; x++)
			d[x] = BufferPixel(row, x);
	}
#endif

	//The frame that was drawn last is on screen by now. The game paces itself, so this does not
	//wait for the blank as well: waiting here pushed a frame that took a little over two blanks
	//onto three, which is 20 frames a second instead of 30
	DrawSync(0);
	screenPage ^= 1;
	PutDispEnv(&dispEnv[screenPage]);
	PutDrawEnv(&drawEnv[screenPage]);
	SetDispMask(1);

	//the pixels into video memory, where the quad below takes them from
	RECT area;
	area.x = TEXTURE_X;
	area.y = TEXTURE_Y;
	area.w = WINDOW_WIDTH;
	area.h = WINDOW_HEIGHT;
	//queued rather than waited for: the GPU takes the pixels while the game works on
	LoadImage(&area, (const uint32_t*)frameTexture);
	DrawSync(0);

	ClearOTagR(orderingTable[screenPage], OT_LENGTH);
	POLY_FT4* quad = (POLY_FT4*)primBuffer[screenPage];
	setPolyFT4(quad);
	//no shading of its own, the texture is shown as it is
	setRGB0(quad, 128, 128, 128);
	setXY4(quad,
	       frameX, frameY,
	       (int16_t)(frameX + frameSize), frameY,
	       frameX, (int16_t)(frameY + frameSize),
	       (int16_t)(frameX + frameSize), (int16_t)(frameY + frameSize));
	setUV4(quad, 0, 0, WINDOW_WIDTH - 1, 0, 0, WINDOW_HEIGHT - 1, WINDOW_WIDTH - 1, WINDOW_HEIGHT - 1);
	//16 bit texture, at the page the pixels were put in
	quad->tpage = getTPage(2, 0, TEXTURE_X, TEXTURE_Y);
	addPrim(orderingTable[screenPage], quad);
	DrawOTag(orderingTable[screenPage] + OT_LENGTH - 1);
}

static void DisplayInit(void)
{
	ResetGraph(0);
	//two screens above each other in video memory, drawn in one while the other is shown
	SetDefDispEnv(&dispEnv[0], 0, 0, SCREEN_PSX_WIDTH, SCREEN_PSX_HEIGHT);
	SetDefDrawEnv(&drawEnv[0], 0, SCREEN_PSX_HEIGHT, SCREEN_PSX_WIDTH, SCREEN_PSX_HEIGHT);
	SetDefDispEnv(&dispEnv[1], 0, SCREEN_PSX_HEIGHT, SCREEN_PSX_WIDTH, SCREEN_PSX_HEIGHT);
	SetDefDrawEnv(&drawEnv[1], 0, 0, SCREEN_PSX_WIDTH, SCREEN_PSX_HEIGHT);
	for (int i = 0; i < 2; i++)
	{
		//black around the game's frame, painted before every frame
		setRGB0(&drawEnv[i], 0, 0, 0);
		drawEnv[i].isbg = 1;
		drawEnv[i].dtd = 0;
	}
	screenPage = 0;
	PutDispEnv(&dispEnv[0]);
	PutDrawEnv(&drawEnv[0]);
	SetDispMask(1);

#if SCALESCREEN
	frameSize = SCREEN_PSX_HEIGHT;
#else
	frameSize = WINDOW_WIDTH;
#endif
	frameX = (int16_t)((SCREEN_PSX_WIDTH - frameSize) / 2);
	frameY = (int16_t)((SCREEN_PSX_HEIGHT - frameSize) / 2);
}

// ===========================================================================
// Buttons
// ===========================================================================

//what the controllers report, the pads write into this by themselves
static uint8_t padBuffer[2][34];

static void ButtonsInit(void)
{
	InitPAD(padBuffer[0], 34, padBuffer[1], 34);
	StartPAD();
	//the pads keep being read without the program asking for it
	ChangeClearPAD(0);
}

uint8_t Platform_GetButtons(void)
{
	const PADTYPE* pad = (const PADTYPE*)padBuffer[0];
	uint8_t buttons = 0;
	//a bit is 0 while its button is held
	if (pad->stat != 0)
		return 0;
	const uint16_t held = (uint16_t)~pad->btn;
	if (held & PAD_LEFT)
		buttons |= BUTTON_LEFT;
	if (held & PAD_UP)
		buttons |= BUTTON_UP;
	if (held & PAD_DOWN)
		buttons |= BUTTON_DOWN;
	if (held & PAD_RIGHT)
		buttons |= BUTTON_RIGHT;
	if (held & PAD_CROSS)
		buttons |= BUTTON_A;
	if (held & PAD_CIRCLE)
		buttons |= BUTTON_B;
	if (held & PAD_L1)
		buttons |= BUTTON_L;
	if (held & PAD_R1)
		buttons |= BUTTON_R;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

//a vertical blank is 1/59.94 of a second
#define MICROS_PER_VBLANK 16683u

uint32_t Platform_Micros(void)
{
	//VSync(-1) is the number of blanks since the display was set up
	return (uint32_t)VSync(-1) * MICROS_PER_VBLANK;
}

//One cycle of a square wave as a single ADPCM block, which is what the SPU plays. The block holds
//28 samples: 14 of them high, 14 low, and its flags make the SPU play it over and over
#define TONE_VOICE 0
#define TONE_SAMPLES 28
#define TONE_SPU_ADDR 0x1010
//the SPU plays 44100 samples a second at a pitch of 4096
#define TONE_SPU_RATE 44100
static bool soundReady = false;
static void ToneOff(void)
{
	if (soundReady)
	{
		//the voice is released and silenced, the next tone starts it again
		SPU_KEY_OFF1 = 1 << TONE_VOICE;
		SPU_CH_VOL_L(TONE_VOICE) = 0;
		SPU_CH_VOL_R(TONE_VOICE) = 0;
	}
	toneEnd = 0;
}

static void SoundInit(void)
{
	SpuInit();
	//the block: shift 0 and filter 0, so a nibble is the sample itself, and loop start, repeat
	//and end together, which makes the SPU play this one block for as long as the voice is on
	uint32_t block[4];
	uint8_t* bytes = (uint8_t*)block;
	memset(block, 0, sizeof(block));
	bytes[0] = 0x00;
	bytes[1] = 0x07;
	for (int i = 0; i < TONE_SAMPLES / 2; i++)
	{
		//two samples to a byte, the low nibble first. 7 is the highest, 8 the lowest
		const int index = i / 2;
		const bool high = (i < TONE_SAMPLES / 4);
		const uint8_t value = high ? 0x7 : 0x8;
		bytes[2 + index] |= (uint8_t)(value << ((i & 1) ? 4 : 0));
	}
	SpuSetTransferStartAddr(TONE_SPU_ADDR);
	SpuWrite(block, sizeof(block));
	SpuIsTransferCompleted(1);

	//the sound the SPU mixes, at the volume the game asks for
	SPU_MASTER_VOL_L = (int16_t)(0x3FFF * SOUNDVOLUME / 100);
	SPU_MASTER_VOL_R = (int16_t)(0x3FFF * SOUNDVOLUME / 100);
	SPU_CH_ADDR(TONE_VOICE) = TONE_SPU_ADDR >> 3;
	//straight on and straight off, no envelope of its own
	SPU_CH_ADSR1(TONE_VOICE) = 0x00FF;
	SPU_CH_ADSR2(TONE_VOICE) = 0x0000;
	soundReady = true;
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	if (!soundReady)
		return;
	ToneOff();
	//a frequency of 0 is a rest
	if (freq == 0)
		return;
	//the pitch that makes the 28 sample cycle come out at freq Hz
	uint32_t pitch = ((uint32_t)freq * TONE_SAMPLES * 4096u + TONE_SPU_RATE / 2) / TONE_SPU_RATE;
	if (pitch > 0x3FFF)
		pitch = 0x3FFF;
	if (pitch == 0)
		pitch = 1;
	const int16_t volume = (int16_t)(0x3FFF * SOUNDVOLUME / 100);
	SPU_CH_FREQ(TONE_VOICE) = (uint16_t)pitch;
	SPU_CH_ADDR(TONE_VOICE) = TONE_SPU_ADDR >> 3;
	SPU_CH_VOL_L(TONE_VOICE) = volume;
	SPU_CH_VOL_R(TONE_VOICE) = volume;
	SPU_KEY_ON1 = 1 << TONE_VOICE;

	//0 keeps playing until the next tone, the others end in Platform_Micros' own time
	if (duration)
	{
		toneEnd = Platform_Micros() + (uint32_t)duration * 1000u;
		//a tone that ends exactly at 0 microseconds would be taken for one that plays on
		if (toneEnd == 0)
			toneEnd = 1;
	}
	else
		toneEnd = 0;
}

//the program ends where the heap starts, and the stack comes down from the top of memory
uint32_t Platform_FreeHeap(void)
{
	//the console has 2 MB, the last 64 KB of it are left for the stack to grow into
	const uintptr_t heapTop = 0x80200000u - 0x10000u;
	const uintptr_t brk = (uintptr_t)sbrk(0);
	return (brk < heapTop) ? (uint32_t)(heapTop - brk) : 0;
}

uint32_t Platform_FreeStack(void)
{
	//what is between the stack as it stands now and the end of the heap below it
	const uintptr_t brk = (uintptr_t)sbrk(0);
	const uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
	return (frame > brk) ? (uint32_t)(frame - brk) : 0;
}

uint32_t Platform_RandomSeed(void)
{
	//how long the console took to reach the game, which is never quite the same twice
	return (uint32_t)VSync(-1) * 2654435761u;
}

void Platform_Log(const char* format, ...)
{
	//goes to the debugger of an emulator, the screen is the game's
	char message[128];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	printf("%s", message);
}

// ===========================================================================
// Saved data
//
// The memory card is not written yet, so the block only lives for as long as the game runs.
// ===========================================================================

static uint8_t storage[PLATFORM_STORAGE_SIZE];

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	memcpy(storage + offset, data, length);
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	(void)appName;
	memset(storage, 0xFF, sizeof(storage));
	DisplayInit();
	ButtonsInit();
	SoundInit();
#if SCREENBUFFER
	screenBuffer.setColorDepth(SCREENBUFFER);
	screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT);
#endif
#if SCREENBUFFER == 8
	//RGB332 to RGB565 the way LovyanGFX converts it, so the colours match the other devices
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
		bufferPalette[i] = ToBGR555((uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3)));
	}
#endif
}

#endif
