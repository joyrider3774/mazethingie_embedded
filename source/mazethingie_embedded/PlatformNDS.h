#ifndef PLATFORM_NDS_H
#define PLATFORM_NDS_H

//The Nintendo DS part of Platform.h, included by it. See "The device" in Platform.h for what this
//supplies, the functions are in PlatformNDS.cpp.
//
//The DS runs the game on its ARM9, a 67 MHz ARM946E-S with 4 MB of main RAM and 656 KB of video RAM,
//and has two 256x192 displays of 15 bit pixels. It is built with devkitARM, libnds and calico, see
//nds/CMakeLists.txt. The display class is a small one of its own below, as on the GBA: it offers the
//LovyanGFX calls the game makes, the same way LovyanGFX takes them, so the game's LOVYANGFX drawing
//paths work unchanged.
//
//The game's 128x128 screen is a bitmap background in video RAM, of which there are two: one is shown
//while the other is drawn, so a frame is never seen half drawn. The DS scales that background to
//192x192 in the middle of the top display, or shows it 1:1 with SCALESCREEN 0. The bottom display
//stays dark, the game has nothing to put on it.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. 0 is the default: the game draws
//straight into the bitmap that is not on the display, which is the least work per pixel and costs no
//memory of its own. The DS has the RAM for any of the others, but they all mean a second copy of
//every pixel. A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 0
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16 on the DS"
#endif

//1 = the frame is scaled to 192x192 by the DS's background scaling, as high as the display, in its
//middle. 0 = it is shown 1:1 in the middle. A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//how loud tones play, 0 (silent) to 100. A build can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 60
#endif

//1 = tones are played as a square wave sample, 0 = on one of the DS's own tone channels. Both are
//square waves, but a tone channel counts its frequency in a 16 bit timer and can not go below about
//256 Hz, so the low notes of the explosion come out at the wrong pitch. A build can still set it
//itself
#ifndef TONEWAVE
#define TONEWAVE 1
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the text of
//the GLCD font, with the arguments LovyanGFX takes
class PlatformNDSGFX
{
public:
	virtual ~PlatformNDSGFX() {}

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

	//a transaction around a group of drawing calls, nothing to do on the DS
	void startWrite(void) {}
	void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//The display itself. With a screen buffer the game only uses it for color565 and everything is drawn
//into the buffer, without one (SCREENBUFFER 0) the game draws through these calls, straight into the
//bitmap that is not on the display
class PlatformNDSDisplay : public PlatformNDSGFX
{
public:
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;
	//the area the pixels written next fill, left to right and top to bottom
	void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h);
	//swap true: the values are plain RGB565. False: they are byte swapped
	void writePixels(const uint16_t* data, int32_t length, bool swap = true);
	//length pixels of one RGB565 colour
	void writeColor(uint16_t color, uint32_t length);
};

//An off screen buffer of the game's size, 1, 8 or 16 bits per pixel, laid out the way LovyanGFX's
//sprites keep them: byte swapped RGB565, RGB332, or 1 bpp packed most significant bit first
class PlatformNDSBuffer : public PlatformNDSGFX
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

typedef PlatformNDSDisplay PlatformDisplay;
typedef PlatformNDSBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//data in flash is the card's ROM, read into memory by the loader
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the ARM9, memcpy keeps a read from an odd address inside a
//byte array well defined
static inline uint16_t PlatformNDS_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformNDS_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
