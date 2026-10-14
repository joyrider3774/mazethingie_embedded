//Platform.h for the Nintendo DS, built with devkitARM, libnds and calico (see nds/CMakeLists.txt):
//  display  the top screen, 256x192 of 15 bit BGR pixels. The game's 128x128 screen is a bitmap
//           background of its own, scaled to 192x192 in the middle of the display. The bottom
//           screen stays dark
//  buttons  d-pad, A, B, L and R
//  sound    one of the DS's PSG channels, a square wave
//  saves    a file on the card the game was started from (libfat), or nothing when there is none
//  time     a free running timer, microseconds
//
//main() here starts the game and runs Game_Loop once every vertical blank, the game holds its frames
//to its own frame rate itself.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_NDS

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <nds.h>
#include <fat.h>
#include "PlatformGamebuinoFont.h"

//The bitmaps the frame is drawn in: two pages of 128x128 15 bit pixels in the first video RAM bank,
//one shown while the other is drawn, so a frame never appears half drawn (no tearing)
#define BITMAP_WIDTH 128
#define BITMAP_HEIGHT 128
#define BITMAP_BYTES (BITMAP_WIDTH * BITMAP_HEIGHT * (int)sizeof(uint16_t))
//a bitmap background's base is counted in 16 KB steps, a page is two of them
#define BITMAP_PAGE(n) ((uint16_t*)((uint8_t*)BG_GFX + (n) * BITMAP_BYTES))
#define BITMAP_PAGE_BASE(n) ((n) * (BITMAP_BYTES / (16 * 1024)))
//SCREEN_WIDTH and SCREEN_HEIGHT are the display's 256x192, libnds has them already

//the timer cpuStartTiming counts with, ticks a second
#define TIMER_TICKS_PER_SECOND BUS_CLOCK

static PlatformNDSDisplay display;
PlatformDisplay& platformDisplay = display;

//the background the game's bitmap is on
static int bitmapBg = -1;

#if SCREENBUFFER
//the whole frame is drawn in here and written to the bitmap when the frame is done
PlatformBuffer screenBuffer;
#endif
#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in, as 15 bit pixels
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x8000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as a 15 bit pixel
static uint16_t bufferPalette[256];
#endif

//RGB565 to the DS's 15 bit BGR, blue in the top bits. Bit 15 is what makes a bitmap pixel show at
//all, so every pixel the game draws carries it
static inline uint16_t ToBGR555(uint16_t color)
{
	return (uint16_t)(0x8000 | ((color >> 11) & 0x1F) | (((color >> 6) & 0x1F) << 5) | ((color & 0x1F) << 10));
}

// ===========================================================================
// Program start
// ===========================================================================

//the microsecond a tone ends at, 0 while none has to end
static uint32_t toneEnd = 0;
static void SendFrame(void);
static void ToneOff(void);
static void StorageFlush(void);

int main(void)
{
	Game_Setup();
	while (true)
	{
		//the frame drawn last goes out at the start of the blank, before the next one is drawn
		swiWaitForVBlank();
		SendFrame();
		//a tone that has played long enough stops here, so nothing has to be timed while drawing
		if (toneEnd && ((int32_t)(Platform_Micros() - toneEnd) >= 0))
		{
			toneEnd = 0;
			ToneOff();
		}
		Game_Loop();
		//a save that was written this frame reaches the card here, once the drawing is done
		StorageFlush();
	}
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
//everything is drawn into the screen buffer
void PlatformNDSDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformNDSDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformNDSDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformNDSDisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the bitmap that is not on the display, so a
//frame is never seen half drawn: at the end of the frame that page goes on the display and its
//pixels are copied into the other one, which the game draws the next frame into. The area the pixels
//go into and where the next one goes, in game coordinates
static uint8_t drawPage = 0;
static int32_t windowLeft = 0, windowRight = -1, windowBottom = -1;
static int32_t cursorX = 0, cursorY = 0;

void PlatformNDSDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	windowLeft = x;
	windowRight = x + w - 1;
	windowBottom = y + h - 1;
	cursorX = x;
	cursorY = y;
}

//the next pixel of the window, what falls outside the game's screen is left out
static inline void WritePixel(uint16_t bgr)
{
	if ((cursorX >= 0) && (cursorX < WINDOW_WIDTH) && (cursorY >= 0) && (cursorY < WINDOW_HEIGHT))
		BITMAP_PAGE(drawPage)[cursorY * BITMAP_WIDTH + cursorX] = bgr;
	if (++cursorX > windowRight)
	{
		cursorX = windowLeft;
		cursorY++;
	}
}

void PlatformNDSDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8));
		WritePixel(ToBGR555(value));
	}
}

void PlatformNDSDisplay::writeColor(uint16_t color, uint32_t length)
{
	const uint16_t bgr = ToBGR555(color);
	while (length--)
		WritePixel(bgr);
}

void PlatformNDSDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	const uint16_t bgr = ToBGR555(color);
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &BITMAP_PAGE(drawPage)[row * BITMAP_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = bgr;
	}
}
#endif

void PlatformNDSGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformNDSGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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
#if SCREENBUFFER == 16
#define BUFFER_BYTES (WINDOW_WIDTH * WINDOW_HEIGHT * 2)
#elif SCREENBUFFER == 8
#define BUFFER_BYTES (WINDOW_WIDTH * WINDOW_HEIGHT)
#else
#define BUFFER_BYTES ((WINDOW_WIDTH + 7) / 8 * WINDOW_HEIGHT)
#endif

bool PlatformNDSBuffer::createSprite(int32_t w, int32_t h)
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

void PlatformNDSBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
	bufferSetColor = ToBGR555(setColor);
	bufferClearColor = ToBGR555(clearColor);
#else
	(void)setColor;
	(void)clearColor;
#endif
}

//the game finished a frame since the last one was shown
static bool frameReady = false;

void Platform_PresentFrame(void)
{
	frameReady = true;
}

//shows the page that was drawn and points the game at the other one
static void ShowPage(uint8_t page, uint8_t nextPage)
{
	bgSetMapBase(bitmapBg, BITMAP_PAGE_BASE(page));
	bgUpdate();
	(void)nextPage;
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

//the page being written, the other one is on the display
static uint8_t writePage = 1;

//the 15 bit pixel of game pixel x of a buffer row
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

//writes the frame the game drew into the page that is not on the display, and shows it
static void SendFrame(void)
{
	if (!frameReady)
		return;
	frameReady = false;
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	uint16_t* const bitmap = BITMAP_PAGE(writePage);
	for (uint16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * BUFFER_ROW_BYTES];
		uint16_t* d = &bitmap[y * BITMAP_WIDTH];
		for (uint16_t x = 0; x < WINDOW_WIDTH; x++)
			d[x] = BufferPixel(row, x);
	}
	//the page just written goes on the display, the one leaving it is written next
	ShowPage(writePage, writePage ^ 1);
	writePage ^= 1;
}
#else
//shows the page the game drew and copies it into the other one, which the next frame is drawn into
static void SendFrame(void)
{
	if (!frameReady)
		return;
	frameReady = false;
	const uint8_t nextPage = drawPage ^ 1;
	ShowPage(drawPage, nextPage);
	dmaCopyWords(3, BITMAP_PAGE(drawPage), BITMAP_PAGE(nextPage), BITMAP_BYTES);
	drawPage = nextPage;
}
#endif

static void DisplayInit(void)
{
	//the first video RAM bank holds both pages of the bitmap, 32 KB each
	videoSetMode(MODE_5_2D);
	vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
	bitmapBg = bgInit(3, BgType_Bmp16, BgSize_B16_128x128, BITMAP_PAGE_BASE(0), 0);
	memset(BITMAP_PAGE(0), 0, BITMAP_BYTES);
	memset(BITMAP_PAGE(1), 0, BITMAP_BYTES);
	//black around the game's screen, and a black bottom one: the game has nothing to put there.
	//A display that is switched off shows white, so the bottom one is left on with no background
	//of its own, which shows the first colour of its palette
	BG_PALETTE[0] = 0;
	videoSetModeSub(MODE_0_2D);
	BG_PALETTE_SUB[0] = 0;

	//The background's matrix maps display pixels to bitmap pixels, 8.8 fixed point. Scaled: 0.67
	//bitmap pixels per display pixel (the game's 128 lines fill the display's 192) with the 192 wide
	//game in the middle. 1:1: the 128x128 game in the middle of the display
#if SCALESCREEN
	const int32_t step = (WINDOW_WIDTH * 256 + SCREEN_HEIGHT / 2) / SCREEN_HEIGHT;
	const int32_t left = (SCREEN_WIDTH - SCREEN_HEIGHT) / 2;
	bgSetAffineMatrixScroll(bitmapBg, step, 0, 0, step, -left * step, 0);
#else
	bgSetAffineMatrixScroll(bitmapBg, 256, 0, 0, 256,
	                        -((SCREEN_WIDTH - WINDOW_WIDTH) / 2) * 256,
	                        -((SCREEN_HEIGHT - WINDOW_HEIGHT) / 2) * 256);
#endif
	bgUpdate();
}

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	scanKeys();
	const uint32_t keys = keysHeld();
	uint8_t buttons = 0;
	if (keys & KEY_LEFT)
		buttons |= BUTTON_LEFT;
	if (keys & KEY_UP)
		buttons |= BUTTON_UP;
	if (keys & KEY_DOWN)
		buttons |= BUTTON_DOWN;
	if (keys & KEY_RIGHT)
		buttons |= BUTTON_RIGHT;
	if (keys & KEY_A)
		buttons |= BUTTON_A;
	if (keys & KEY_B)
		buttons |= BUTTON_B;
	if (keys & KEY_L)
		buttons |= BUTTON_L;
	if (keys & KEY_R)
		buttons |= BUTTON_R;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

//The timer cpuStartTiming runs wraps around every two minutes or so, well before the microseconds
//the game counts do. Its steps are added up here instead, so what this returns keeps climbing for as
//long as the game runs and only wraps where the game expects it to
static uint32_t lastTicks = 0;
static uint64_t totalTicks = 0;

uint32_t Platform_Micros(void)
{
	const uint32_t now = cpuGetTiming();
	totalTicks += (uint32_t)(now - lastTicks);
	lastTicks = now;
	return (uint32_t)(totalTicks * 1000000u / TIMER_TICKS_PER_SECOND);
}

//the channel a tone plays on, -1 while none does
static int toneChannel = -1;

#if TONEWAVE
//One cycle of a square wave as 8 bit signed samples, the same tone the other devices make. It is
//played as a sample rather than on one of the DS's own tone channels because those count their
//frequency in a 16 bit timer, which can not go below about 256 Hz: the game's explosion sweeps down
//to 40 Hz and every note under that limit came out at the wrong pitch. A cycle of 32 samples covers
//16 Hz to 2047 Hz, which holds every note the game plays
static const int8_t toneWave[32] = {
	 100,  100,  100,  100,  100,  100,  100,  100,
	 100,  100,  100,  100,  100,  100,  100,  100,
	-100, -100, -100, -100, -100, -100, -100, -100,
	-100, -100, -100, -100, -100, -100, -100, -100,
};
//the sample rate that carries a note, and the notes the DS's 16 bit timer can play at all
#define TONE_RATE(freq) ((uint32_t)(freq) * (uint32_t)sizeof(toneWave))
#define TONE_FREQ_MIN 16u
#define TONE_FREQ_MAX (65535u / (uint32_t)sizeof(toneWave))
#endif

static void ToneOff(void)
{
	if (toneChannel >= 0)
	{
		soundKill(toneChannel);
		toneChannel = -1;
	}
}

static void SoundInit(void)
{
	soundEnable();
#if TONEWAVE
	//the sound hardware reads the samples itself, so what the cache holds has to be in memory
	DC_FlushRange(toneWave, sizeof(toneWave));
#endif
	ToneOff();
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	ToneOff();
	//a frequency of 0 is a rest
	if (freq == 0)
	{
		toneEnd = 0;
		return;
	}
	//the sound channels take a volume of 0 to 127
	const uint8_t volume = (uint8_t)(127 * SOUNDVOLUME / 100);
#if TONEWAVE
	//the cycle above repeats at the note's frequency, so the samples go out 32 times as fast.
	//A note the timer can not reach is played at the nearest one it can
	uint16_t note = freq;
	if (note < TONE_FREQ_MIN)
		note = TONE_FREQ_MIN;
	if (note > TONE_FREQ_MAX)
		note = TONE_FREQ_MAX;
	toneChannel = soundPlaySample(toneWave, SoundFormat_8Bit, sizeof(toneWave),
	                              (uint16_t)TONE_RATE(note), volume, 64, true, 0);
#else
	toneChannel = soundPlayPSG(DutyCycle_50, freq, volume, 64);
#endif
	//0 keeps playing until the next tone, the main loop ends the others
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

uint32_t Platform_FreeHeap(void)
{
	//what is left between the end of the heap and where it may grow to, plus what malloc was given back
	return (uint32_t)(getHeapLimit() - getHeapEnd()) + (uint32_t)mallinfo().fordblks;
}

//the stack sits at the top of the ARM9's data tightly coupled memory and grows down, the game's
//variables in it end here
extern char __dtcm_bss_end[];

uint32_t Platform_FreeStack(void)
{
	return (uint32_t)((char*)__builtin_frame_address(0) - __dtcm_bss_end);
}

uint32_t Platform_RandomSeed(void)
{
	//the time since switching on, which differs with how long the loader and the game took to start
	return (uint32_t)(cpuGetTiming() * 2654435761u);
}

void Platform_Log(const char* format, ...)
{
	//goes to the debugger of an emulator, there is no room for it on the displays
	char message[128];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	nocashMessage(message);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in RAM and in a file on the card the game was started from, which
// is written at the end of a frame it changed in. Without a card that libfat can write to (a DS card
// with no save support of its own, an emulator without one) the block still works, it is only not
// there again the next time the game starts.
// ===========================================================================

static uint8_t storage[PLATFORM_STORAGE_SIZE];
static char storagePath[64];
static bool storageOnCard = false;
static bool storageChanged = false;

static void StorageInit(const char* appName)
{
	memset(storage, 0xFF, sizeof(storage));
	if (!fatInitDefault())
		return;
	snprintf(storagePath, sizeof(storagePath), "/%s.sav", (appName && appName[0]) ? appName : "game");
	//a space in the name would do no harm, but a path without one is easier to find
	for (char* c = storagePath; *c; c++)
		if (*c == ' ')
			*c = '_';
	storageOnCard = true;
	FILE* file = fopen(storagePath, "rb");
	if (!file)
		return;
	fread(storage, 1, sizeof(storage), file);
	fclose(file);
}

//writes the block to the card, called at the end of a frame it changed in
static void StorageFlush(void)
{
	if (!storageChanged)
		return;
	storageChanged = false;
	if (!storageOnCard)
		return;
	FILE* file = fopen(storagePath, "wb");
	if (!file)
		return;
	fwrite(storage, 1, sizeof(storage), file);
	fclose(file);
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	if (memcmp(storage + offset, data, length) == 0)
		return;
	memcpy(storage + offset, data, length);
	storageChanged = true;
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	//the main loop waits for the vertical blank, which needs its interrupt
	irqEnable(IRQ_VBLANK);
	//the timer the microseconds are counted with, before anything asks for them
	cpuStartTiming(0);
	lastTicks = cpuGetTiming();
	DisplayInit();
	SoundInit();
	StorageInit(appName);
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
