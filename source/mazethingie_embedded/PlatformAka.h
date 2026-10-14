#ifndef PLATFORM_AKA_H
#define PLATFORM_AKA_H

//The Gamebuino AKA part of Platform.h, included by it. See "The device" in Platform.h for
//what this supplies, the functions are in PlatformAka.cpp.
//
//The AKA is an ESP32-S3 at 240 MHz with octal PSRAM, 8 MB of flash and a 320x240 ST7789V.
//Everything goes through the AKA library (components/gamebuino): gb_core for the buttons and
//the joystick, gb_graphics for the drawing and gb_audio_player for the sound. Nothing here
//touches the library's framebuffer or the panel itself, so a change inside the library does
//not reach this port.
//
//The game's 128x128 frame is scaled up to 240x240 in the middle of the display, as on the
//Tufty: 240 / 128 is not a whole number, so a game pixel is 1 or 2 display pixels wide.
//SCALESCREEN 0 shows it 1:1 in the middle instead.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

//the display class offers LovyanGFX's calls, so the game takes its LovyanGFX drawing paths
#define LOVYANGFX 1

//Platform_Exit goes back to the launcher (the App Store's, in the second app partition), as
//Jicehel's games do. RUN and MENU held together call it, see Platform_GetButtons
#define PLATFORM_HAS_EXIT 1

//Where drawing goes, the modes are described in PlatformESPboy.h. Only a buffered frame can be
//scaled, and with PSRAM there is room to spare: a 16 bpp buffer of the game's size is 32 KB.
//A build can still set it itself
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif
#if (SCREENBUFFER != 0) && (SCREENBUFFER != 1) && (SCREENBUFFER != 8) && (SCREENBUFFER != 16)
#error "the Gamebuino AKA has the RAM for SCREENBUFFER 0, 1, 8 or 16"
#endif

//1 scales the frame to 240x240 in the middle of the display, 0 shows it 1:1 in the middle.
//Only a frame from a screen buffer can be scaled, drawn straight to the display
//(SCREENBUFFER 0) the game is always shown 1:1. A build can still set it itself
#ifndef SCALESCREEN
#define SCALESCREEN 1
#endif

//backlight brightness in percent, 0 (off) to 100. A build can still set it itself
#ifndef BACKLIGHT
#define BACKLIGHT 80
#endif

//What the game draws with, shared by the display and the screen buffer: rectangles and the
//text of the GLCD font, with the arguments LovyanGFX takes
class PlatformAkaGFX
{
public:
	virtual ~PlatformAkaGFX() {}

	//RGB565 from 8 bit channels, as every other device makes it. The AKA's panel does not
	//take this order, PlatformAka.cpp converts where it hands a colour to gb_graphics
	static uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
	{
		return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	}

	virtual void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) = 0;
	void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);

	//a background the same as the text colour is not painted, as in LovyanGFX
	void setTextColor(uint16_t fg, uint16_t bg) { textColor = fg; textBackground = bg; }
	void setTextSize(uint8_t size) { textSize = size ? size : 1; }
	virtual size_t drawChar(uint16_t c, int32_t x, int32_t y);

	//a transaction around a group of drawing calls, gb_graphics needs none
	virtual void startWrite(void) {}
	virtual void endWrite(void) {}

protected:
	uint16_t textColor = 0xFFFF;
	uint16_t textBackground = 0x0000;
	uint8_t textSize = 1;
};

//the display itself: drawing goes through gb_graphics, the game's screen is the
//WINDOW_WIDTH x WINDOW_HEIGHT in the middle of the 320x240
class PlatformAkaDisplay : public PlatformAkaGFX
{
public:
	void init(void);

	//the area the pixels written next fill, left to right and top to bottom
	void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h);
	//swap true: the values are plain RGB565. False: they are already byte swapped
	void writePixels(const uint16_t* data, int32_t length, bool swap = true);
	//length pixels of one RGB565 colour
	void writeColor(uint16_t color, uint32_t length);
	//bytes as they are, two per pixel, high byte first
	void writeBytes(const uint8_t* data, uint32_t length);
	void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override;

private:
	//where the pixels written next go, and how far along that run is
	int32_t windowX = 0, windowY = 0, windowW = 0, windowH = 0;
	int32_t windowPos = 0;
	void putNext(uint16_t color);
};

//An off screen buffer of the game's size, laid out the way LovyanGFX's sprites keep them:
//16 bpp byte swapped, RGB332, or 1 bpp packed most significant bit first
class PlatformAkaBuffer : public PlatformAkaGFX
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

typedef PlatformAkaDisplay PlatformDisplay;
typedef PlatformAkaBuffer PlatformBuffer;
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//flash is memory mapped on the ESP32-S3, const data can be read through a pointer of its own
//type. The images are little endian RGB565 like the chip itself
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

static inline uint16_t PlatformAka_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformAka_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
