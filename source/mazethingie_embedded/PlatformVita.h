#ifndef PLATFORM_VITA_H
#define PLATFORM_VITA_H

//The PlayStation Vita part of Platform.h, included by it. See "The device" in Platform.h for what
//this supplies, the functions are in PlatformVita.cpp.
//
//The Vita is a quad core ARM Cortex-A9 at 444 MHz with 512 MB of RAM and a 960x544 display, built
//with VitaSDK, see vita/CMakeLists.txt. The display class is a small one of its own below, as on the
//PSP: it offers the LovyanGFX calls the game makes, the same way LovyanGFX takes them, so the game's
//LOVYANGFX drawing paths work unchanged.
//
//Everything is drawn into a screen buffer, which is copied to the display once a frame: four times
//the size, 512x512 in the middle of the display, or 1:1 in the middle with SCALESCREEN 0. Four is the
//most a 128x128 frame can be blown up on a 544 line display without a pixel bleeding into its
//neighbour, so it is what the frame is scaled by.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. There is RAM enough for anything
//here, 16 keeps every colour the game has. A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16"
#endif

//1 = the frame is blown up to 512x512 in the middle of the display, 0 = it is shown 1:1 in the
//middle. A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//how loud tones play, 0 (silent) to 100. A build can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 60
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the
//text of the GLCD font, with the arguments LovyanGFX takes
class PlatformVitaGFX
{
public:
	virtual ~PlatformVitaGFX() {}

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

	//a transaction around a group of drawing calls, nothing to do here
	void startWrite(void) {}
	void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//The display itself. With a screen buffer the game only uses it for color565 and everything is drawn
//into the buffer, without one (SCREENBUFFER 0) the game draws through these calls, straight into the
//frame that is sent to the display
class PlatformVitaDisplay : public PlatformVitaGFX
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
class PlatformVitaBuffer : public PlatformVitaGFX
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

typedef PlatformVitaDisplay PlatformDisplay;
typedef PlatformVitaBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//the images are part of the program, in memory like anything else
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the Vita's ARM, memcpy keeps a read from an odd address
//inside a byte array well defined
static inline uint16_t PlatformVita_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformVita_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
