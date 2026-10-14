//Platform.h for the Pimoroni PicoSystem, built with the arduino-pico core (board: Generic RP2040,
//flash size 16 MB). The hardware, as the Pico SDK's pimoroni_picosystem.h board file has it:
//  display  ST7789 240x240 on SPI0: SCK 6, MOSI 7, chip select 5, data/command 9, reset 4,
//           backlight 12
//  buttons  one GPIO each, low while held: up 23, down 20, left 22, right 21, A 18, B 19, X 17, Y 16
//  sound    the piezo on pin 11
//  saves    arduino-pico's EEPROM, a sector of the flash
//
//Buttons: d-pad, A, B, Y = left side button, X = right side button.
//
//With a screen buffer the second core sends the frames to the display: Platform_PresentFrame on the
//first core only hands a copy over, so the game does not wait for the SPI transfer.
//
//The display goes through the Pico SDK's SPI functions and not the SPI library: its SPI object
//would claim pins 16, 18 and 19 for SPI0, and those are buttons on the PicoSystem.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_PICOSYSTEM

#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include "PlatformGamebuinoFont.h"

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
//the game's screen sits in the middle, unless SCALESCREEN scales a buffered frame to the whole display
#define DISPLAY_OFFSET_X ((DISPLAY_WIDTH - WINDOW_WIDTH) / 2)
#define DISPLAY_OFFSET_Y ((DISPLAY_HEIGHT - WINDOW_HEIGHT) / 2)

#define LCD_SPI spi0
#define LCD_SPI_FREQ 62500000
#define LCD_SCK_PIN 6
#define LCD_MOSI_PIN 7
#define LCD_CS_PIN 5
#define LCD_DC_PIN 9
#define LCD_RESET_PIN 4
#define LCD_BACKLIGHT_PIN 12
#define AUDIO_PIN 11
#define BUTTON_UP_PIN 23
#define BUTTON_DOWN_PIN 20
#define BUTTON_LEFT_PIN 22
#define BUTTON_RIGHT_PIN 21
#define BUTTON_A_PIN 18
#define BUTTON_B_PIN 19
#define BUTTON_X_PIN 17
#define BUTTON_Y_PIN 16

static PlatformPicoSystemDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the display once at the end of Game_Loop
PlatformBuffer screenBuffer;
#endif
#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in, high byte first as the display takes them
static uint8_t bufferSetColor[2] = { 0xFF, 0xFF }, bufferClearColor[2] = { 0x00, 0x00 };
#endif
#if SCREENBUFFER == 8
//every RGB332 value as the two bytes of its RGB565 pixel, high byte first
static uint8_t bufferPalette[256 * 2];
#endif

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
// SPI
//
// Bytes are gathered in a small buffer and sent a block at a time: spi_write_blocking per byte
// would cost a call and a wait for every one of the 115 KB of a scaled frame
// ===========================================================================

static uint8_t spiBuffer[512];
static uint16_t spiUsed = 0;

static void SpiFlush(void)
{
	if (spiUsed)
	{
		spi_write_blocking(LCD_SPI, spiBuffer, spiUsed);
		spiUsed = 0;
	}
}

static inline void SpiWrite(uint8_t value)
{
	spiBuffer[spiUsed++] = value;
	if (spiUsed == sizeof(spiBuffer))
		SpiFlush();
}

static void WriteCommand(uint8_t command)
{
	SpiFlush();
	gpio_put(LCD_DC_PIN, 0);
	SpiWrite(command);
	SpiFlush();
	gpio_put(LCD_DC_PIN, 1);
}

//a command and its arguments in a transaction of its own, for the init
static void SendCommand(uint8_t command, uint8_t argCount = 0, const char* args = nullptr)
{
	gpio_put(LCD_CS_PIN, 0);
	WriteCommand(command);
	for (uint8_t i = 0; i < argCount; i++)
		SpiWrite((uint8_t)args[i]);
	SpiFlush();
	gpio_put(LCD_CS_PIN, 1);
}

// ===========================================================================
// Display
// ===========================================================================

//the area of the display itself the pixels written next fill, both corners included
static void SetDisplayWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	WriteCommand(0x2A);
	SpiWrite((uint8_t)(x0 >> 8));
	SpiWrite((uint8_t)x0);
	SpiWrite((uint8_t)(x1 >> 8));
	SpiWrite((uint8_t)x1);
	WriteCommand(0x2B);
	SpiWrite((uint8_t)(y0 >> 8));
	SpiWrite((uint8_t)y0);
	SpiWrite((uint8_t)(y1 >> 8));
	SpiWrite((uint8_t)y1);
	WriteCommand(0x2C);
}

void PlatformPicoSystemDisplay::init(void)
{
	gpio_init(LCD_CS_PIN);
	gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
	gpio_put(LCD_CS_PIN, 1);
	gpio_init(LCD_DC_PIN);
	gpio_set_dir(LCD_DC_PIN, GPIO_OUT);
	gpio_put(LCD_DC_PIN, 1);
	gpio_init(LCD_RESET_PIN);
	gpio_set_dir(LCD_RESET_PIN, GPIO_OUT);
	gpio_put(LCD_RESET_PIN, 1);
	//The SPI clock is at most half the peripheral clock, which the core runs from the 48 MHz USB
	//PLL: LCD_SPI_FREQ became 24 MHz (measured on a PicoSystem). From the system clock, 125 MHz
	//(the build's freq=125), the SPI runs at the 62.5 MHz asked for
	clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, clock_get_hz(clk_sys),
	                clock_get_hz(clk_sys));
	spi_init(LCD_SPI, LCD_SPI_FREQ);
	gpio_set_function(LCD_SCK_PIN, GPIO_FUNC_SPI);
	gpio_set_function(LCD_MOSI_PIN, GPIO_FUNC_SPI);

	//a reset pulse, then the init the 32blit SDK uses for the PicoSystem's display
	gpio_put(LCD_RESET_PIN, 0);
	delay(10);
	gpio_put(LCD_RESET_PIN, 1);
	delay(10);
	SendCommand(0x01);                                  //software reset
	delay(150);
	SendCommand(0x3A, 1, "\x05");                       //16 bits per pixel
	SendCommand(0xB2, 5, "\x0c\x0c\x00\x33\x33");       //porch control
	SendCommand(0xB7, 1, "\x14");                       //gate control
	SendCommand(0xBB, 1, "\x37");                       //VCOM
	SendCommand(0xC0, 1, "\x2c");                       //LCM control
	SendCommand(0xC2, 1, "\x01");                       //VDV and VRH from the commands
	SendCommand(0xC3, 1, "\x12");                       //VRH
	SendCommand(0xC4, 1, "\x20");                       //VDV
	SendCommand(0xD0, 2, "\xa4\xa1");                   //power control
	SendCommand(0xE0, 14, "\xD0\x08\x11\x08\x0c\x15\x39\x33\x50\x36\x13\x14\x29\x2d");
	SendCommand(0xE1, 14, "\xD0\x08\x10\x08\x06\x06\x39\x44\x51\x0b\x16\x14\x2f\x31");
	SendCommand(0xC6, 1, "\x15");                       //frame rate 50 Hz
	SendCommand(0x21);                                  //inversion on, the panel needs it
	SendCommand(0x11);                                  //out of sleep mode
	SendCommand(0x29);                                  //display on
	delay(100);
	SendCommand(0x36, 1, "\x00");                       //MADCTL: no rotation, RGB order

	//black until the game draws, with the game drawn 1:1 the border stays that way
	startWrite();
	SetDisplayWindow(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
	writeColor(0x0000, (uint32_t)DISPLAY_WIDTH * DISPLAY_HEIGHT);
	endWrite();

	pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
	digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
}

void PlatformPicoSystemDisplay::startWrite(void)
{
	if (writeDepth++ == 0)
		gpio_put(LCD_CS_PIN, 0);
}

void PlatformPicoSystemDisplay::endWrite(void)
{
	if (writeDepth && (--writeDepth == 0))
	{
		SpiFlush();
		gpio_put(LCD_CS_PIN, 1);
	}
}

void PlatformPicoSystemDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	startWrite();
	SetDisplayWindow((uint16_t)(x + DISPLAY_OFFSET_X), (uint16_t)(y + DISPLAY_OFFSET_Y),
	                 (uint16_t)(x + DISPLAY_OFFSET_X + w - 1), (uint16_t)(y + DISPLAY_OFFSET_Y + h - 1));
	endWrite();
}

void PlatformPicoSystemDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	startWrite();
	if (swap)
	{
		for (int32_t i = 0; i < length; i++)
		{
			SpiWrite((uint8_t)(data[i] >> 8));
			SpiWrite((uint8_t)data[i]);
		}
	}
	else
	{
		for (int32_t i = 0; i < length; i++)
		{
			SpiWrite((uint8_t)data[i]);
			SpiWrite((uint8_t)(data[i] >> 8));
		}
	}
	endWrite();
}

void PlatformPicoSystemDisplay::writeColor(uint16_t color, uint32_t length)
{
	const uint8_t high = (uint8_t)(color >> 8), low = (uint8_t)color;
	startWrite();
	while (length--)
	{
		SpiWrite(high);
		SpiWrite(low);
	}
	endWrite();
}

void PlatformPicoSystemDisplay::writeBytes(const uint8_t* data, uint32_t length)
{
	startWrite();
	//a block this big goes out as it is, no need to copy it into the SPI buffer first
	if (length >= sizeof(spiBuffer))
	{
		SpiFlush();
		spi_write_blocking(LCD_SPI, data, length);
	}
	else
	{
		while (length--)
			SpiWrite(*data++);
	}
	endWrite();
}

//clips x, y, w, h to the game's screen, false when nothing of it is left
static bool ClipRect(int32_t& x, int32_t& y, int32_t& w, int32_t& h)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > WINDOW_WIDTH) w = WINDOW_WIDTH - x;
	if (y + h > WINDOW_HEIGHT) h = WINDOW_HEIGHT - y;
	return (w > 0) && (h > 0);
}

void PlatformPicoSystemDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	startWrite();
	setAddrWindow(x, y, w, h);
	writeColor(color, (uint32_t)w * h);
	endWrite();
}

void PlatformPicoSystemGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if ((w <= 0) || (h <= 0))
		return;
	startWrite();
	fillRect(x, y, w, 1, color);
	fillRect(x, y + h - 1, w, 1, color);
	if (h > 2)
	{
		fillRect(x, y + 1, 1, h - 2, color);
		fillRect(x + w - 1, y + 1, 1, h - 2, color);
	}
	endWrite();
}

//Draws the character the way LovyanGFX draws its default font: a 6x8 cell times the text
//size, the 5 columns of the glyph and a column of background, the background only when it
//differs from the text colour. Every column goes out as runs of one colour
size_t PlatformPicoSystemGFX::drawChar(uint16_t c, int32_t x, int32_t y)
{
	const int32_t size = textSize;
	//the 'classic' character set LovyanGFX uses unless told otherwise
	if (c >= 176)
		c++;
	if (c > 255)
		return 6 * size;
	const bool fillBackground = (textBackground != textColor);
	const uint8_t* glyph = &platformFont[c * 5];
	startWrite();
	for (int32_t column = 0; column < 5; column++)
	{
		const uint8_t bits = glyph[column];
		int32_t row = 0;
		while (row < 8)
		{
			const bool set = ((bits >> row) & 1) != 0;
			int32_t end = row + 1;
			while ((end < 8) && ((((bits >> end) & 1) != 0) == set))
				end++;
			if (set || fillBackground)
				fillRect(x + column * size, y + row * size, size, (end - row) * size, set ? textColor : textBackground);
			row = end;
		}
	}
	if (fillBackground)
		fillRect(x + 5 * size, y, size, 8 * size, textBackground);
	endWrite();
	return 6 * size;
}

bool PlatformPicoSystemBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformPicoSystemBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!pixels || !ClipRect(x, y, w, h))
		return;
	(void)depth;
#if SCREENBUFFER == 16
	//a 16 bpp sprite keeps its pixels byte swapped
	const uint16_t value = (uint16_t)((color >> 8) | (color << 8));
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &((uint16_t*)(void*)pixels)[row * WINDOW_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = value;
	}
#elif SCREENBUFFER == 8
	const uint8_t value = (uint8_t)(((color & 0xE000) >> 8) | ((color & 0x0700) >> 6) | ((color & 0x0018) >> 3));
	for (int32_t row = y; row < y + h; row++)
		memset(&pixels[row * WINDOW_WIDTH + x], value, w);
#elif SCREENBUFFER == 1
	for (int32_t row = y; row < y + h; row++)
		for (int32_t column = x; column < x + w; column++)
			SetBufferBit(pixels, column, row, color);
#else
	(void)color;
#endif
}

void Platform_Init(const char* appName)
{
	(void)appName;
	display.init();

	//every button pulls its pin low
	const uint8_t buttonPins[] = { BUTTON_UP_PIN, BUTTON_DOWN_PIN, BUTTON_LEFT_PIN, BUTTON_RIGHT_PIN,
	                               BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_X_PIN, BUTTON_Y_PIN };
	for (uint8_t i = 0; i < sizeof(buttonPins); i++)
		pinMode(buttonPins[i], INPUT_PULLUP);

	//the save storage stays open, a write commits it to flash
	EEPROM.begin(PLATFORM_STORAGE_SIZE);

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
		bufferPalette[i * 2] = (uint8_t)(color >> 8);
		bufferPalette[i * 2 + 1] = (uint8_t)color;
	}
#endif
}

#if SCREENBUFFER
//bytes of one row of the screen buffer
  #if SCREENBUFFER == 16
#define BUFFER_ROW_BYTES (WINDOW_WIDTH * 2)
  #elif SCREENBUFFER == 8
#define BUFFER_ROW_BYTES WINDOW_WIDTH
  #else
#define BUFFER_ROW_BYTES ((WINDOW_WIDTH + 7) / 8)
  #endif

//The frame as it was last sent. Sending costs far more than comparing: only the rows that differ
//from it go to the display again, and of those only the columns that changed. In a frame where
//just the player moves that is a few KB instead of the whole frame
static uint8_t sentFrame[BUFFER_ROW_BYTES * WINDOW_HEIGHT];
static bool sentFrameValid = false;

//The frame the first core handed to the second one to send. frameReady is set by the first core
//once the copy is complete and cleared by the second one once it is sent, only the core that
//owns the frame at that moment touches it
static uint8_t presentFrame[BUFFER_ROW_BYTES * WINDOW_HEIGHT];
static volatile bool frameReady = false;
  #if SCREENBUFFER == 1
//the colours the handed over frame is shown in, and whether they changed since the last one
static uint8_t presentSetColor[2] = { 0xFF, 0xFF }, presentClearColor[2] = { 0x00, 0x00 };
static bool coloursChanged = true;
static bool presentColoursChanged = false;
  #endif

//The display column and row every game column and row starts at, one more for where the last
//one ends. 1:1 in the middle of the display, or with SCALESCREEN nearest neighbour over the whole
//display: 240 / 128 is not a whole number, so a game pixel is 1 or 2 display pixels wide
static uint16_t displayColumn[WINDOW_WIDTH + 1], displayRow[WINDOW_HEIGHT + 1];

static void MapDisplay(void)
{
	for (uint16_t x = 0; x <= WINDOW_WIDTH; x++)
  #if SCALESCREEN
		displayColumn[x] = (uint16_t)((x * DISPLAY_WIDTH + WINDOW_WIDTH - 1) / WINDOW_WIDTH);
  #else
		displayColumn[x] = (uint16_t)(DISPLAY_OFFSET_X + x);
  #endif
	for (uint16_t y = 0; y <= WINDOW_HEIGHT; y++)
  #if SCALESCREEN
		displayRow[y] = (uint16_t)((y * DISPLAY_HEIGHT + WINDOW_HEIGHT - 1) / WINDOW_HEIGHT);
  #else
		displayRow[y] = (uint16_t)(DISPLAY_OFFSET_Y + y);
  #endif
}

//the two bytes the display takes for game pixel x of a buffer row
static inline const uint8_t* PixelBytes(const uint8_t* row, uint16_t x)
{
  #if SCREENBUFFER == 16
	//byte swapped in the buffer, so the bytes already are in display order
	return &row[x * 2];
  #elif SCREENBUFFER == 8
	return &bufferPalette[row[x] * 2];
  #else
	//most significant bit first as SetBufferBit writes
	return (row[x >> 3] & (0x80 >> (x & 7))) ? presentSetColor : presentClearColor;
  #endif
}

//false when row y is the same as when it was last sent, otherwise the first and last game column
//that differ
static bool RowChanged(const uint8_t* frame, int16_t y, uint16_t& first, uint16_t& last)
{
	if (!sentFrameValid)
	{
		first = 0;
		last = WINDOW_WIDTH - 1;
		return true;
	}
	const uint8_t* row = &frame[y * BUFFER_ROW_BYTES];
	const uint8_t* sent = &sentFrame[y * BUFFER_ROW_BYTES];
	uint16_t a = 0, b = BUFFER_ROW_BYTES;
	while ((a < b) && (row[a] == sent[a]))
		a++;
	if (a == b)
		return false;
	while (row[b - 1] == sent[b - 1])
		b--;
  #if SCREENBUFFER == 16
	first = a / 2;
	last = (b - 1) / 2;
  #elif SCREENBUFFER == 8
	first = a;
	last = b - 1;
  #else
	first = a * 8;
	last = (b * 8 - 1 < WINDOW_WIDTH - 1) ? b * 8 - 1 : WINDOW_WIDTH - 1;
  #endif
	return true;
}
#endif

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	bufferSetColor[0] = (uint8_t)(setColor >> 8);
	bufferSetColor[1] = (uint8_t)setColor;
	bufferClearColor[0] = (uint8_t)(clearColor >> 8);
	bufferClearColor[1] = (uint8_t)clearColor;
	//every pixel on the display has the old colours, the frame that takes the new ones goes out whole
	coloursChanged = true;
#else
	(void)setColor;
	(void)clearColor;
#endif
}

#if SCREENBUFFER
//Second core: sends what changed in the handed over frame since the last one, every run of changed
//rows gets one display window over the columns that changed in any of them
static void SendFrame(void)
{
	const uint8_t* src = presentFrame;
  #if SCREENBUFFER == 1
	if (presentColoursChanged)
	{
		sentFrameValid = false;
		presentColoursChanged = false;
	}
  #endif
	static bool mapped = false;
	if (!mapped)
	{
		MapDisplay();
		mapped = true;
	}
	uint8_t line[DISPLAY_WIDTH * 2];
	display.startWrite();
	int16_t y = 0;
	while (y < WINDOW_HEIGHT)
	{
		//the run of changed rows starting here, and the columns that changed in any of them
		const int16_t runStart = y;
		uint16_t first = WINDOW_WIDTH - 1, last = 0;
		uint16_t rowFirst, rowLast;
		while ((y < WINDOW_HEIGHT) && RowChanged(src, y, rowFirst, rowLast))
		{
			if (rowFirst < first)
				first = rowFirst;
			if (rowLast > last)
				last = rowLast;
			y++;
		}
		if (y == runStart)
		{
			y++;
			continue;
		}
		SetDisplayWindow(displayColumn[first], displayRow[runStart], displayColumn[last + 1] - 1, displayRow[y] - 1);
		for (int16_t sy = runStart; sy < y; sy++)
		{
			const uint8_t* row = &src[sy * BUFFER_ROW_BYTES];
			uint8_t* dst = line;
			for (uint16_t sx = first; sx <= last; sx++)
			{
				const uint8_t* pixel = PixelBytes(row, sx);
				for (uint16_t n = displayColumn[sx + 1] - displayColumn[sx]; n > 0; n--)
				{
					*dst++ = pixel[0];
					*dst++ = pixel[1];
				}
			}
			//a game row is 1 or 2 display rows high when scaled
			for (uint16_t n = displayRow[sy + 1] - displayRow[sy]; n > 0; n--)
				display.writeBytes(line, (uint32_t)(dst - line));
			memcpy(&sentFrame[sy * BUFFER_ROW_BYTES], row, BUFFER_ROW_BYTES);
		}
	}
	display.endWrite();
	sentFrameValid = true;
}

//The second core's program: it waits for a frame and sends it
void setup1()
{
}

void loop1()
{
	if (!frameReady)
	{
		delay(1);
		return;
	}
	SendFrame();
	frameReady = false;
}
#endif

//First core: hands a copy of the frame to the second core. While that one is still sending the
//last frame this one is left out, a later frame shows whatever changed. Without a buffer there is
//nothing to send, the game drew straight to the display
void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src || frameReady)
		return;
	memcpy(presentFrame, src, sizeof(presentFrame));
  #if SCREENBUFFER == 1
	if (coloursChanged)
	{
		memcpy(presentSetColor, bufferSetColor, sizeof(presentSetColor));
		memcpy(presentClearColor, bufferClearColor, sizeof(presentClearColor));
		presentColoursChanged = true;
		coloursChanged = false;
	}
  #endif
	frameReady = true;
#endif
}

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	uint8_t buttons = 0;
	if (!digitalRead(BUTTON_LEFT_PIN))
		buttons |= BUTTON_LEFT;
	if (!digitalRead(BUTTON_UP_PIN))
		buttons |= BUTTON_UP;
	if (!digitalRead(BUTTON_DOWN_PIN))
		buttons |= BUTTON_DOWN;
	if (!digitalRead(BUTTON_RIGHT_PIN))
		buttons |= BUTTON_RIGHT;
	if (!digitalRead(BUTTON_A_PIN))
		buttons |= BUTTON_A;
	if (!digitalRead(BUTTON_B_PIN))
		buttons |= BUTTON_B;
	if (!digitalRead(BUTTON_Y_PIN))
		buttons |= BUTTON_L;
	if (!digitalRead(BUTTON_X_PIN))
		buttons |= BUTTON_R;
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
	//a frequency of 0 is a rest
	if (!freq)
		noTone(AUDIO_PIN);
	else if (duration)
		tone(AUDIO_PIN, freq, duration);
	else
		tone(AUDIO_PIN, freq);
}

uint32_t Platform_FreeHeap(void)
{
	return (uint32_t)rp2040.getFreeHeap();
}

//the stack that is free right now, the core does not keep the least since boot
uint32_t Platform_FreeStack(void)
{
	return (uint32_t)rp2040.getFreeStack();
}

uint32_t Platform_RandomSeed(void)
{
	//the RP2040's ring oscillator gives random bits
	return rp2040.hwrand32();
}

void Platform_Log(const char* format, ...)
{
	char text[128];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	//only to a computer that has the USB serial port open
	if (Serial)
		Serial.print(text);
}

// ===========================================================================
// Saved data
//
// arduino-pico's EEPROM keeps a copy in RAM and writes it to a flash sector on commit. Erased
// flash reads as 0xFF, as on the ESPboy, so the game sees a never saved store.
// ===========================================================================

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	for (uint16_t i = 0; i < length; i++)
		data[i] = EEPROM.read(offset + i);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	//only a byte that really differs marks the copy dirty, storing what is already there costs no
	//flash write at all
	for (uint16_t i = 0; i < length; i++)
		EEPROM.write(offset + i, data[i]);
	EEPROM.commit();
}

#endif
