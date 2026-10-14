#ifndef PLATFORM_N64_H
#define PLATFORM_N64_H

//The Nintendo 64 part of Platform.h, included by it. See "The device" in Platform.h for what this
//supplies, the functions are in PlatformN64.cpp.
//
//The N64 is a 93.75 MHz VR4300 with 4 MB of memory and a display of 320x240 pixels, built with
//libdragon (see n64/CMakeLists.txt). The display class is a small one of its own below, as on the
//GBA and the PlayStation: it offers the LovyanGFX calls the game makes, the same way LovyanGFX
//takes them, so the game's LOVYANGFX drawing paths work unchanged.
//
//The game draws into a frame that is kept in the console's own 16 bit pixels, and the RDP puts that
//frame on screen scaled to 240x240 in the middle, or 1:1 with SCALESCREEN 0.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. 0 is the default here: the game
//draws into a frame that is already in the console's colours, so only what changes is touched and
//the RDP takes it from there. A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 0
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16 on the N64"
#endif

//1 = the frame is scaled to 240x240, as high as the screen, in its middle. 0 = it is shown 1:1 in
//the middle. A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//how loud tones play, 0 (silent) to 100. A build can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 60
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the text of
//the GLCD font, with the arguments LovyanGFX takes
class PlatformN64GFX
{
public:
	virtual ~PlatformN64GFX() {}

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

	//a transaction around a group of drawing calls, nothing to do on the N64
	void startWrite(void) {}
	void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//The screen itself. Without a screen buffer the game draws through these calls, into the frame the
//RDP puts on screen at the end of every frame. With one, everything goes into the buffer instead
//and only color565 is used here
class PlatformN64Display : public PlatformN64GFX
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
class PlatformN64Buffer : public PlatformN64GFX
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

typedef PlatformN64Display PlatformDisplay;
typedef PlatformN64Buffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//data in flash is part of the cartridge, which the console reads into memory
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//A 16 bit value the compiler put in flash, like the values of a tune: it lies there the way this
//machine reads it, which is big endian. memcpy keeps a read of an address a 16 bit load can not
//take well defined
static inline uint16_t PlatformN64_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}

//A pixel of an image, which is a byte array holding little endian RGB565 whatever the machine
//reading it is, so its two bytes are put together by hand. This also keeps a read of an odd
//address well defined, which the pixels that follow the control byte of an encoded row sit on
static inline uint16_t PlatformN64_ReadPixel(const void* addr)
{
	const uint8_t* p = (const uint8_t*)addr;
	return (uint16_t)(p[0] | (p[1] << 8));
}

//Everything copied out of flash this way is pixels: whole rows of images, which the game reads
//back as 16 bit values. The two bytes of every pixel change places on the way, so what lands in
//memory is RGB565 the way this machine reads it
static inline void PlatformN64_ReadBytes(void* dst, const void* src, size_t length)
{
	uint8_t* d = (uint8_t*)dst;
	const uint8_t* s = (const uint8_t*)src;
	for (size_t pixels = length >> 1; pixels; pixels--, d += 2, s += 2)
	{
		d[0] = s[1];
		d[1] = s[0];
	}
	//pixel data is never an odd number of bytes, this is here so nothing is lost if it ever is
	if (length & 1)
		*d = *s;
}

#define PLATFORM_READ_WORD(addr) PlatformN64_ReadWord(addr)
#define PLATFORM_READ_PIXEL(addr) PlatformN64_ReadPixel(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) PlatformN64_ReadBytes((dst), (addr), (len))

//flash is plain memory here, but this machine reads a 16 bit value the other way around than the
//images hold it, so the pixels go through the reads above instead of through a pointer of their own
#define PLATFORM_DIRECT_FLASH 0

#endif
