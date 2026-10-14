#ifndef PLATFORM_3DS_H
#define PLATFORM_3DS_H

//The Nintendo 3DS part of Platform.h, included by it. See "The device" in Platform.h for what this
//supplies, the functions are in Platform3DS.cpp.
//
//The 3DS runs the game on an ARM11 at 268 MHz with 128 MB of memory, and has a 400x240 top screen
//and a 320x240 one below it. It is built with devkitARM and libctru, see 3ds/CMakeLists.txt. The
//display class is a small one of its own below, as on the GBA and the DS: it offers the LovyanGFX
//calls the game makes, the same way LovyanGFX takes them, so the game's LOVYANGFX drawing paths
//work unchanged.
//
//The game is drawn into a screen buffer and that buffer is written to the top screen when the frame
//is done, scaled to 240x240 in its middle, or 1:1 with SCALESCREEN 0. The bottom screen stays dark,
//the game has nothing to put on it.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. 16 is the default here: the 3DS
//has memory to spare and its own screen is written as a whole anyway, so the frame is composed in
//full colour and sent in one go. A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif
#if (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 1, 8 or 16 on the 3DS, the game is never drawn straight to the screen"
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
class Platform3DSGFX
{
public:
	virtual ~Platform3DSGFX() {}

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

	//a transaction around a group of drawing calls, nothing to do on the 3DS
	void startWrite(void) {}
	void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//The screens themselves. Everything is drawn into the buffer, so the game only uses this for
//color565, but the unbuffered drawing paths of the game still ask for these calls
class Platform3DSDisplay : public Platform3DSGFX
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
class Platform3DSBuffer : public Platform3DSGFX
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

typedef Platform3DSDisplay PlatformDisplay;
typedef Platform3DSBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//data in flash is part of the program, which the loader reads into memory
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the ARM11, memcpy keeps a read from an odd address inside
//a byte array well defined
static inline uint16_t Platform3DS_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) Platform3DS_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
