//Platform.h for the Adafruit PyBadge and PyGamer. Adafruit's ST7735 library drives the display
//and Adafruit_SPIFlash with SdFat keeps the save file, the rest is the Arduino core. The pins are
//the ones the Adafruit Arcada library uses for both boards:
//  display  ST7735R on SPI1, chip select 44, data/command 45, reset 46, backlight 47
//  buttons  a shift register, clock 48, data 49, latch 50. The PyGamer's d-pad is the analog
//           joystick on A11 (x) and A10 (y)
//  sound    tone() on A0, the speaker amplifier is switched on with pin 51
//  saves    a file on the FAT file system of the QSPI flash, in a folder named after the game
//
//Buttons: d-pad (PyBadge) or joystick (PyGamer), A, B, SELECT = left side button,
//START = right side button

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_PYBADGE

#include <stdarg.h>
#include <stdio.h>
#include <malloc.h>
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SPIFlash.h>

//the game's screen in the middle of the 160x128 display
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 128
#define DISPLAY_OFFSET_X ((DISPLAY_WIDTH - WINDOW_WIDTH) / 2)
#define DISPLAY_OFFSET_Y ((DISPLAY_HEIGHT - WINDOW_HEIGHT) / 2)

#define TFT_CS_PIN 44
#define TFT_DC_PIN 45
#define TFT_RST_PIN 46
#define TFT_BACKLIGHT_PIN 47
#define BUTTON_CLOCK_PIN 48
#define BUTTON_DATA_PIN 49
#define BUTTON_LATCH_PIN 50
#define SPEAKER_ENABLE_PIN 51
#define AUDIO_PIN A0
#define NEOPIXEL_PIN 8
#define NEOPIXEL_COUNT 5

//the bits of the shift register, 1 while the button is held. Only the PyBadge has its d-pad on it
#define SHIFT_B      0x80
#define SHIFT_A      0x40
#define SHIFT_START  0x20
#define SHIFT_SELECT 0x10
#define SHIFT_RIGHT  0x08
#define SHIFT_DOWN   0x04
#define SHIFT_UP     0x02
#define SHIFT_LEFT   0x01

#if defined(ADAFRUIT_PYGAMER_M4_EXPRESS)
#define JOYSTICK_X_PIN A11
#define JOYSTICK_Y_PIN A10
//the stick reads 0..1023 with the middle at 512, pushed further than this from the middle it
//counts as a direction, as in the Arcada library
#define JOYSTICK_THRESHOLD 350
#endif

static PlatformPyBadgeDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the display once at the end of Game_Loop
PlatformBuffer screenBuffer;
#endif
#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in, in display byte order
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as a 16 bit pixel in display byte order, see Platform_PresentFrame
static uint16_t bufferPalette[256];
#endif

static void PaintStack(void);
static void StorageInit(const char* appName);

// ===========================================================================
// Program start
// ===========================================================================

//the Arduino core starts the program here
void setup()
{
	Game_Setup();
}

void loop()
{
	Game_Loop();
}

// ===========================================================================
// Display
// ===========================================================================

PlatformPyBadgeDisplay::PlatformPyBadgeDisplay(void) :
	Adafruit_ST7735(&SPI1, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN)
{
}

void PlatformPyBadgeDisplay::init(void)
{
	//dark until the display shows something
	pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
	digitalWrite(TFT_BACKLIGHT_PIN, LOW);
	//the display as Arcada starts it on both boards, turned to 160 wide and 128 high
	initR(INITR_BLACKTAB);
	//The SPI clock is at most half its SERCOM's clock, 48 MHz by default: the library's 32 MHz
	//became 24 (measured on a PyGamer, 13.7 ms for a 160x128 frame). From the 100 MHz clock it
	//runs at 50: 6.6 ms
	SPI1.setClockSource(SERCOM_CLOCK_SOURCE_100M);
	setSPISpeed(50000000);
	setRotation(1);
	//the strips left and right of the game's screen stay black
	fillScreen(0x0000);
	//From here on 0,0 is the top left of the game's screen: the library adds the offset to every
	//window it opens and clips what it draws itself at the width and height
	_xstart += DISPLAY_OFFSET_X;
	_ystart += DISPLAY_OFFSET_Y;
	_width = WINDOW_WIDTH;
	_height = WINDOW_HEIGHT;
	digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
}

void PlatformPyBadgeDisplay::startWrite(void)
{
	if (writeDepth++ == 0)
		Adafruit_ST7735::startWrite();
}

void PlatformPyBadgeDisplay::endWrite(void)
{
	if (writeDepth && (--writeDepth == 0))
		Adafruit_ST7735::endWrite();
}

void PlatformPyBadgeDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	if (length <= 0)
		return;
	//The library only reads the pixels: little endian ones are swapped into a buffer of its own,
	//big endian ones go out as they are. Blocking, the next call may open another window
	Adafruit_ST7735::writePixels((uint16_t*)data, (uint32_t)length, true, !swap);
}

size_t PlatformPyBadgeDisplay::drawChar(uint16_t c, int32_t x, int32_t y)
{
	//Adafruit's GLCD font is the one LovyanGFX's default font comes from: a 6x8 cell with the
	//background only painted when it differs from the text colour
	Adafruit_GFX::drawChar((int16_t)x, (int16_t)y, (unsigned char)c, textcolor, textbgcolor, textsize_x, textsize_y);
	return 6 * textsize_x;
}

PlatformPyBadgeBuffer::PlatformPyBadgeBuffer(void) :
	Adafruit_GFX(WINDOW_WIDTH, WINDOW_HEIGHT)
{
}

bool PlatformPyBadgeBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * (depth / 8);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformPyBadgeBuffer::drawPixel(int16_t x, int16_t y, uint16_t color)
{
#if SCREENBUFFER
	if (!pixels || (x < 0) || (y < 0) || (x >= WINDOW_WIDTH) || (y >= WINDOW_HEIGHT))
		return;
	SetBufferPixel(pixels, x, y, color);
#else
	(void)x;
	(void)y;
	(void)color;
#endif
}

void PlatformPyBadgeBuffer::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	int32_t x0 = x, y0 = y, x1 = (int32_t)x + w, y1 = (int32_t)y + h;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > WINDOW_WIDTH) x1 = WINDOW_WIDTH;
	if (y1 > WINDOW_HEIGHT) y1 = WINDOW_HEIGHT;
	if (!pixels || (x0 >= x1) || (y0 >= y1))
		return;
#if SCREENBUFFER == 16
	(void)depth;
	//a 16 bpp buffer keeps its pixels byte swapped
	const uint16_t value = (uint16_t)((color >> 8) | (color << 8));
	for (int32_t row = y0; row < y1; row++)
	{
		uint16_t* d = &((uint16_t*)(void*)pixels)[row * WINDOW_WIDTH];
		for (int32_t column = x0; column < x1; column++)
			d[column] = value;
	}
#elif SCREENBUFFER == 8
	(void)depth;
	const uint8_t value = (uint8_t)(((color & 0xE000) >> 8) | ((color & 0x0700) >> 6) | ((color & 0x0018) >> 3));
	for (int32_t row = y0; row < y1; row++)
		memset(&pixels[row * WINDOW_WIDTH + x0], value, x1 - x0);
#elif SCREENBUFFER == 1
	(void)depth;
	for (int32_t row = y0; row < y1; row++)
		for (int32_t column = x0; column < x1; column++)
			SetBufferBit(pixels, column, row, color);
#else
	(void)depth;
	(void)color;
#endif
}

size_t PlatformPyBadgeBuffer::drawChar(uint16_t c, int32_t x, int32_t y)
{
	Adafruit_GFX::drawChar((int16_t)x, (int16_t)y, (unsigned char)c, textcolor, textbgcolor, textsize_x, textsize_y);
	return 6 * textsize_x;
}

void Platform_Init(const char* appName)
{
	//quiet until the game plays something, the amplifier is switched on once the pin is low
	pinMode(AUDIO_PIN, OUTPUT);
	digitalWrite(AUDIO_PIN, LOW);
	pinMode(SPEAKER_ENABLE_PIN, OUTPUT);
	digitalWrite(SPEAKER_ENABLE_PIN, LOW);

	//the NeoPixels keep what an earlier program showed on them, the game does not use them
	Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
	pixels.begin();
	pixels.clear();
	pixels.show();

	//the shift register's clock and latch rest high, as the Arcada library leaves them
	pinMode(BUTTON_CLOCK_PIN, OUTPUT);
	digitalWrite(BUTTON_CLOCK_PIN, HIGH);
	pinMode(BUTTON_LATCH_PIN, OUTPUT);
	digitalWrite(BUTTON_LATCH_PIN, HIGH);
	pinMode(BUTTON_DATA_PIN, INPUT);

	display.init();
	StorageInit(appName);
	digitalWrite(SPEAKER_ENABLE_PIN, HIGH);
#if SCREENBUFFER
	screenBuffer.setColorDepth(SCREENBUFFER);
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
#endif
#if SCREENBUFFER == 8
	//RGB332 to RGB565 the way LovyanGFX converts it, so the colours match the other devices
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
		const uint16_t color = (uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3));
		bufferPalette[i] = (uint16_t)((color >> 8) | (color << 8));
	}
#endif
	//last, the stack that is free from here on is what Platform_FreeStack measures
	PaintStack();
}

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	//byte swapped, Platform_PresentFrame writes them into its rows as they are
	bufferSetColor = (uint16_t)((setColor >> 8) | (setColor << 8));
	bufferClearColor = (uint16_t)((clearColor >> 8) | (clearColor << 8));
#else
	(void)setColor;
	(void)clearColor;
#endif
}

void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	display.startWrite();
	display.setAddrWindow(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
  #if SCREENBUFFER == 16
	//the buffer already is in display byte order, the whole frame goes out as one DMA transfer
	display.writePixels((const uint16_t*)(void*)src, WINDOW_WIDTH * WINDOW_HEIGHT, false);
  #else
	//a row at a time, turned into pixels in display byte order
	uint16_t line[WINDOW_WIDTH];
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
		display.writePixels(line, WINDOW_WIDTH, false);
	}
  #endif
	display.endWrite();
#endif
}

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	//the latch loads the buttons into the shift register, then they are clocked out most
	//significant bit first, the way the Arcada library reads them
	digitalWrite(BUTTON_LATCH_PIN, LOW);
	delayMicroseconds(1);
	digitalWrite(BUTTON_LATCH_PIN, HIGH);
	delayMicroseconds(1);
	uint8_t held = 0;
	for (uint8_t i = 0; i < 8; i++)
	{
		held = (uint8_t)((held << 1) | (digitalRead(BUTTON_DATA_PIN) ? 1 : 0));
		digitalWrite(BUTTON_CLOCK_PIN, HIGH);
		delayMicroseconds(1);
		digitalWrite(BUTTON_CLOCK_PIN, LOW);
		delayMicroseconds(1);
	}

	uint8_t buttons = 0;
	if (held & SHIFT_A)
		buttons |= BUTTON_A;
	if (held & SHIFT_B)
		buttons |= BUTTON_B;
	if (held & SHIFT_SELECT)
		buttons |= BUTTON_L;
	if (held & SHIFT_START)
		buttons |= BUTTON_R;

#if defined(ADAFRUIT_PYGAMER_M4_EXPRESS)
	const int16_t x = (int16_t)analogRead(JOYSTICK_X_PIN) - 512;
	const int16_t y = (int16_t)analogRead(JOYSTICK_Y_PIN) - 512;
	if (x > JOYSTICK_THRESHOLD)
		buttons |= BUTTON_RIGHT;
	else if (x < -JOYSTICK_THRESHOLD)
		buttons |= BUTTON_LEFT;
	if (y > JOYSTICK_THRESHOLD)
		buttons |= BUTTON_DOWN;
	else if (y < -JOYSTICK_THRESHOLD)
		buttons |= BUTTON_UP;
#else
	if (held & SHIFT_LEFT)
		buttons |= BUTTON_LEFT;
	if (held & SHIFT_UP)
		buttons |= BUTTON_UP;
	if (held & SHIFT_DOWN)
		buttons |= BUTTON_DOWN;
	if (held & SHIFT_RIGHT)
		buttons |= BUTTON_RIGHT;
#endif
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return micros();
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	//a frequency of 0 is a rest, tone() stops the sound for it
	if (duration)
		tone(AUDIO_PIN, freq, duration);
	else
		tone(AUDIO_PIN, freq);
}

extern "C" char* sbrk(int increment);

//The heap grows up from the end of the program's data, the stack down from the end of RAM. Free
//heap is the gap between them plus what malloc has been given back
uint32_t Platform_FreeHeap(void)
{
	char* stackTop = (char*)__builtin_frame_address(0);
	return (uint32_t)(stackTop - sbrk(0)) + (uint32_t)mallinfo().fordblks;
}

#define STACK_PAINT 0xA5

//fills the gap between heap and stack with a pattern, what the stack reaches overwrites it
static void PaintStack(void)
{
	char* from = sbrk(0);
	//a little room below this function's own frame for the calls it makes
	char* to = (char*)__builtin_frame_address(0) - 64;
	for (char* p = from; p < to; p++)
		*p = (char)STACK_PAINT;
}

//the least stack that has been free since Platform_Init: the pattern left above the heap. Heap
//taken since then counts as used as well, heap and stack share the same gap
uint32_t Platform_FreeStack(void)
{
	char* p = sbrk(0);
	const char* limit = (char*)__builtin_frame_address(0);
	uint32_t count = 0;
	while ((p < limit) && (*p == (char)STACK_PAINT))
	{
		p++;
		count++;
	}
	return count;
}

uint32_t Platform_RandomSeed(void)
{
	//the SAMD51's true random number generator
	MCLK->APBCMASK.reg |= MCLK_APBCMASK_TRNG;
	TRNG->CTRLA.reg = TRNG_CTRLA_ENABLE;
	while (!TRNG->INTFLAG.bit.DATARDY)
		;
	const uint32_t value = TRNG->DATA.reg;
	TRNG->CTRLA.reg = 0;
	return value;
}

void Platform_Log(const char* format, ...)
{
	char text[128];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	//only to a computer that has the USB serial port open, it does not wait for one
	if (Serial)
		Serial.print(text);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in RAM and in the file /<Game>/<Game>.sav on the QSPI flash,
// named after the first word of the game's name. The flash needs a FAT file system, the one
// CircuitPython makes (the file shows up next to its files). Bytes the file does not have yet
// read as 0xFF, the way erased flash does on the ESPboy, so the game sees a never saved store.
// Without a file system the game still runs, it just does not keep anything.
// ===========================================================================

static Adafruit_FlashTransport_QSPI flashTransport(PIN_QSPI_SCK, PIN_QSPI_CS, PIN_QSPI_IO0,
                                                   PIN_QSPI_IO1, PIN_QSPI_IO2, PIN_QSPI_IO3);
static Adafruit_SPIFlash flash(&flashTransport);
static FatVolume fatfs;
static bool storageReady = false;
static uint8_t storage[PLATFORM_STORAGE_SIZE];
static char storagePath[32] = "/game/game.sav";

static void StorageInit(const char* appName)
{
	memset(storage, 0xFF, sizeof(storage));
	size_t n = 0;
	while (appName[n] && (appName[n] != ' ') && (n < 12))
		n++;
	if (n)
		snprintf(storagePath, sizeof(storagePath), "/%.*s/%.*s.sav", (int)n, appName, (int)n, appName);

	storageReady = flash.begin() && fatfs.begin(&flash);
	if (!storageReady)
	{
		Platform_Log("no file system on the QSPI flash, nothing is saved\n");
		return;
	}
	File32 file = fatfs.open(storagePath, O_RDONLY);
	if (file)
	{
		file.read(storage, sizeof(storage));
		file.close();
	}
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	//storing what is already there does not touch the flash
	if (memcmp(storage + offset, data, length) == 0)
		return;
	memcpy(storage + offset, data, length);
	if (!storageReady)
		return;
	//the folder is the part of the path before the file name
	char folder[sizeof(storagePath)];
	strcpy(folder, storagePath);
	char* slash = strrchr(folder, '/');
	if (slash && (slash != folder))
	{
		*slash = '\0';
		if (!fatfs.exists(folder))
			fatfs.mkdir(folder);
	}
	File32 file = fatfs.open(storagePath, O_WRONLY | O_CREAT | O_TRUNC);
	if (!file)
	{
		Platform_Log("could not write %s\n", storagePath);
		return;
	}
	file.write(storage, sizeof(storage));
	//closing writes the file system's cached blocks to the flash
	file.close();
}

#endif
