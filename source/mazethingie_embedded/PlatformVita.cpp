//Platform.h for the PlayStation Vita, built with VitaSDK (see vita/CMakeLists.txt):
//  display  960x544, 32 bit pixels in video memory, two buffers so a frame is never seen half drawn.
//           The game's 128x128 frame is blown up four times to 512x512 in the middle of it
//  buttons  d-pad (the left stick works as one too), cross, circle, L and R
//  sound    a square wave written into a background music port by a thread of its own
//  saves    ux0:data/<game>/<game>.sav
//  time     the system clock, which counts microseconds
//
//main() here starts the game and runs Game_Loop until the Vita closes the program, the game holds its
//frames to its own frame rate itself.
//
//The Vita's only display format is 32 bit with red in the low byte, so the RGB565 the game works in
//is widened on its way to the display.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_VITA

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include "PlatformGamebuinoFont.h"

//the name of the folder the save file goes in. The build passes it
#ifndef VITA_APP_NAME
#define VITA_APP_NAME "Game"
#endif

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
//a row of the display is 960 pixels apart, and every pixel is four bytes
#define VRAM_STRIDE 960
#define VRAM_BUFFER_BYTES (VRAM_STRIDE * SCREEN_HEIGHT * 4)
//video memory is handed out in whole blocks of 256 KB
#define VRAM_BLOCK_BYTES ((VRAM_BUFFER_BYTES + 0x3FFFF) & ~0x3FFFF)

//How many display pixels a game pixel is wide and high, and where the frame sits on the display.
//The game has a SCALE of its own in defines.h, so this one carries the platform's name
#if SCALESCREEN
#define VITA_SCALE 4
#else
#define VITA_SCALE 1
#endif
#define OUT_WIDTH (WINDOW_WIDTH * VITA_SCALE)
#define OUT_HEIGHT (WINDOW_HEIGHT * VITA_SCALE)
#define OFFSET_X ((SCREEN_WIDTH - OUT_WIDTH) / 2)
#define OFFSET_Y ((SCREEN_HEIGHT - OUT_HEIGHT) / 2)

#define AUDIO_SAMPLE_RATE 48000
//samples the sound port takes at a time, it has to be a multiple of 64
#define AUDIO_GRAIN 1024

static PlatformVitaDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the display when it is done
PlatformBuffer screenBuffer;
#else
//without a buffer the game draws into this frame, which is sent to the display the same way
static uint16_t frame[WINDOW_WIDTH * WINDOW_HEIGHT];
#endif
#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in, in the display's own pixel format
static uint32_t bufferSetColor = 0xFFFFFFFF, bufferClearColor = 0xFF000000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as a pixel of the display's format
static uint32_t bufferPalette[256];
#endif

//the two buffers in video memory, one on the display while the other is drawn into
static uint32_t* vramBuffer[2] = { nullptr, nullptr };
static uint8_t drawBuffer = 0;
//where the save file goes, built in Platform_Init
static char storagePath[128] = "";

//RGB565 to the display's pixel: red in the low byte, and the channels widened to 8 bits each the way
//a 5 or 6 bit value fills 8 bits, so white stays white
static inline uint32_t ToDisplay(uint16_t color)
{
	const uint32_t r5 = (color >> 11) & 0x1F, g6 = (color >> 5) & 0x3F, b5 = color & 0x1F;
	const uint32_t r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
	return 0xFF000000u | (b << 16) | (g << 8) | r;
}

// ===========================================================================
// Program start
// ===========================================================================

static void DisplayInit(void)
{
	for (int i = 0; i < 2; i++)
	{
		const SceUID block = sceKernelAllocMemBlock("display", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
		                                            VRAM_BLOCK_BYTES, nullptr);
		void* base = nullptr;
		sceKernelGetMemBlockBase(block, &base);
		vramBuffer[i] = (uint32_t*)base;
		//black around the frame
		if (base)
			memset(base, 0, VRAM_BUFFER_BYTES);
	}
	drawBuffer = 0;
}

//hands the buffer that was drawn into to the display
static void ShowBuffer(uint8_t which)
{
	SceDisplayFrameBuf fb;
	memset(&fb, 0, sizeof(fb));
	fb.size = sizeof(fb);
	fb.base = vramBuffer[which];
	fb.pitch = VRAM_STRIDE;
	fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	fb.width = SCREEN_WIDTH;
	fb.height = SCREEN_HEIGHT;
	sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	Game_Setup();
	while (true)
		Game_Loop();
	sceKernelExitProcess(0);
	return 0;
}

// ===========================================================================
// Drawing
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
//everything is drawn into the screen buffer
void PlatformVitaDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformVitaDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformVitaDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformVitaDisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the frame that is sent to the display. The area
//the pixels go into and where the next one goes, in game coordinates
static int32_t windowLeft = 0, windowRight = -1;
static int32_t cursorX = 0, cursorY = 0;

void PlatformVitaDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	windowLeft = x;
	windowRight = x + w - 1;
	cursorX = x;
	cursorY = y;
}

//the next pixel of the window, what falls outside the game's screen is left out
static inline void WritePixel(uint16_t color)
{
	if ((cursorX >= 0) && (cursorX < WINDOW_WIDTH) && (cursorY >= 0) && (cursorY < WINDOW_HEIGHT))
		frame[cursorY * WINDOW_WIDTH + cursorX] = color;
	if (++cursorX > windowRight)
	{
		cursorX = windowLeft;
		cursorY++;
	}
}

void PlatformVitaDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8));
		WritePixel(value);
	}
}

void PlatformVitaDisplay::writeColor(uint16_t color, uint32_t length)
{
	while (length--)
		WritePixel(color);
}

void PlatformVitaDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &frame[row * WINDOW_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = color;
	}
}
#endif

void PlatformVitaGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

//Draws the character the way LovyanGFX draws its default font: a 6x8 cell times the text
//size, the 5 columns of the glyph and a column of background, the background only when it
//differs from the text colour. Every column goes out as runs of one colour
size_t PlatformVitaGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

bool PlatformVitaBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformVitaBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
#elif SCREENBUFFER == 1
	for (int32_t row = y; row < y + h; row++)
		for (int32_t column = x; column < x + w; column++)
			SetBufferBit(pixels, column, row, color);
#else
	(void)color;
#endif
}

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	bufferSetColor = ToDisplay(setColor);
	bufferClearColor = ToDisplay(clearColor);
#else
	(void)setColor;
	(void)clearColor;
#endif
}

// ===========================================================================
// The frame on the display
// ===========================================================================

//The pixel of game column x of a row of what was drawn, in the display's own format. The screen
//buffer keeps its pixels the way LovyanGFX does, which is not the display's layout
static inline uint32_t FramePixel(const uint8_t* row, int32_t x)
{
#if SCREENBUFFER == 16
	const uint16_t value = ((const uint16_t*)(const void*)row)[x];
	//byte swapped in the buffer
	return ToDisplay((uint16_t)((value >> 8) | (value << 8)));
#elif SCREENBUFFER == 8
	return bufferPalette[row[x]];
#elif SCREENBUFFER == 1
	//most significant bit first as SetBufferBit writes
	return (row[x >> 3] & (0x80 >> (x & 7))) ? bufferSetColor : bufferClearColor;
#else
	return ToDisplay(((const uint16_t*)(const void*)row)[x]);
#endif
}

//bytes of one row of what was drawn
#if SCREENBUFFER == 8
#define FRAME_ROW_BYTES WINDOW_WIDTH
#elif SCREENBUFFER == 1
#define FRAME_ROW_BYTES ((WINDOW_WIDTH + 7) / 8)
#else
#define FRAME_ROW_BYTES (WINDOW_WIDTH * 2)
#endif

void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
#else
	const uint8_t* src = (const uint8_t*)frame;
#endif
	if (!src || !vramBuffer[drawBuffer])
		return;

	//the buffer that is not on the display is drawn into, so nothing is ever seen half drawn
	uint32_t* out = vramBuffer[drawBuffer] + OFFSET_Y * VRAM_STRIDE + OFFSET_X;
	for (int32_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * FRAME_ROW_BYTES];
		for (int32_t x = 0; x < WINDOW_WIDTH; x++)
		{
			const uint32_t pixel = FramePixel(row, x);
			uint32_t* d = &out[x * VITA_SCALE];
			for (int32_t n = 0; n < VITA_SCALE; n++)
				d[n] = pixel;
		}
		//the row is as many display rows high as it is wide, copied from the one just written
		for (int32_t n = 1; n < VITA_SCALE; n++)
			memcpy(out + n * VRAM_STRIDE, out, OUT_WIDTH * 4);
		out += VRAM_STRIDE * VITA_SCALE;
	}

	//The buffer goes on the display at the next vertical blank, and the wait is for that blank: only
	//then is the other buffer off the display and safe for the next frame to be drawn into
	ShowBuffer(drawBuffer);
	sceDisplayWaitVblankStart();
	drawBuffer ^= 1;
}

// ===========================================================================
// Buttons
// ===========================================================================

//how far the stick has to be pushed from the middle before it counts as a direction
#define ANALOG_THRESHOLD 48

uint8_t Platform_GetButtons(void)
{
	SceCtrlData pad;
	memset(&pad, 0, sizeof(pad));
	sceCtrlPeekBufferPositive(0, &pad, 1);
	uint8_t buttons = 0;
	if (pad.buttons & SCE_CTRL_LEFT)
		buttons |= BUTTON_LEFT;
	if (pad.buttons & SCE_CTRL_UP)
		buttons |= BUTTON_UP;
	if (pad.buttons & SCE_CTRL_DOWN)
		buttons |= BUTTON_DOWN;
	if (pad.buttons & SCE_CTRL_RIGHT)
		buttons |= BUTTON_RIGHT;
	if (pad.buttons & SCE_CTRL_CROSS)
		buttons |= BUTTON_A;
	if (pad.buttons & SCE_CTRL_CIRCLE)
		buttons |= BUTTON_B;
	//the shoulder buttons report as the triggers on a Vita and as L1/R1 on a PlayStation TV or a
	//DualShock, so both count
	if (pad.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_L1))
		buttons |= BUTTON_L;
	if (pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1))
		buttons |= BUTTON_R;
	//the left stick works as a d-pad as well, as the PyGamer's joystick does
	const int x = (int)pad.lx - 128, y = (int)pad.ly - 128;
	if (x < -ANALOG_THRESHOLD)
		buttons |= BUTTON_LEFT;
	if (x > ANALOG_THRESHOLD)
		buttons |= BUTTON_RIGHT;
	if (y < -ANALOG_THRESHOLD)
		buttons |= BUTTON_UP;
	if (y > ANALOG_THRESHOLD)
		buttons |= BUTTON_DOWN;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return (uint32_t)sceKernelGetProcessTimeLow();
}

//the tone playing: its frequency (0 = silence), how many samples are left of it (0 = until the next
//tone or Platform_StopTone), and where in its period the wave is, in 1/65536 of a period. The sound
//thread reads these while the game writes them, a tone starting a moment late is not worth locking for
static volatile uint32_t toneFreq = 0;
static volatile uint32_t toneSamplesLeft = 0;
static uint32_t tonePhase = 0;

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	//a frequency of 0 is a rest
	toneSamplesLeft = duration ? (uint32_t)((uint64_t)duration * AUDIO_SAMPLE_RATE / 1000) : 0;
	if (duration && !toneSamplesLeft)
		toneSamplesLeft = 1;
	tonePhase = 0;
	toneFreq = freq;
}

//The sound thread: it keeps handing the port the next block of samples, which is what paces it
static int AudioThread(SceSize args, void* argp)
{
	(void)args;
	(void)argp;
	const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRAIN, AUDIO_SAMPLE_RATE,
	                                     SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0)
		return 0;
	static int16_t samples[AUDIO_GRAIN * 2];
	const int16_t level = (int16_t)(16000 * SOUNDVOLUME / 100);
	while (true)
	{
		for (int i = 0; i < AUDIO_GRAIN; i++)
		{
			int16_t value = 0;
			const uint32_t freq = toneFreq;
			if (freq)
			{
				value = (tonePhase < 32768) ? level : (int16_t)-level;
				tonePhase = (tonePhase + (uint32_t)((uint64_t)freq * 65536 / AUDIO_SAMPLE_RATE)) & 0xFFFF;
				if (toneSamplesLeft && (--toneSamplesLeft == 0))
					toneFreq = 0;
			}
			samples[i * 2] = value;
			samples[i * 2 + 1] = value;
		}
		sceAudioOutOutput(port, samples);
	}
	return 0;
}

//the Vita hands out memory as it is asked for, the game has far more than it needs
uint32_t Platform_FreeHeap(void)
{
	return 0;
}

uint32_t Platform_FreeStack(void)
{
	return 0;
}

uint32_t Platform_RandomSeed(void)
{
	return (uint32_t)sceKernelGetProcessTimeLow();
}

void Platform_Log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	//only reaches a computer with the Vita's debug output, on the device itself it goes nowhere
	vprintf(format, args);
	va_end(args);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in a file in the game's own data folder. Bytes the file does not
// have yet read as 0xFF, the way erased flash does on the ESPboy, so the game sees a never saved store.
// ===========================================================================

static void StorageLoad(uint8_t* block)
{
	memset(block, 0xFF, PLATFORM_STORAGE_SIZE);
	FILE* file = fopen(storagePath, "rb");
	if (file)
	{
		size_t got = fread(block, 1, PLATFORM_STORAGE_SIZE, file);
		(void)got;
		fclose(file);
	}
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	uint8_t block[PLATFORM_STORAGE_SIZE];
	StorageLoad(block);
	memcpy(data, block + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	uint8_t block[PLATFORM_STORAGE_SIZE];
	StorageLoad(block);
	//storing what is already there does not touch the file
	if (memcmp(block + offset, data, length) == 0)
		return;
	memcpy(block + offset, data, length);
	FILE* file = fopen(storagePath, "wb");
	if (!file)
	{
		Platform_Log("could not write %s\n", storagePath);
		return;
	}
	fwrite(block, 1, PLATFORM_STORAGE_SIZE, file);
	fclose(file);
}

// ===========================================================================
// Start up
// ===========================================================================

void Platform_Init(const char* appName)
{
	(void)appName;
	DisplayInit();

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

	//the save file lives in the game's own folder, which is made if it is not there yet
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir("ux0:data/" VITA_APP_NAME, 0777);
	snprintf(storagePath, sizeof(storagePath), "ux0:data/%s/%s.sav", VITA_APP_NAME, VITA_APP_NAME);

	const SceUID thread = sceKernelCreateThread("sound", AudioThread, 0x10000100, 0x10000, 0, 0, nullptr);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, nullptr);

#if SCREENBUFFER
	screenBuffer.setColorDepth(SCREENBUFFER);
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
#endif
#if SCREENBUFFER == 8
	//RGB332 to RGB565 the way LovyanGFX converts it, so the colours match the other devices
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
		const uint16_t color = (uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3));
		bufferPalette[i] = ToDisplay(color);
	}
#endif
}

#endif
