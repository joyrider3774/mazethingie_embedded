#ifndef PLATFORM_LIBRETRO_H
#define PLATFORM_LIBRETRO_H

//The libretro part of Platform.h, included by it. See "The device" in Platform.h for what this
//supplies, the functions are in PlatformLibretro.cpp.
//
//The game as a libretro core, a shared library RetroArch and the other libretro frontends load and
//run on every system they run on. Built with libretro/CMakeLists.txt. The frontend calls the core
//once per frame and does the rest: scaling the 128x128 picture, input devices, sound output and
//writing the save file. The display class is a small one of its own below, as on the PicoSystem: it
//offers the LovyanGFX calls the game makes, the same way LovyanGFX takes them, so the game's
//LOVYANGFX drawing paths work unchanged.
//
//Everything is drawn into a screen buffer, which is handed to the frontend as RGB565 pixels at the
//end of every frame.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. 16 is full colour, 8 the colours
//LovyanGFX's 8 bpp sprites keep, 1 the black & white skin. There is no drawing straight to the
//display here, 0 is not possible. A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif
#if (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 1, 8 or 16 in the libretro core"
#endif

//The frontend runs the core once per frame at the game's frame rate: no need for the game to hold
//frames back itself as well. A build can still set it itself
#ifndef FPSLOCK
#define FPSLOCK 0
#endif

//how loud tones play, 0 (silent) to 100. A build can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 30
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the
//text of the GLCD font, with the arguments LovyanGFX takes
class PlatformLibretroGFX
{
public:
	virtual ~PlatformLibretroGFX() {}

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

	//a transaction around a group of drawing calls, nothing to do in a core
	void startWrite(void) {}
	void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//The display itself. Everything is drawn into the screen buffer, the game only uses the display
//for color565
class PlatformLibretroDisplay : public PlatformLibretroGFX
{
public:
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;
};

//An off screen buffer of the game's size, 1, 8 or 16 bits per pixel, laid out the way LovyanGFX's
//sprites keep them: byte swapped RGB565, RGB332, or 1 bpp packed most significant bit first
class PlatformLibretroBuffer : public PlatformLibretroGFX
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

typedef PlatformLibretroDisplay PlatformDisplay;
typedef PlatformLibretroBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//data in flash is ordinary memory on every system a frontend runs on
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//The images are little endian RGB565 and the game copies their rows straight into 16 bit pixels, so
//the core is for little endian systems (x86, ARM), which is every usual frontend. memcpy keeps a
//read from an odd address inside a byte array well defined
static inline uint16_t PlatformLibretro_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformLibretro_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
