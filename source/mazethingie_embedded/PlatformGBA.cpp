//Platform.h for the Game Boy Advance, built with devkitARM and libgba (see gba/CMakeLists.txt):
//  display  240x160, 15 bit BGR pixels. The game's 128x128 screen buffer goes to a mode 5 bitmap,
//           160x128, and the GBA's background scaling shows it at 160x160 in the middle
//  buttons  d-pad, A, B, L and R
//  sound    the square wave of sound channel 2
//  saves    the cartridge's battery backed SRAM, marked with an SRAM_V string so emulators and flash
//           carts know the save type
//  time     counts vertical blanks, 59.73 of them a second
//
//main() here starts the game and runs Game_Loop once every vertical blank, the game holds its frames
//to its own frame rate itself.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_GBA

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <gba_base.h>
#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <gba_timers.h>
#include <gba_dma.h>
#include "PlatformGamebuinoFont.h"

//The bitmaps the frame is written to: mode 5 has two pages of 160x128 15 bit pixels, one shown while
//the other is written, so a frame never appears half written (no tearing)
#define BITMAP_WIDTH 160
#define BITMAP_HEIGHT 128
#define BITMAP_PAGE(n) ((uint16_t*)(VRAM + (n) * 0xA000))
//bit 4 of the display register shows the second page
#define DISPLAY_PAGE2 0x0010
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

//sound registers, with names of their own
#define SOUND2_CONTROL (*(volatile uint16_t*)0x04000068)
#define SOUND2_FREQUENCY (*(volatile uint16_t*)0x0400006C)
#define SOUND_MIX (*(volatile uint16_t*)0x04000080)
#define SOUND_MIX_DS (*(volatile uint16_t*)0x04000082)
#define SOUND_MASTER (*(volatile uint16_t*)0x04000084)

//the cartridge's battery backed save RAM
#define CART_SRAM ((uint8_t*)0x0E000000)

//How long the GBA waits for the cartridge and its work RAM. The values it starts with are the slowest
//ones, and the program is read from the cartridge: 3 and 1 cycles with the prefetch buffer on, and
//8 for the save RAM, is what GBA games use. Work RAM (EWRAM) can run on 1 wait state instead of 2,
//which the screen buffer and the frame before it are read and written with
#define REG_WAITSTATES (*(volatile uint16_t*)0x04000204)
#define REG_MEMORY_CONTROL (*(volatile uint32_t*)0x04000800)

//Emulators and flash carts find the save type by looking for this in the ROM. Its address is stored
//in a volatile variable at the start, so the linker can not leave it out as unused
static const char saveType[] = "SRAM_V113";
static const char* volatile saveTypeInROM = nullptr;

static PlatformGBADisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and written to the display at the next vertical blank
PlatformBuffer screenBuffer;
#endif
#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in, as 15 bit pixels, and whether they changed
static uint16_t bufferSetColor = 0x7FFF, bufferClearColor = 0x0000;
static bool coloursChanged = true;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as a 15 bit pixel
static uint16_t bufferPalette[256];
#endif

//RGB565 to the GBA's 15 bit BGR, blue in the top bits
static inline uint16_t ToBGR555(uint16_t color)
{
	return (uint16_t)(((color >> 11) & 0x1F) | (((color >> 6) & 0x1F) << 5) | ((color & 0x1F) << 10));
}

// ===========================================================================
// Program start
// ===========================================================================

//vertical blanks since the start
static volatile uint32_t vblanks = 0;
//the vertical blank a tone ends at, 0 while none has to end
static volatile uint32_t toneEnd = 0;
static void SendFrame(void);

static void ToneOff(void);

static void VBlankInterrupt(void)
{
	vblanks++;
	if (toneEnd && (vblanks >= toneEnd))
	{
		toneEnd = 0;
		ToneOff();
	}
}

int main(void)
{
	REG_WAITSTATES = 0x4317;
	REG_MEMORY_CONTROL = 0x0E000020;
	irqInit();
	irqSet(IRQ_VBLANK, VBlankInterrupt);
	irqEnable(IRQ_VBLANK);
	Game_Setup();
	while (true)
	{
		//the frame drawn last goes out at the start of the blank, before the next one is drawn
		VBlankIntrWait();
		SendFrame();
		Game_Loop();
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
void PlatformGBADisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformGBADisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformGBADisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformGBADisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the display's pixels. It draws into the page
//that is not on the display, so a frame is never seen half drawn: at the end of the frame that page
//goes on the display and its pixels are copied into the other one, which the game draws the next
//frame into. The area the pixels go into and where the next one goes, in game coordinates
static uint8_t drawPage = 0;
static int32_t windowLeft = 0, windowRight = -1, windowBottom = -1;
static int32_t cursorX = 0, cursorY = 0;

void PlatformGBADisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
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

void PlatformGBADisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8));
		WritePixel(ToBGR555(value));
	}
}

void PlatformGBADisplay::writeColor(uint16_t color, uint32_t length)
{
	const uint16_t bgr = ToBGR555(color);
	while (length--)
		WritePixel(bgr);
}

void PlatformGBADisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

void PlatformGBAGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformGBAGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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
//The screen buffer, read and written for every pixel the game draws and again when the frame is sent.
//In IWRAM, the GBA's fast memory, when BUFFERINIWRAM says it fits there next to the game's globals
//(the .bss of a GBA program is in IWRAM), otherwise in EWRAM
#if SCREENBUFFER == 16
#define BUFFER_BYTES (WINDOW_WIDTH * WINDOW_HEIGHT * 2)
#elif SCREENBUFFER == 8
#define BUFFER_BYTES (WINDOW_WIDTH * WINDOW_HEIGHT)
#else
#define BUFFER_BYTES ((WINDOW_WIDTH + 7) / 8 * WINDOW_HEIGHT)
#endif
#if BUFFERINIWRAM
static uint8_t bufferMemory[BUFFER_BYTES] ALIGN(4);
#else
EWRAM_BSS static uint8_t bufferMemory[BUFFER_BYTES] ALIGN(4);
#endif

bool PlatformGBABuffer::createSprite(int32_t w, int32_t h)
{
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	if (bytes > sizeof(bufferMemory))
		return false;
	pixels = bufferMemory;
	memset(pixels, 0, bytes);
	return true;
}

void PlatformGBABuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
	//every pixel on the display has the old colours, the frame that takes the new ones goes out whole
	coloursChanged = true;
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

//The frame as each page was last written, in EWRAM: only the rows that differ from it are written
//again, and of those only the columns that changed
EWRAM_BSS static uint8_t sentFrame[2][BUFFER_ROW_BYTES * WINDOW_HEIGHT];
static bool sentFrameValid[2] = { false, false };
//the page being written, the other one is on the display
static uint8_t writePage = 1;
//the game finished a frame since the last one was written
static bool frameReady = false;

void Platform_PresentFrame(void)
{
	frameReady = true;
}

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

//Writes what changed in the frame since it was last written, into the top left of the bitmap. In
//IWRAM: it runs for every frame and reads the whole buffer, so it is worth the space
IWRAM_CODE static void SendFrame(void)
{
	if (!frameReady)
		return;
	frameReady = false;
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
#if SCREENBUFFER == 1
	if (coloursChanged)
	{
		sentFrameValid[0] = sentFrameValid[1] = false;
		coloursChanged = false;
	}
#endif
	uint16_t* const bitmap = BITMAP_PAGE(writePage);
	for (uint16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * BUFFER_ROW_BYTES];
		uint8_t* sent = &sentFrame[writePage][y * BUFFER_ROW_BYTES];
		uint16_t first = 0, last = WINDOW_WIDTH - 1;
		if (sentFrameValid[writePage])
		{
			//a row is compared 4 bytes at a time, both are aligned
			const uint32_t* rowWords = (const uint32_t*)(const void*)row;
			const uint32_t* sentWords = (const uint32_t*)(const void*)sent;
			uint16_t wordA = 0, wordB = BUFFER_ROW_BYTES / 4;
			while ((wordA < wordB) && (rowWords[wordA] == sentWords[wordA]))
				wordA++;
			if (wordA == wordB)
				continue;
			while (rowWords[wordB - 1] == sentWords[wordB - 1])
				wordB--;
			uint16_t a = wordA * 4, b = wordB * 4;
			while (row[a] == sent[a])
				a++;
			while (row[b - 1] == sent[b - 1])
				b--;
#if SCREENBUFFER == 16
			first = a / 2;
			last = (b - 1) / 2;
#elif SCREENBUFFER == 8
			first = a;
			last = b - 1;
#else
			first = a * 8;
			last = (b * 8 - 1 < WINDOW_WIDTH - 1) ? b * 8 - 1 : WINDOW_WIDTH - 1;
#endif
		}
		uint16_t* d = &bitmap[y * BITMAP_WIDTH];
		for (uint16_t x = first; x <= last; x++)
			d[x] = BufferPixel(row, x);
		memcpy(sent, row, BUFFER_ROW_BYTES);
	}
	sentFrameValid[writePage] = true;
	//the page just written goes on the display, the one leaving it is written next
	if (writePage)
		REG_DISPCNT |= DISPLAY_PAGE2;
	else
		REG_DISPCNT &= ~DISPLAY_PAGE2;
	writePage ^= 1;
}
#else
//the game drew straight into the page that is not on the display, it can go on it now
static bool frameReady = false;

void Platform_PresentFrame(void)
{
	frameReady = true;
}

//shows the page the game drew and copies it into the other one, which the next frame is drawn into
static void SendFrame(void)
{
	if (!frameReady)
		return;
	frameReady = false;
	if (drawPage)
		REG_DISPCNT |= DISPLAY_PAGE2;
	else
		REG_DISPCNT &= ~DISPLAY_PAGE2;
	const uint8_t nextPage = drawPage ^ 1;
	dmaCopy(BITMAP_PAGE(drawPage), BITMAP_PAGE(nextPage), BITMAP_WIDTH * BITMAP_HEIGHT * sizeof(uint16_t));
	drawPage = nextPage;
}
#endif

static void DisplayInit(void)
{
	//mode 5 with its bitmap on background 2, black around it (the backdrop)
	REG_DISPCNT = MODE_5 | BG2_ON;
	memset(BITMAP_PAGE(0), 0, BITMAP_WIDTH * BITMAP_HEIGHT * sizeof(uint16_t));
	memset(BITMAP_PAGE(1), 0, BITMAP_WIDTH * BITMAP_HEIGHT * sizeof(uint16_t));
	BG_COLORS[0] = 0;
	//Background 2's matrix maps display pixels to bitmap pixels, 8.8 fixed point. Scaled: 0.8 bitmap
	//pixels per display pixel (the game's 128 lines fill the display's 160) with the 160 wide game in
	//the middle. 1:1: the 128x128 game in the middle of the display
#if SCALESCREEN
	const int32_t step = (WINDOW_WIDTH * 256 + SCREEN_HEIGHT / 2) / SCREEN_HEIGHT;
	const int32_t left = (SCREEN_WIDTH - SCREEN_HEIGHT) / 2;
	REG_BG2PA = (int16_t)step;
	REG_BG2PD = (int16_t)step;
	REG_BG2X = -left * step;
	REG_BG2Y = 0;
#else
	REG_BG2PA = 256;
	REG_BG2PD = 256;
	REG_BG2X = -((SCREEN_WIDTH - WINDOW_WIDTH) / 2) * 256;
	REG_BG2Y = -((SCREEN_HEIGHT - WINDOW_HEIGHT) / 2) * 256;
#endif
	REG_BG2PB = 0;
	REG_BG2PC = 0;
}

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	//a bit is 0 while its key is held
	const uint16_t keys = (uint16_t)~REG_KEYINPUT;
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

//a vertical blank is 1/59.7275 of a second
#define MICROS_PER_VBLANK 16743u

uint32_t Platform_Micros(void)
{
	return vblanks * MICROS_PER_VBLANK;
}

//volume 0 and restarted: the channel goes quiet
static void ToneOff(void)
{
	SOUND2_CONTROL = (uint16_t)(2 << 6);
	SOUND2_FREQUENCY = 0x8000;
}

static void SoundInit(void)
{
	//sound on, channel 2 on both sides at full mix volume, the DMG channels at 100%
	SOUND_MASTER = 0x0080;
	SOUND_MIX = 0x0077 | (1 << 9) | (1 << 13);
	SOUND_MIX_DS = 0x0002;
	ToneOff();
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	//a frequency of 0 is a rest, and the channel can not go below about 64 Hz
	if (freq < 64)
	{
		ToneOff();
		return;
	}
	const uint16_t volume = (uint16_t)(15 * SOUNDVOLUME / 100);
	//50% duty, no envelope, the initial volume in the top 4 bits
	SOUND2_CONTROL = (uint16_t)((volume << 12) | (2 << 6));
	//the channel's rate for freq Hz, restarted, playing until stopped
	SOUND2_FREQUENCY = (uint16_t)(0x8000 | (2048 - 131072 / freq));
	if (duration)
	{
		//at least one blank, ended in the vertical blank interrupt
		uint32_t blanks = ((uint32_t)duration * 1000u + MICROS_PER_VBLANK - 1) / MICROS_PER_VBLANK;
		toneEnd = vblanks + (blanks ? blanks : 1);
	}
	else
		toneEnd = 0;
}

extern "C" char* sbrk(int increment);
extern char __eheap_end;
extern char __bss_end__;

//the heap in EWRAM: what is left after the end of it, plus what malloc was given back
uint32_t Platform_FreeHeap(void)
{
	return (uint32_t)(&__eheap_end - sbrk(0)) + (uint32_t)mallinfo().fordblks;
}

//the stack in IWRAM that is free right now, down to the globals
uint32_t Platform_FreeStack(void)
{
	return (uint32_t)((char*)__builtin_frame_address(0) - &__bss_end__);
}

uint32_t Platform_RandomSeed(void)
{
	//The time since switching on and where the display is. The same on every start of an emulator,
	//it only differs with how long the game took to start
	return (vblanks * 2654435761u) ^ ((uint32_t)REG_VCOUNT << 16) ^ REG_TM0CNT_L;
}

void Platform_Log(const char* format, ...)
{
	(void)format;
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in RAM and in the cartridge's SRAM, which is read and written a
// byte at a time. Never saved SRAM reads as 0xFF on emulators, as erased flash does on the ESPboy.
// ===========================================================================

static uint8_t storage[PLATFORM_STORAGE_SIZE];

static void StorageInit(void)
{
	for (uint16_t i = 0; i < PLATFORM_STORAGE_SIZE; i++)
		storage[i] = CART_SRAM[i];
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	for (uint16_t i = 0; i < length; i++)
	{
		//only the bytes that really differ are written
		if (storage[offset + i] != data[i])
		{
			storage[offset + i] = data[i];
			CART_SRAM[offset + i] = data[i];
		}
	}
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	(void)appName;
	saveTypeInROM = saveType;
	//a free running timer for the random seed
	REG_TM0CNT_L = 0;
	REG_TM0CNT_H = TIMER_START;
	DisplayInit();
	SoundInit();
	StorageInit();
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
