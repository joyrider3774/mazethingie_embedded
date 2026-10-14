#ifndef PLATFORM_H
#define PLATFORM_H

//Everything the game needs from the device goes through here: the display and where
//drawing goes, the buttons, sound, saving and memory figures. This header only uses
//standard C headers. What can not be written portably comes from the device's own
//header, picked below, and the functions are implemented by the device's own source:
//PlatformESPboy.h and PlatformESPboy.cpp for the ESPboy.

#include <stdint.h>
#include <stddef.h>
#include "Defines.h"

// ===========================================================================
// Settings
// ===========================================================================

//SCREENBUFFER, where drawing goes (0, 1, 8 or 16), is set by the device header that
//PlatformDevice.h includes: how much buffer fits depends on the device. PlatformESPboy.h
//describes the modes. A build can also set it itself

//bytes of save storage the game uses, every save and load goes through one block this size
#define PLATFORM_STORAGE_SIZE 32

// ===========================================================================
// The device
//
// PlatformDevice.h picks the device and includes its header, which supplies:
//   PlatformDisplay, PlatformBuffer   the display and screen buffer classes SCREEN and GFX name
//   SCREENBUFFER_PIXELS()             start of the screen buffer's pixels, in the layout
//                                     SetBufferPixel writes
//   PLATFORM_PROGMEM                  marks const data (images, level packs) kept in flash
//   PLATFORM_READ_BYTE(addr)          reads one byte of such data
//   PLATFORM_READ_WORD(addr)          reads one 16 bit value of such data
//   PLATFORM_READ_BYTES(dst, addr, n) copies n bytes of such data into memory
// ===========================================================================

#include "PlatformDevice.h"

#ifndef SCREENBUFFER
#error "the device header has to define SCREENBUFFER"
#endif

//Marks a function worth keeping in the fastest memory the device has. Only the Game Boy Advance
//has any use for it: its program is read from the cartridge with wait states, so a loop that runs
//once for every pixel is several times slower from there than from the internal work RAM. The
//space is small, so only the few calls that touch every pixel are marked. A device header sets
//this to whatever its toolchain wants, everywhere else it is nothing
#ifndef PLATFORM_FAST_CODE
#define PLATFORM_FAST_CODE
#endif

//The same, for the handful of loops that run once per pixel rather than once per call. It is kept
//apart from PLATFORM_FAST_CODE because a device may have room for these and not for the whole of
//the drawing: the CHGame has 20k of ram in all and moves only these into it
#ifndef PLATFORM_HOT_CODE
#define PLATFORM_HOT_CODE
#endif

//Temporary, and only the CHGame reads it: with -DCHGAME_TIMING=1 it says over that device's USB
//serial where a frame's time goes, so the part worth working on is known rather than guessed, see
//Platform_PresentFrame in PlatformCHGame.cpp. Build it with -DFPSLOCK=0 as well, or the frame lock
//hides the answer by waiting out whatever is left of the frame. It is defaulted here rather than in
//the device file because the counters are kept by the drawing, which every device compiles
#ifndef CHGAME_TIMING
#define CHGAME_TIMING 0
#endif

//1 when data in flash is plain memory that can be read through a pointer of its own type.
//Only the ESP8266 has to go through PLATFORM_READ_xxx, and where a few pixels are copied at
//a time its memcpy costs more than reading them one by one. A device header sets this to 0
#ifndef PLATFORM_DIRECT_FLASH
#define PLATFORM_DIRECT_FLASH 1
#endif

//Reads one pixel of an image. Images are byte arrays holding little endian RGB565, which on every
//little endian device is the same as reading any other 16 bit value out of flash. A device that
//reads 16 bit values the other way around (the Nintendo 64) defines this itself: there the values
//the compiler put in flash, like the ones of a tune, and the bytes of an image are not the same
//thing
#ifndef PLATFORM_READ_PIXEL
#define PLATFORM_READ_PIXEL(addr) PLATFORM_READ_WORD(addr)
#endif

// ===========================================================================
// What the game supplies
// ===========================================================================

//Game_Setup runs once when the device has started, Game_Loop then over and over for as
//long as it runs. The device's source calls them from wherever its program starts
void Game_Setup(void);
void Game_Loop(void);

// ===========================================================================
// Display
// ===========================================================================

//SCREEN is always the display itself. Only what has to reach the display whatever
//SCREENBUFFER is set to uses it: the unbuffered drawing paths and color565.
extern PlatformDisplay& platformDisplay;
#define SCREEN platformDisplay

//Everything that draws goes through GFX, which is either the display itself or the
//off screen buffer SCREENBUFFER picks. This has to be a macro and not a TFT_eSPI
//reference: pushImage is not virtual, so through a reference to the base class it
//would draw straight to the display even when the reference holds the buffer.
#if SCREENBUFFER
extern PlatformBuffer screenBuffer;
#define GFX screenBuffer
#else
#define GFX SCREEN
#endif

#if DITHERING && ((SCREENBUFFER == 1) || (SCREENBUFFER == 8))
//Where in the spread of a colour the pixel at x,y falls, 0 to 15: the 4x4 Bayer pattern. x and y
//are where the pixel lands on the screen, so the pattern stands still and a picture drawn twice in
//the same place comes out the same both times
static inline uint8_t DitherSpread(int16_t x, int16_t y)
{
	static const uint8_t pattern[4][4] = {
		{  0,  8,  2, 10 },
		{ 12,  4, 14,  6 },
		{  3, 11,  1,  9 },
		{ 15,  7, 13,  5 }
	};
	return pattern[y & 3][x & 3];
}
#endif

#if SCREENBUFFER == 8
//The RGB332 an 8 bpp buffer keeps a colour as. Red keeps 3 of its 5 bits, green 3 of its 6 and
//blue 2 of its 5, and without DITHERING what does not fit is simply dropped: the same conversion
//both libraries apply to everything else.
//
//With DITHERING the colour is first given a share of the pattern, as much as one step of what that
//channel is about to lose, so a shade that falls between two of the colours RGB332 has comes out as
//the two of them in a pattern rather than as the nearer one, and what would be a band across a sky
//becomes a texture. See DitherSpread for where x and y come into it.
//
//A colour RGB332 holds exactly is not moved by this: the share added is always less than the step
//it would take to reach the next one. Black and white therefore come out of this the same as they
//go in, and meet a dithered image without a seam
static inline uint8_t ToBuffer332(uint16_t color, int16_t x, int16_t y)
{
  #if DITHERING
	const uint8_t spread = DitherSpread(x, y);
	//red loses 2 bits so its step is 4 and it is given 0 to 3, green and blue lose 3 so theirs is
	//8 and they are given 0 to 7. The brightest colours would carry past what the channel holds
	uint16_t r = (uint16_t)((color >> 11) & 0x1F) + (spread >> 2);
	uint16_t g = (uint16_t)((color >> 5) & 0x3F) + (spread >> 1);
	uint16_t b = (uint16_t)(color & 0x1F) + (spread >> 1);
	if (r > 0x1F) r = 0x1F;
	if (g > 0x3F) g = 0x3F;
	if (b > 0x1F) b = 0x1F;
	return (uint8_t)(((r & 0x1C) << 3) | ((g & 0x38) >> 1) | (b >> 3));
  #else
	(void)x;
	(void)y;
	return (uint8_t)(((color & 0xE000) >> 8) | ((color & 0x0700) >> 6) | ((color & 0x0018) >> 3));
  #endif
}
#endif

#if SCREENBUFFER == 1
//A 1 bpp buffer only knows set and clear, the frame shows them in the colours given to
//Platform_SetBufferColors. The maze is white on black, a pixel is set when it is
//brighter than mid grey
static inline void SetBufferBit(uint8_t* dst, int16_t x, int16_t y, uint16_t color)
{
	//images repeat the same few colours, so the answer for the last colour is kept instead
	//of working out the brightness again for every pixel
	static uint16_t lastColor = 0x0000;
	static uint16_t lastLum = 0;
	if (color != lastColor)
	{
		//luminance on a 0-255 scale from the 5, 6 and 5 bit channels
		lastLum = ((((color >> 11) & 0x1F) << 3) * 77 + (((color >> 5) & 0x3F) << 2) * 150 + ((color & 0x1F) << 3) * 29) >> 8;
		lastColor = color;
	}
  #if DITHERING
	//the pattern over the same 0-255 scale, 8 to 248: black is under all of it and white over all
	//of it, so only the shades in between become a pattern of the two
	const bool set = (lastLum > (uint16_t)(DitherSpread(x, y) * 16u + 8u));
  #else
	const bool set = (lastLum >= 128);
  #endif
	//bits are packed most significant first, the same layout TFT_eSprite::drawPixel uses
	uint8_t mask = 0x80 >> (x & 7);
	if (set)
		dst[(x + y * WINDOW_WIDTH) >> 3] |= mask;
	else
		dst[(x + y * WINDOW_WIDTH) >> 3] &= ~mask;
}
#endif

#if SCREENBUFFER
//writes one RGB565 pixel into the buffer in whatever form the buffer keeps it, x and y
//have to be on screen already
static inline void SetBufferPixel(void* dst, int16_t x, int16_t y, uint16_t color)
{
  #if SCREENBUFFER == 16
	//a 16 bpp sprite keeps its pixels byte swapped
	((uint16_t*)dst)[y * WINDOW_WIDTH + x] = (uint16_t)((color >> 8) | (color << 8));
  #elif SCREENBUFFER == 8
	((uint8_t*)dst)[y * WINDOW_WIDTH + x] = ToBuffer332(color, x, y);
  #else
	SetBufferBit((uint8_t*)dst, x, y, color);
  #endif
}
#endif

//starts the device, shows its boot screen and creates the screen buffer
void Platform_Init(const char* appName);

//The colours a 1 bpp buffer shows its set and clear bits in. Does nothing with any other
//SCREENBUFFER setting
void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor);

//sends the finished frame to the display, without a screen buffer there is nothing to send
void Platform_PresentFrame(void);

// ===========================================================================
// Buttons
// ===========================================================================

#define BUTTON_LEFT   0x01
#define BUTTON_UP     0x02
#define BUTTON_DOWN   0x04
#define BUTTON_RIGHT  0x08
#define BUTTON_A      0x10
#define BUTTON_B      0x20
//the two side buttons
#define BUTTON_L      0x40
#define BUTTON_R      0x80

//the BUTTON_ bits of every button held down right now
uint8_t Platform_GetButtons(void);

//Leaves the game for the device's own game menu or launcher, where the platform has one (the
//CHGame's SD game menu). A platform that has it defines PLATFORM_HAS_EXIT in its header and
//writes the function; everywhere else this does nothing and the game carries on
#ifdef PLATFORM_HAS_EXIT
void Platform_Exit(void);
#else
static inline void Platform_Exit(void) { }
#endif

// ===========================================================================
// Time, sound and memory
// ===========================================================================

//microseconds since the device started, wraps around after about 71 minutes
uint32_t Platform_Micros(void);

//plays freq Hz for duration ms, a duration of 0 keeps it playing until the next tone
void Platform_PlayTone(uint16_t freq, uint16_t duration);

//free heap in bytes
uint32_t Platform_FreeHeap(void);

//the least stack that has been free since boot, in bytes
uint32_t Platform_FreeStack(void);

//a value to seed srand with that differs from one start to the next
uint32_t Platform_RandomSeed(void);

//printf style message for whoever is debugging, it does not show on the display
#ifdef __GNUC__
void Platform_Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
#else
void Platform_Log(const char* format, ...);
#endif

// ===========================================================================
// Saved data
// ===========================================================================

//Reads or writes length bytes at offset in the save storage, offset + length has to stay
//within PLATFORM_STORAGE_SIZE. A write of bytes that are already stored costs nothing
void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length);
void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length);

#endif
