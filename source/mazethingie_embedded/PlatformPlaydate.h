#ifndef PLATFORM_PLAYDATE_H
#define PLATFORM_PLAYDATE_H

//The Panic Playdate part of Platform.h, included by it. See "The device" in Platform.h for what this
//supplies, the functions are in PlatformPlaydate.cpp.
//
//The Playdate is a Cortex-M7 at 180 MHz with 16 MB of RAM and a 400x240 display of 1 bit pixels,
//built with Panic's Playdate C SDK: as a pdex.elf for the device and a pdex.dll for the simulator,
//see playdate/CMakeLists.txt. The display class is a small one of its own below, as on the
//PicoSystem: it offers the LovyanGFX calls the game makes, the same way LovyanGFX takes them, so the
//game's LOVYANGFX drawing paths work unchanged.
//
//Everything is drawn into a screen buffer, which is turned into the Playdate's 1 bit pixels at the
//end of every frame and scaled up to 240x240 in the middle of its display, or shown 1:1 in the
//middle with SCALESCREEN 0.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Where drawing goes, the modes are described in PlatformESPboy.h. 1 draws the black & white skin
//straight into 1 bit pixels, what the Playdate shows. 8 and 16 draw the colour skin and dither it
//down to 1 bit, which suits photos more than line art. There is no drawing straight to the display
//here, 0 is not possible. A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 1
#endif
#if (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "SCREENBUFFER has to be 1, 8 or 16 on the Playdate"
#endif

//1 = the frame is scaled up to 240x240, as high as the display, in its middle. 0 = it is shown 1:1
//in the middle. A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//The Playdate calls the game once per display refresh, and the refresh rate is set to the game's
//frame rate: no need for the game to hold frames back itself as well. A build can still set it
#ifndef FPSLOCK
#define FPSLOCK 0
#endif

//how loud tones play, 0 (silent) to 100. A build can still set it itself
#ifndef SOUNDVOLUME
#define SOUNDVOLUME 40
#endif

//The Playdate has no side buttons. The system menu has an entry for each, choosing it presses the
//button once, see Platform_GetButtons. Without a name there is no entry. A build can still set them
#ifndef PLAYDATE_MENU_L
#define PLAYDATE_MENU_L "restart"
#endif
#ifndef PLAYDATE_MENU_R
#define PLAYDATE_MENU_R "free view"
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the
//text of the GLCD font, with the arguments LovyanGFX takes
class PlatformPlaydateGFX
{
public:
	virtual ~PlatformPlaydateGFX() {}

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

	//a transaction around a group of drawing calls, nothing to do on the Playdate
	void startWrite(void) {}
	void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//The display itself. Everything is drawn into the screen buffer, the game only uses the display
//for color565
class PlatformPlaydateDisplay : public PlatformPlaydateGFX
{
public:
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;
};

//An off screen buffer of the game's size, 1, 8 or 16 bits per pixel, laid out the way LovyanGFX's
//sprites keep them: byte swapped RGB565, RGB332, or 1 bpp packed most significant bit first
class PlatformPlaydateBuffer : public PlatformPlaydateGFX
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

typedef PlatformPlaydateDisplay PlatformDisplay;
typedef PlatformPlaydateBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//data in flash is ordinary memory on the Playdate and in the simulator
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the Playdate's Cortex-M7 and the PC, memcpy keeps a read
//from an odd address inside a byte array well defined
static inline uint16_t PlatformPlaydate_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformPlaydate_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
