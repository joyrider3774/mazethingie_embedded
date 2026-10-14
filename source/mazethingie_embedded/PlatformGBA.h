#ifndef PLATFORM_GBA_H
#define PLATFORM_GBA_H

//The Game Boy Advance part of Platform.h, included by it. See "The device" in Platform.h for what
//this supplies, the functions are in PlatformGBA.cpp.
//
//The GBA is an ARM7TDMI at 16.8 MHz with 32 KB of fast RAM (IWRAM, where the globals live), 256 KB of
//work RAM (EWRAM, where the heap and the big buffers go) and a 240x160 display of 15 bit pixels, built
//with devkitARM and libgba, see gba/CMakeLists.txt. The display class is a small one of its own below,
//as on the PicoSystem: it offers the LovyanGFX calls the game makes, the same way LovyanGFX takes them,
//so the game's LOVYANGFX drawing paths work unchanged.
//
//Everything is drawn into a screen buffer. Its changed rows are written to a 160x128 bitmap (mode 5)
//at the start of every vertical blank, and the GBA's own background scaling shows that bitmap at
//160x160 in the middle of the display, or 1:1 with SCALESCREEN 0.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. 8 is the default here: its 16 KB
//buffer fits in the fast IWRAM, which is what keeps the game at its frame rate (Znax: 30 frames a
//second against 20 with a 16 bpp buffer, which is 32 KB and only fits in the slower EWRAM). 8 bpp
//keeps the colours as RGB332, as on the ESPboy. 0 is the default here: the game draws straight into
//the display's pixels in full colour, which is the least work per pixel and needs no buffer memory at
//all. It draws into the page that is not on the display and that page is shown when the frame is
//done, so nothing is ever seen half drawn. A buffer costs a second copy of every pixel and a
//comparison of the whole frame, which on this 16.8 MHz machine is what keeps games from their frame
//rate (Blockdude: 30 frames a second without a buffer against 3.5 with one in the work RAM).
//A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 0
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 0, 1, 8 or 16 on the GBA"
#endif

//The program is read from the cartridge, which the processor waits on, while the internal work RAM
//it is copied into at the start answers at once. The loops that run once for every pixel are worth
//the room there, the rest is not: there are 32 KB of it and the variables live in it too. This is
//what libgba's IWRAM_CODE is, written out so that this header does not need libgba's own
#define PLATFORM_FAST_CODE __attribute__((section(".iwram"), long_call))

//1 = the screen buffer is in IWRAM, the GBA's fast memory, 0 = in EWRAM. Everything the game draws
//goes through the buffer, so IWRAM is a great deal faster, but IWRAM is only 32 KB and the game's own
//globals live there as well: a game with big globals has no room for it and the build says so
//("region `iwram' overflowed"). A 16 bpp buffer never fits. A build can still set it itself
#ifndef BUFFERINIWRAM
#define BUFFERINIWRAM (SCREENBUFFER != 16)
#endif

//1 = the frame is scaled up to 160x160 by the GBA's background scaling, as high as the display, in
//its middle. 0 = it is shown 1:1 in the middle. A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//how loud tones play, 0 (silent) to 100. A build can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 60
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the
//text of the GLCD font, with the arguments LovyanGFX takes
class PlatformGBAGFX
{
public:
	virtual ~PlatformGBAGFX() {}

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

	//a transaction around a group of drawing calls, nothing to do on the GBA
	void startWrite(void) {}
	void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//The display itself. With a screen buffer the game only uses it for color565 and everything is drawn
//into the buffer, without one (SCREENBUFFER 0) the game draws through these calls, straight into the
//display's pixels
class PlatformGBADisplay : public PlatformGBAGFX
{
public:
	PLATFORM_FAST_CODE void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;
	//the area the pixels written next fill, left to right and top to bottom
	void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h);
	//swap true: the values are plain RGB565. False: they are byte swapped
	PLATFORM_FAST_CODE void writePixels(const uint16_t* data, int32_t length, bool swap = true);
	//length pixels of one RGB565 colour
	PLATFORM_FAST_CODE void writeColor(uint16_t color, uint32_t length);
};

//An off screen buffer of the game's size, 1, 8 or 16 bits per pixel, laid out the way LovyanGFX's
//sprites keep them: byte swapped RGB565, RGB332, or 1 bpp packed most significant bit first
class PlatformGBABuffer : public PlatformGBAGFX
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

typedef PlatformGBADisplay PlatformDisplay;
typedef PlatformGBABuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//data in flash is the cartridge ROM, mapped into memory
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the GBA's ARM7, memcpy keeps a read from an odd address
//inside a byte array well defined
static inline uint16_t PlatformGBA_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformGBA_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
