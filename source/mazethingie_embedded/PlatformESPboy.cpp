//Platform.h for the ESPboy. This is the only file that includes the ESPboy library, its
//sources are compiled here as part of this file because the sketch build only compiles
//the files in the sketch folder itself.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_ESPBOY

extern bool webAppStore;

#include <stdarg.h>
#include <stdlib.h>
#include <EEPROM.h>
#if LOVYANGFX
//the ESPboy's ST7735 panel for LovyanGFX: SPI pins, chip select and backlight on the I/O expander
#include "lib/LGFX_ESP8266_ESPboy.hpp"
#endif
#include "lib/ESPboyInit.h"
#include "lib/ESPboyInit.cpp"
#include "lib/ESPboyMCP.cpp"
#include "lib/ESPboyLED.cpp"

//the BUTTON_ bits are the ESPboy's own key bits, so getKeys can be passed on unchanged
static_assert((BUTTON_LEFT == PAD_LEFT) && (BUTTON_UP == PAD_UP) && (BUTTON_DOWN == PAD_DOWN) &&
			  (BUTTON_RIGHT == PAD_RIGHT) && (BUTTON_A == PAD_ACT) && (BUTTON_B == PAD_ESC) &&
			  (BUTTON_L == PAD_LFT) && (BUTTON_R == PAD_RGT), "BUTTON_ bits do not match the ESPboy keys");

static ESPboyInit myESPboy;
PlatformDisplay& platformDisplay = myESPboy.tft;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the display once at the end of loop()
PlatformBuffer screenBuffer(&myESPboy.tft);
#endif
#if (SCREENBUFFER == 1) && LOVYANGFX
//the two colours a 1 bpp frame is shown in, in display byte order
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if (SCREENBUFFER == 8) && LOVYANGFX
//Every RGB332 value as a 16 bit pixel in display byte order, see Platform_PresentFrame. It is
//allocated in Platform_Init and only when the game is what runs: the web app store has no frame to
//show and would rather have the 512 bytes
static uint16_t* bufferPalette = nullptr;
#endif

//the Arduino core starts the program here
void setup()
{
	Game_Setup();
}

void loop()
{
	Game_Loop();
}

void Platform_Init(const char* appName)
{
	//button press or so ?
	webAppStore = false;
	if(webAppStore)
	{
		myESPboy.begin("Web App Store");
		return;
	}
	myESPboy.begin(appName);
#if SCREENBUFFER
	//LovyanGFX makes a 1 bpp sprite a two colour palette sprite, drawing into it only
	//looks at the lowest bit of a colour: ColorWhite sets a bit, ColorBlack clears it
	screenBuffer.setColorDepth(SCREENBUFFER);
#if !LOVYANGFX
	//a 16 bpp sprite keeps its pixels byte swapped, the way pushSprite sends them, so the
	//little endian image data has to be swapped on the way in. The 8 bpp conversion wants
	//the plain value, so there it must not be. With LovyanGFX no image data goes through
	//the sprite's own image functions, the pixels are written into its buffer directly
	screenBuffer.setSwapBytes(SCREENBUFFER == 16);
#endif
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
#endif
#if (SCREENBUFFER == 8) && LOVYANGFX
	//the library's own RGB332 to RGB565 conversion, so the colours are the ones pushSprite showed
	bufferPalette = (uint16_t*)malloc(256 * sizeof(uint16_t));
	if (!bufferPalette)
		Platform_Log("the palette could not be allocated\n");
	else
		for (uint16_t i = 0; i < 256; i++)
			bufferPalette[i] = (uint16_t)lgfx::color_convert<lgfx::swap565_t, lgfx::rgb332_t>(i);
#endif
}

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if (SCREENBUFFER == 1) && LOVYANGFX
	//byte swapped, Platform_PresentFrame writes them into its rows as they are
	bufferSetColor = (uint16_t)((setColor >> 8) | (setColor << 8));
	bufferClearColor = (uint16_t)((clearColor >> 8) | (clearColor << 8));
#elif SCREENBUFFER == 1
	screenBuffer.setBitmapColor(setColor, clearColor);
#else
	(void)setColor;
	(void)clearColor;
#endif
}

void Platform_PresentFrame(void)
{
#if ((SCREENBUFFER == 1) || (SCREENBUFFER == 8)) && LOVYANGFX
	//A 16 bpp buffer already holds display ready pixels and pushSprite sends it as raw bytes.
	//A 1 or 8 bpp one would go through the library's general pixel converter, pixel by
	//pixel, which made those modes fall below the frame rate. So the rows are turned into
	//display byte order (swap565) here, and handed over as swap565 they take the same raw
	//byte path. It also keeps the 1 bpp frame away from the palette push, which left the
	//ESPboy's display empty.
	static_assert((WINDOW_WIDTH % 8) == 0, "every row of a 1 bpp buffer has to start on a byte");
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
  #if SCREENBUFFER == 8
	//the palette is not there in a web app store build, and without it there is nothing to show
	if (!bufferPalette)
		return;
  #endif
	uint16_t line[WINDOW_WIDTH];
	SCREEN.startWrite();
	SCREEN.setAddrWindow(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	for (int16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
  #if SCREENBUFFER == 1
		//a byte of the buffer at a time, most significant bit first as SetBufferBit writes
		for (int16_t x = 0; x < WINDOW_WIDTH; x += 8)
		{
			uint8_t bits = *src++;
			for (uint8_t b = 0; b < 8; b++, bits <<= 1)
				line[x + b] = (bits & 0x80) ? bufferSetColor : bufferClearColor;
		}
  #else
		for (int16_t x = 0; x < WINDOW_WIDTH; x++)
			line[x] = bufferPalette[*src++];
  #endif
		SCREEN.writePixels((const lgfx::swap565_t*)line, WINDOW_WIDTH);
	}
	SCREEN.endWrite();
#elif SCREENBUFFER
	screenBuffer.pushSprite(0, 0);
#endif
}

uint8_t Platform_GetButtons(void)
{
	return myESPboy.getKeys();
}

uint32_t Platform_Micros(void)
{
	return micros();
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	if (duration)
		myESPboy.playTone(freq, duration);
	else
		myESPboy.playTone(freq);
}

uint32_t Platform_FreeHeap(void)
{
	return ESP.getFreeHeap();
}

uint32_t Platform_FreeStack(void)
{
	return ESP.getFreeContStack();
}

uint32_t Platform_RandomSeed(void)
{
	//the ESP8266's hardware random number generator, time() has no clock to read here and
	//returns about the same value every start
	return ESP.random();
}

void Platform_Log(const char* format, ...)
{
	//the serial port is opened by the first thing that logs, so a build that never logs does not
	//take it and nothing has to remember to open it
	static bool serialStarted = false;
	if (!serialStarted)
	{
		Serial.begin(115200);
		serialStarted = true;
	}
	char text[128];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	Serial.print(text);
}

// The save storage is the ESP8266's emulated EEPROM, one flash sector. Every begin()
// asks for the same PLATFORM_STORAGE_SIZE: a commit erases the whole sector and writes
// back only the bytes it was given, so a shorter begin() would wipe whatever sits beyond it.

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	EEPROM.begin(PLATFORM_STORAGE_SIZE);
	for (uint16_t i = 0; i < length; i++)
		data[i] = EEPROM.read(offset + i);
	EEPROM.end();
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	EEPROM.begin(PLATFORM_STORAGE_SIZE);
	//write only flags the buffer dirty when a byte really differs, so storing what is
	//already there costs no flash erase at all
	for (uint16_t i = 0; i < length; i++)
		EEPROM.write(offset + i, data[i]);
	EEPROM.commit();
	EEPROM.end();
}

#endif
