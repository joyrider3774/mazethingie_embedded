#ifndef PLATFORM_PYBADGE_H
#define PLATFORM_PYBADGE_H

//The Adafruit PyBadge and PyGamer part of Platform.h, included by it. See "The device" in
//Platform.h for what this supplies, the functions are in PlatformPyBadge.cpp.
//
//Both are a SAMD51J19 (Cortex-M4F, 120 MHz) with 512 KB of flash, of which the bootloader keeps
//16 KB, 192 KB of RAM, a 160x128 ST7735R display, QSPI flash for files and the same buttons on
//a shift register. They differ in the d-pad: the PyBadge has buttons on the shift register, the
//PyGamer an analog joystick. The display is driven by Adafruit's ST7735 library, which uses DMA
//on these boards. The class below offers the LovyanGFX calls the game makes on top of it, so the
//game's LOVYANGFX drawing paths work unchanged. The game's 128x128 screen sits in the middle of
//the display.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. There is RAM for a full colour
//16 bpp buffer (32 KB of the 192 KB). A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16"
#endif

//The ST7735 driver with the game's screen as its whole drawing area: everything it draws is moved
//into the middle of the display and clipped at the edges of the game's screen. On top of that the
//LovyanGFX calls the game makes, with the arguments LovyanGFX takes
class PlatformPyBadgeDisplay : public Adafruit_ST7735
{
public:
	PlatformPyBadgeDisplay(void);
	void init(void);

	//The game opens a transaction around groups of calls that open and close one of their own,
	//only the outermost one really starts and ends it. Adafruit's drawing functions call these
	//through the base class, so they count as well
	void startWrite(void) override;
	void endWrite(void) override;

	//swap true: the values are plain RGB565 and are put in display order. False: they already
	//are in display order. Hides the library's own writePixels, whose third and fourth arguments
	//mean something else
	void writePixels(const uint16_t* data, int32_t length, bool swap = true);

	//draws character c of the GLCD font at x,y in the colours and size set with setTextColor
	//and setTextSize, and returns how far the text moves on. A background the same as the text
	//colour is not painted, as in LovyanGFX
	size_t drawChar(uint16_t c, int32_t x, int32_t y);

private:
	uint8_t writeDepth = 0;
};

//An off screen buffer of the game's size, 1, 8 or 16 bits per pixel, laid out the way LovyanGFX's
//sprites keep them: byte swapped RGB565, RGB332, or 1 bpp packed most significant bit first.
//Adafruit_GFX draws the rectangles and text into it a pixel or rectangle at a time
class PlatformPyBadgeBuffer : public Adafruit_GFX
{
public:
	//in PlatformPyBadge.cpp, the game's screen size is not defined yet where this is included
	PlatformPyBadgeBuffer(void);
	void setColorDepth(uint8_t bits) { depth = bits; }
	bool createSprite(int32_t w, int32_t h);
	void* getBuffer(void) { return pixels; }

	void drawPixel(int16_t x, int16_t y, uint16_t color) override;
	void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
	//as PlatformPyBadgeDisplay::drawChar
	size_t drawChar(uint16_t c, int32_t x, int32_t y);

private:
	uint8_t* pixels = nullptr;
	uint8_t depth = 16;
};

typedef PlatformPyBadgeDisplay PlatformDisplay;
typedef PlatformPyBadgeBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//flash is ordinary memory on the SAMD51, it can be read like any other
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the SAMD51 itself, memcpy keeps a read from an odd
//address inside a byte array well defined
static inline uint16_t PlatformPyBadge_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformPyBadge_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
