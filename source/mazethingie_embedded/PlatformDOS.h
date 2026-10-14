#ifndef PLATFORM_DOS_H
#define PLATFORM_DOS_H

//The MS-DOS part of Platform.h, included by it. See "The device" in Platform.h for what this
//supplies, the functions are in PlatformDOS.cpp.
//
//Built with DJGPP (see dos/CMakeLists.txt), which makes a 32 bit protected mode program, so the
//game's images are reached as plain memory instead of through the far pointers a 16 bit compiler
//would need. The screen is VGA mode X: 320x240 in 256 colours, which is the mode that has square
//pixels. Plain mode 13h is 320x200 and would show the game a fifth taller than it is wide.
//
//The display class is a small one of its own below, as on the consoles: it offers the LovyanGFX
//calls the game makes, so the game's LOVYANGFX drawing paths work unchanged.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
//the game prints its figures with PRIu32 and the like. Other toolchains bring these along with
//something else they include, DJGPP's does not, and this header is in the way of every file
#include <inttypes.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. 8 is the default: the screen is
//256 colours and the game's 8 bpp buffer is RGB332, which is exactly the palette this sets up, so a
//frame goes to the screen as it lies without a colour being worked out. A build can still set it
//itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 8
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16 on MS-DOS"
#endif

//1 = a shade the buffer has no colour for is a pattern of the two nearest ones it does have,
//0 = it becomes the nearer of them, which shows as bands. On here by default: the screen is
//256 colours and the 8 bpp buffer that feeds it is RGB332, which is coarse enough for the bands
//to show on anything that shades. A build can still set it itself, see DITHERING in defines.h
#ifndef DITHERING
#define DITHERING 1
#endif

//1 = the frame is blown up to 240x240, as high as the screen, in its middle. 0 = it is shown 1:1 in
//the middle. A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//how loud the tones are, 0 (silent) to 100. The PC speaker has one volume, so this only turns the
//sound on or off. A build can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 60
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the text of
//the GLCD font, with the arguments LovyanGFX takes
class PlatformDOSGFX
{
public:
	virtual ~PlatformDOSGFX() {}

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

//The screen itself. Without a screen buffer the game draws through these calls, into the frame that
//goes to the card at the end of every frame. With one, everything goes into the buffer instead and
//only color565 is used here
class PlatformDOSDisplay : public PlatformDOSGFX
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
class PlatformDOSBuffer : public PlatformDOSGFX
{
public:
	void setColorDepth(uint8_t bits) { depth = bits; }
	bool createSprite(int32_t w, int32_t h);
	void* getBuffer(void) { return pixels; }
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;

private:
	uint8_t* pixels = nullptr;
	uint8_t depth = 8;
};

typedef PlatformDOSDisplay PlatformDisplay;
typedef PlatformDOSBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//a 32 bit DOS program reaches everything as plain memory, flash data included
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the machine itself, memcpy keeps a read from an odd
//address inside a byte array well defined
static inline uint16_t PlatformDOS_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformDOS_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
