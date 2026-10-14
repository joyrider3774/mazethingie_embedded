#ifndef PLATFORM_TUFTY_H
#define PLATFORM_TUFTY_H

//The Pimoroni Tufty 2350 part of Platform.h, included by it. See "The device" in Platform.h for
//what this supplies, the functions are in PlatformTufty.cpp.
//
//The Tufty 2350 is an RP2350B (two Cortex-M33 at 150 MHz) with 512 KB of RAM, 8 MB of PSRAM, 16 MB of
//flash and a 320x240 ST7789 display on an 8 bit parallel bus, the same display and bus as the
//Pimoroni Explorer. It builds with the arduino-pico core as its Generic RP2350 board. LovyanGFX's
//RP2040 support does not build with that core's Pico SDK and has no parallel bus for it either, so
//as on the PicoSystem the display class is a small one of its own below: it offers the LovyanGFX
//calls the game makes, the same way LovyanGFX takes them, so the game's LOVYANGFX drawing paths
//work unchanged.
//
//With a screen buffer the game's 128x128 frame is scaled up to 240x240 in the middle of the display,
//SCALESCREEN can show it 1:1 in the middle instead. Drawn straight to the display (SCREENBUFFER 0)
//it always sits 1:1 in the middle.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. There is RAM for a full colour
//16 bpp buffer (32 KB of the 512 KB), and only a buffer can be scaled up to the display.
//A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16"
#endif

//1 = the frame is scaled up to 240x240, as high as the display, in its middle. 0 = it is shown 1:1
//in the middle. Only what changed since the last frame is sent. Only a frame from a screen buffer
//can be scaled, drawn straight to the display (SCREENBUFFER 0) the game is always shown 1:1.
//A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//backlight brightness, 0 (off) to 255, with a gamma so the steps look even. A build can still set
//it itself
#ifndef BACKLIGHT
#define BACKLIGHT 200
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the
//text of the GLCD font, with the arguments LovyanGFX takes
class PlatformTuftyGFX
{
public:
	virtual ~PlatformTuftyGFX() {}

	//RGB565 from 8 bit channels
	static uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
	{
		return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	}

	virtual void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) = 0;
	void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);

	//a background the same as the text colour is not painted, as in LovyanGFX
	void setTextColor(uint16_t fg, uint16_t bg) { textColor = fg; textBackground = bg; }
	void setTextSize(uint8_t size) { textSize = size ? size : 1; }
	//draws character c of the GLCD font at x,y and returns how far the text moves on
	size_t drawChar(uint16_t c, int32_t x, int32_t y);

	//a transaction around a group of drawing calls, only the display needs one
	virtual void startWrite(void) {}
	virtual void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//the display itself: the calls with game coordinates reach the 128x128 in its middle
class PlatformTuftyDisplay : public PlatformTuftyGFX
{
public:
	void init(void);

	void startWrite(void) override;
	void endWrite(void) override;
	//the area the pixels written next fill, left to right and top to bottom
	void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h);
	//swap true: the values are plain RGB565 and are put in display order here. False: they
	//already are in display order
	void writePixels(const uint16_t* data, int32_t length, bool swap = true);
	//length pixels of one RGB565 colour
	void writeColor(uint16_t color, uint32_t length);
	//bytes as they are, for pixels already in display order
	void writeBytes(const uint8_t* data, uint32_t length);
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;

private:
	uint8_t writeDepth = 0;
};

//An off screen buffer of the game's size, 1, 8 or 16 bits per pixel, laid out the way LovyanGFX's
//sprites keep them: byte swapped RGB565, RGB332, or 1 bpp packed most significant bit first
class PlatformTuftyBuffer : public PlatformTuftyGFX
{
public:
	void setColorDepth(uint8_t bits) { depth = bits; }
	bool createSprite(int32_t w, int32_t h);
	void* getBuffer(void) { return pixels; }
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;

private:
	uint8_t* pixels = nullptr;
	uint8_t depth = 16;
};

typedef PlatformTuftyDisplay PlatformDisplay;
typedef PlatformTuftyBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//flash is mapped into memory on the RP2350, it can be read like any other
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the RP2350 itself, memcpy keeps a read from an odd
//address inside a byte array well defined
static inline uint16_t PlatformTufty_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformTufty_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
