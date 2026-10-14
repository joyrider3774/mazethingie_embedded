#ifndef PLATFORM_THUMBY_H
#define PLATFORM_THUMBY_H

//The TinyCircuits Thumby Color part of Platform.h, included by it. See "The device" in Platform.h for
//what this supplies, the functions are in PlatformThumby.cpp.
//
//The Thumby Color is an RP2350A (two Cortex-M33 at 150 MHz) with 520 KB of RAM, 16 MB of flash and a
//0.85" 128x128 GC9107 display on SPI. It builds with the arduino-pico core as its Generic RP2350
//board with the RP2350A chip variant. LovyanGFX's RP2040 support does not build with that core's
//Pico SDK, so as on the PicoSystem the display class is a small one of its own below: it offers the
//LovyanGFX calls the game makes, the same way LovyanGFX takes them, so the game's LOVYANGFX drawing
//paths work unchanged.
//
//The display is 128x128, the size of the game's own screen, so the frame is always shown 1:1 over
//the whole display and there is nothing to scale or centre.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. There is RAM for a full colour
//16 bpp buffer (32 KB of the 520 KB). A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16"
#endif

//backlight brightness, 0 (off) to 255, with a gamma so the steps look even. A build can still set
//it itself
#ifndef BACKLIGHT
#define BACKLIGHT 200
#endif

//how loud the speaker plays, 0 (silent) to 100: the height of the tone's square wave in percent of
//the most the speaker pin can put out, the loudness follows it. The Thumby Color's speaker is a
//small one right under the thumbs, 3 keeps it at about the volume of the other handhelds. A build
//can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 3
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the
//text of the GLCD font, with the arguments LovyanGFX takes
class PlatformThumbyGFX
{
public:
	virtual ~PlatformThumbyGFX() {}

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

//the display itself, the same 128x128 as the game's screen
class PlatformThumbyDisplay : public PlatformThumbyGFX
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
class PlatformThumbyBuffer : public PlatformThumbyGFX
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

typedef PlatformThumbyDisplay PlatformDisplay;
typedef PlatformThumbyBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//flash is mapped into memory on the RP2350, it can be read like any other
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the RP2350 itself, memcpy keeps a read from an odd
//address inside a byte array well defined
static inline uint16_t PlatformThumby_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformThumby_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
