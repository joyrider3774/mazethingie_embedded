#ifndef PLATFORM_SDL_H
#define PLATFORM_SDL_H

//The Windows part of Platform.h, included by it. LovyanGFX draws into an SDL window, the
//functions are in PlatformSDL.cpp. See "The device" in Platform.h for what this supplies.

#include <string.h>

//always LovyanGFX here, its SDL panel is what makes the window
#define LOVYANGFX 1

//where drawing goes, the modes are described in PlatformESPboy.h: a PC has the memory for
//a full colour buffer. CMakeLists.txt passes its own SCREENBUFFER, this is for a build that
//does not
#ifndef SCREENBUFFER
#define SCREENBUFFER 16
#endif

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
typedef lgfx::LGFX_Device PlatformDisplay;
typedef LGFX_Sprite PlatformBuffer;
//start of the sprite's pixels: 16 bpp byte swapped RGB565, 8 bpp RGB332, 1 bpp packed most
//significant bit first, the same as on the ESPboy
#define SCREENBUFFER_PIXELS() screenBuffer.getBuffer()

//flash data is ordinary memory on a PC
#define PLATFORM_PROGMEM
#define PLATFORM_READ_BYTE(addr) (*(const uint8_t*)(addr))

//the images are little endian RGB565 like the PC itself, memcpy keeps a read from an odd
//address inside a byte array well defined
static inline uint16_t PlatformSDL_ReadWord(const void* addr)
{
	uint16_t value;
	memcpy(&value, addr, sizeof(value));
	return value;
}
#define PLATFORM_READ_WORD(addr) PlatformSDL_ReadWord(addr)
#define PLATFORM_READ_BYTES(dst, addr, len) memcpy((dst), (addr), (len))

#endif
