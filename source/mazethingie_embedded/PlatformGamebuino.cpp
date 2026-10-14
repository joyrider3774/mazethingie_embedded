//Platform.h for the Gamebuino META. Nothing of the Gamebuino library is used, only the Arduino
//core with its SPI library and SdFat for the save file:
//  display  ST7735 on SPI (SERCOM4), chip select PB22 (pin 30), data/command PB23 (pin 31)
//  buttons  a shift register on the same SPI, chip select pin 25
//  sound    the DAC on A0, a square wave made in the TC5 timer interrupt
//  saves    a file on the SD card (chip select pin 26), in a folder named after the game
//
//Buttons: d-pad, A, B, MENU = left side button, HOME = right side button. Holding HOME for a
//second goes back to the Gamebuino loader.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_GAMEBUINO

#include <stdarg.h>
#include <stdio.h>
#include <malloc.h>
#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include "PlatformGamebuinoFont.h"

//the game's screen in the middle of the 160x128 display
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 128
#define DISPLAY_OFFSET_X ((DISPLAY_WIDTH - WINDOW_WIDTH) / 2)
#define DISPLAY_OFFSET_Y ((DISPLAY_HEIGHT - WINDOW_HEIGHT) / 2)

#define TFT_CS_MASK (1ul << 22)      //PB22
#define TFT_DC_MASK (1ul << 23)      //PB23
#define BUTTONS_CS_PIN 25
#define SD_CS_PIN 26

//how long HOME has to be held to go back to the loader
#define HOME_HOLD_MS 1000

static PlatformGamebuinoDisplay display;
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

static void PaintStack(void);
static void StorageInit(const char* appName);
static void AudioInit(void);

// ===========================================================================
// Program start
// ===========================================================================

//the Arduino core starts the program here
void setup()
{
	//The bootloader starts the watchdog before it hands over, a game has to stop it straight
	//away or the console restarts
	WDT->CTRL.bit.ENABLE = 0;
	Game_Setup();
}

void loop()
{
	Game_Loop();
}

// ===========================================================================
// SPI
//
// Pixels go out through the SERCOM's register directly, a byte at a time through
// SPI.transfer costs a function call and waits for the byte coming back. The buttons and the
// SD card use the SPI library as usual, a display transaction is never open while they do.
// ===========================================================================

//set while a byte written may still be on its way out
static bool spiPending = false;

static inline void SpiWrite(uint8_t value)
{
	//the whole registers, not their bitfields: a bitfield write is a read modify write, and
	//reading DATA takes the byte the display sent back out of the receive buffer. At 24MHz a
	//byte is on its way out in 16 cycles of the 48MHz core, so what happens per byte here is
	//what decides whether the display is kept busy
	while (!(SERCOM4->SPI.INTFLAG.reg & SERCOM_SPI_INTFLAG_DRE))
		;
	SERCOM4->SPI.DATA.reg = value;
	spiPending = true;
}

//waits for the last byte to be sent, before the data/command line or the chip select changes
static void SpiWait(void)
{
	if (!spiPending)
		return;
	while (!SERCOM4->SPI.INTFLAG.bit.TXC)
		;
	//The bytes the display sent back are of no use. Left in the receive buffer, the next
	//SPI.transfer (the buttons) would take one of them as its answer. The count is bounded:
	//the web emulator holds INTFLAG.RXC set for good, an unbounded loop never leaves it
	for (uint8_t i = 0; i < 4 && SERCOM4->SPI.INTFLAG.bit.RXC; i++)
		(void)SERCOM4->SPI.DATA.reg;
	SERCOM4->SPI.STATUS.bit.BUFOVF = 1;
	spiPending = false;
}

static void WriteCommand(uint8_t command)
{
	SpiWait();
	PORT->Group[1].OUTCLR.reg = TFT_DC_MASK;
	SpiWrite(command);
	SpiWait();
	PORT->Group[1].OUTSET.reg = TFT_DC_MASK;
}

// ===========================================================================
// Display
// ===========================================================================

#define ST7735_DELAY 0x80

//The init of the Gamebuino library's ST7735 driver: command, argument count (with ST7735_DELAY
//a delay in ms follows the arguments), arguments. MADCTL is the rotation that library uses,
//the display 160 wide and 128 high
static const uint8_t displayResetCommands[] = {
	2,
	0x01, ST7735_DELAY, 150,        //software reset
	0x11, ST7735_DELAY, 150,        //out of sleep mode
};
static const uint8_t displayInitCommands[] = {
	19,
	0xB1, 3,0x01, 0x2C, 0x2D,      //frame rate, normal mode
	0xB2, 3, 0x01, 0x2C, 0x2D,      //frame rate, idle mode
	0xB3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,  //frame rate, partial mode
	0xB4, 1, 0x07,                  //no inversion
	0xC0, 3, 0xA2, 0x02, 0x84,      //power control
	0xC1, 1, 0xC5,
	0xC2, 2, 0x0A, 0x00,
	0xC3, 2, 0x8A, 0x2A,
	0xC4, 2, 0x8A, 0xEE,
	0xC5, 1, 0x0E,
	0x20, 0,                        //inversion off
	0x3A, 1, 0x05,                  //16 bit colour
	0x2A, 4, 0x00, 0x00, 0x00, 0x7F,
	0x2B, 4, 0x00, 0x00, 0x00, 0x9F,
	0xE0, 16, 0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
	0xE1, 16, 0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
	0x13, 0,                        //normal display on
	0x29, 0,                        //display on
	0x36, 1, 0x60,                  //MADCTL: MX | MV, RGB order
};

static void RunCommands(const uint8_t* list)
{
	uint8_t count = *list++;
	SPI.beginTransaction(SPISettings(12000000, MSBFIRST, SPI_MODE0));
	PORT->Group[1].OUTCLR.reg = TFT_CS_MASK;
	while (count--)
	{
		WriteCommand(*list++);
		uint8_t args = *list++;
		const bool delayAfter = (args & ST7735_DELAY) != 0;
		args &= ~ST7735_DELAY;
		while (args--)
			SpiWrite(*list++);
		if (delayAfter)
		{
			SpiWait();
			delay(*list++);
		}
	}
	SpiWait();
	PORT->Group[1].OUTSET.reg = TFT_CS_MASK;
	SPI.endTransaction();
}

//the area of the display itself the pixels written next fill, both corners included
static void SetDisplayWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
	WriteCommand(0x2A);
	SpiWrite(0);
	SpiWrite(x0);
	SpiWrite(0);
	SpiWrite(x1);
	WriteCommand(0x2B);
	SpiWrite(0);
	SpiWrite(y0);
	SpiWrite(0);
	SpiWrite(y1);
	WriteCommand(0x2C);
}

void PlatformGamebuinoDisplay::init(void)
{
	PORT->Group[1].DIRSET.reg = TFT_CS_MASK | TFT_DC_MASK;
	PORT->Group[1].OUTSET.reg = TFT_CS_MASK | TFT_DC_MASK;
	SPI.begin();
	//after a software reset (the loader starting the game) the display is already running
	if (!PM->RCAUSE.bit.SYST)
		RunCommands(displayResetCommands);
	RunCommands(displayInitCommands);
	//the strips left and right of the game's screen stay black
	startWrite();
	SetDisplayWindow(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
	for (uint32_t i = 0; i < (uint32_t)DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
	{
		SpiWrite(0);
		SpiWrite(0);
	}
	endWrite();
}

void PlatformGamebuinoDisplay::startWrite(void)
{
	if (writeDepth++ == 0)
	{
		SPI.beginTransaction(SPISettings(24000000, MSBFIRST, SPI_MODE0));
		PORT->Group[1].OUTCLR.reg = TFT_CS_MASK;
	}
}

void PlatformGamebuinoDisplay::endWrite(void)
{
	if (writeDepth && (--writeDepth == 0))
	{
		//A NOP ends the pixel write the display was in. On the device the chip select
		//below is what stops it listening, but the web emulator ignores the chip select
		//and takes every byte on the SPI as display data: without this the buttons and
		//the SD card paint over the pixels drawn last
		WriteCommand(0x00);
		SpiWait();
		PORT->Group[1].OUTSET.reg = TFT_CS_MASK;
		SPI.endTransaction();
	}
}

void PlatformGamebuinoDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	startWrite();
	SetDisplayWindow((uint8_t)(x + DISPLAY_OFFSET_X), (uint8_t)(y + DISPLAY_OFFSET_Y),
	                 (uint8_t)(x + DISPLAY_OFFSET_X + w - 1), (uint8_t)(y + DISPLAY_OFFSET_Y + h - 1));
	endWrite();
}

void PlatformGamebuinoDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
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

void PlatformGamebuinoDisplay::writeColor(uint16_t color, uint32_t length)
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

void PlatformGamebuinoDisplay::writeBytes(const uint8_t* data, uint32_t length)
{
	startWrite();
	while (length--)
		SpiWrite(*data++);
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

void PlatformGamebuinoDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	startWrite();
	setAddrWindow(x, y, w, h);
	writeColor(color, (uint32_t)w * h);
	endWrite();
}

void PlatformGamebuinoGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformGamebuinoGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

//Same character as the one above, but painted as one window: a run of pixels through
//fillRect costs a window of its own, and a window is command bytes the SPI has to drain
//before anything else can follow. A whole line of text was more of a frame than the board
//it was drawn over. A background the same as the text colour still goes the other way,
//there the pixels between the glyph are left as they are
#define FASTCHARSIZE 2   //text this size or smaller is built in the buffer below
//only taken from the heap the first time text is drawn this way, and kept from then on.
//NULL until then, or when that allocation failed, and the run path draws the character
static uint16_t* charCell = NULL;

size_t PlatformGamebuinoDisplay::drawChar(uint16_t c, int32_t x, int32_t y)
{
	const int32_t size = textSize;
	if (c >= 176)
		c++;
	if (c > 255)
		return 6 * size;
	const int32_t w = 6 * size, h = 8 * size;
	//a character that hangs off the screen is left to the clipping the run path does
	if ((textBackground == textColor) || (size > FASTCHARSIZE) ||
		(x < 0) || (y < 0) || (x + w > WINDOW_WIDTH) || (y + h > WINDOW_HEIGHT))
		return PlatformGamebuinoGFX::drawChar(c, x, y);

	if (!charCell)
	{
		charCell = (uint16_t*)malloc(6 * FASTCHARSIZE * 8 * FASTCHARSIZE * sizeof(uint16_t));
		//without it the character still goes out, a window per run of pixels
		if (!charCell)
			return PlatformGamebuinoGFX::drawChar(c, x, y);
	}

	//the cell is filled a glyph pixel at a time, never worked out per screen pixel: the
	//core has no divide instruction, so a division per pixel costs more than the drawing
	const uint8_t* glyph = &platformFont[c * 5];
	uint16_t* d = charCell;
	for (int32_t glyphRow = 0; glyphRow < 8; glyphRow++)
	{
		const uint16_t* rowStart = d;
		for (int32_t glyphCol = 0; glyphCol < 5; glyphCol++)
		{
			const uint16_t color = (((glyph[glyphCol] >> glyphRow) & 1) != 0) ? textColor : textBackground;
			for (int32_t i = 0; i < size; i++)
				*d++ = color;
		}
		//the sixth column is the space between characters
		for (int32_t i = 0; i < size; i++)
			*d++ = textBackground;
		//a scaled cell repeats the row it just built
		for (int32_t i = 1; i < size; i++, d += w)
			memcpy(d, rowStart, w * sizeof(uint16_t));
	}
	startWrite();
	setAddrWindow(x, y, w, h);
	writePixels(charCell, w * h, true);
	endWrite();
	return 6 * size;
}

bool PlatformGamebuinoBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h;
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformGamebuinoBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!pixels || !ClipRect(x, y, w, h))
		return;
#if SCREENBUFFER == 8
	(void)depth;
	const uint8_t value = (uint8_t)(((color & 0xE000) >> 8) | ((color & 0x0700) >> 6) | ((color & 0x0018) >> 3));
	for (int32_t row = y; row < y + h; row++)
		memset(&pixels[row * WINDOW_WIDTH + x], value, w);
#elif SCREENBUFFER == 1
	(void)depth;
	for (int32_t row = y; row < y + h; row++)
		for (int32_t column = x; column < x + w; column++)
			SetBufferBit(pixels, column, row, color);
#else
	(void)depth;
	(void)color;
#endif
}

void Platform_Init(const char* appName)
{
	//the other SPI devices stay deselected while the display starts
	pinMode(BUTTONS_CS_PIN, OUTPUT);
	digitalWrite(BUTTONS_CS_PIN, HIGH);
	pinMode(SD_CS_PIN, OUTPUT);
	digitalWrite(SD_CS_PIN, HIGH);
	display.init();
	StorageInit(appName);
	AudioInit();
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
	//last, the stack that is free from here on is what Platform_FreeStack measures
	PaintStack();
}

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	bufferSetColor[0] = (uint8_t)(setColor >> 8);
	bufferSetColor[1] = (uint8_t)setColor;
	bufferClearColor[0] = (uint8_t)(clearColor >> 8);
	bufferClearColor[1] = (uint8_t)clearColor;
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
	//a row at a time, turned into the bytes the display takes
	uint8_t line[WINDOW_WIDTH * 2];
	display.startWrite();
	display.setAddrWindow(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	for (int16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		uint8_t* dst = line;
  #if SCREENBUFFER == 1
		//a byte of the buffer at a time, most significant bit first as SetBufferBit writes
		for (int16_t x = 0; x < WINDOW_WIDTH; x += 8)
		{
			uint8_t bits = *src++;
			for (uint8_t b = 0; b < 8; b++, bits <<= 1)
			{
				const uint8_t* color = (bits & 0x80) ? bufferSetColor : bufferClearColor;
				*dst++ = color[0];
				*dst++ = color[1];
			}
		}
  #else
		for (int16_t x = 0; x < WINDOW_WIDTH; x++)
		{
			const uint8_t* color = &bufferPalette[*src++ * 2];
			*dst++ = color[0];
			*dst++ = color[1];
		}
  #endif
		display.writeBytes(line, sizeof(line));
	}
	display.endWrite();
#endif
}

// ===========================================================================
// Buttons
// ===========================================================================

//the Gamebuino loader's entry in the bootloader's table
static void GoToLoader(void)
{
	NVIC_DisableIRQ(TC5_IRQn);
	((void(*)(void))(*((uint32_t*)0x3FF4)))();
}

uint8_t Platform_GetButtons(void)
{
	SPI.beginTransaction(SPISettings(12000000, MSBFIRST, SPI_MODE0));
	digitalWrite(BUTTONS_CS_PIN, LOW);
	//the shift register's load input needs a moment
	delayMicroseconds(1);
	//a bit per button, 0 while it is held: down, left, right, up, A, B, MENU, HOME
	const uint8_t held = (uint8_t)~SPI.transfer(1);
	digitalWrite(BUTTONS_CS_PIN, HIGH);
	SPI.endTransaction();

	uint8_t buttons = 0;
	if (held & 0x01)
		buttons |= BUTTON_DOWN;
	if (held & 0x02)
		buttons |= BUTTON_LEFT;
	if (held & 0x04)
		buttons |= BUTTON_RIGHT;
	if (held & 0x08)
		buttons |= BUTTON_UP;
	if (held & 0x10)
		buttons |= BUTTON_A;
	if (held & 0x20)
		buttons |= BUTTON_B;
	if (held & 0x40)
		buttons |= BUTTON_L;
	if (held & 0x80)
		buttons |= BUTTON_R;

	//HOME held long enough leaves the game
	static uint32_t homeSince = 0;
	if (held & 0x80)
	{
		if (!homeSince)
			homeSince = millis() | 1;
		else if (millis() - homeSince >= HOME_HOLD_MS)
			GoToLoader();
	}
	else
		homeSince = 0;
	return buttons;
}

// ===========================================================================
// Sound: a square wave on the DAC, made in the TC5 timer interrupt
// ===========================================================================

#define AUDIO_RATE 11025
//peak of the square wave around the middle of the DAC's 0..1023
#define AUDIO_AMPLITUDE 96
//the level the wave swings around, reached a step per sample so starting and stopping do not pop
#define AUDIO_MIDDLE 512

static volatile uint32_t toneHalfWave = 0;     //samples per half wave, 0 is silence
static volatile uint32_t toneSamplesLeft = 0;  //samples until the tone stops, unless it plays on
static volatile bool tonePlaysOn = false;

extern "C" void TC5_Handler(void)
{
	static uint32_t halfWaveCount = 0;
	static bool high = false;
	static uint16_t middle = 0;
	int16_t output;
	if (toneHalfWave && (tonePlaysOn || toneSamplesLeft))
	{
		if (middle < AUDIO_MIDDLE)
			middle++;
		//counted and not divided, the M0+ has no division in hardware
		if (++halfWaveCount >= toneHalfWave)
		{
			halfWaveCount = 0;
			high = !high;
		}
		output = (int16_t)middle + (high ? AUDIO_AMPLITUDE : -AUDIO_AMPLITUDE);
		if (output < 0)
			output = 0;
		if (!tonePlaysOn)
			toneSamplesLeft--;
	}
	else
	{
		//silent the output rests at 0, as the Gamebuino library does
		if (middle > 0)
			middle--;
		output = (int16_t)middle;
	}
	//Every sample is written, not only the ones that change the level: the web emulator
	//takes one audio sample per DAC write, and dropping the samples that hold their
	//level collapses the wave into noise. Straight to the register: analogWrite's three
	//syncs and its CTRLA read-modify-write cost more than the rest of this handler
	while (DAC->STATUS.bit.SYNCBUSY)
		;
	DAC->DATA.reg = (uint16_t)output;
	TC5->COUNT16.INTFLAG.bit.MC0 = 1;
}

static void AudioInit(void)
{
	analogWriteResolution(10);
	analogWrite(A0, 0);

	//TC5 counts up to its compare value AUDIO_RATE times a second, as in the Gamebuino library
	GCLK->CLKCTRL.reg = (uint16_t)(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID(GCM_TC4_TC5));
	while (GCLK->STATUS.bit.SYNCBUSY)
		;
	TC5->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
	while (TC5->COUNT16.STATUS.reg & TC_STATUS_SYNCBUSY)
		;
	while (TC5->COUNT16.CTRLA.bit.SWRST)
		;
	TC5->COUNT16.CTRLA.reg |= TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV1;
	TC5->COUNT16.CC[0].reg = (uint16_t)(SystemCoreClock / AUDIO_RATE - 1);
	while (TC5->COUNT16.STATUS.reg & TC_STATUS_SYNCBUSY)
		;
	NVIC_DisableIRQ(TC5_IRQn);
	NVIC_ClearPendingIRQ(TC5_IRQn);
	NVIC_SetPriority(TC5_IRQn, 0);
	NVIC_EnableIRQ(TC5_IRQn);
	TC5->COUNT16.INTENSET.bit.MC0 = 1;
	TC5->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
	while (TC5->COUNT16.STATUS.reg & TC_STATUS_SYNCBUSY)
		;
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	//a frequency of 0 is a rest
	uint32_t halfWave = freq ? (uint32_t)(AUDIO_RATE / (2 * (uint32_t)freq)) : 0;
	if (freq && !halfWave)
		halfWave = 1;
	NVIC_DisableIRQ(TC5_IRQn);
	toneHalfWave = halfWave;
	tonePlaysOn = (duration == 0);
	toneSamplesLeft = (uint32_t)duration * AUDIO_RATE / 1000;
	NVIC_EnableIRQ(TC5_IRQn);
}

// ===========================================================================
// Time and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return micros();
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
	//the time since power on hardly differs from one start to the next, the noise on two
	//unconnected analog inputs does (A0 is the speaker), as the Gamebuino library does it
	return (micros() * micros()) ^ ((uint32_t)analogRead(1) * analogRead(2)) ^ ((uint32_t)analogRead(3) << 16);
}

void Platform_Log(const char* format, ...)
{
	char text[128];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	//only to a computer that has the USB serial port open, it does not wait for one
	if (SerialUSB)
		SerialUSB.print(text);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in RAM and in the file /<Game>_embedded/<Game>_embedded.sav on
// the SD card, named after the first word of the game's name. The loader gives every game a folder
// of its own and there are other games called Sokoban and Waternet already, so the folder carries
// the name of this repository and sits beside theirs instead of on top of them. Bytes the file does not have yet read as 0xFF,
// the way erased flash does on the ESPboy, so the game sees a never saved store. Without a
// card the game still runs, it just does not keep anything.
// ===========================================================================

static SdFat sd;
static bool sdReady = false;
static uint8_t storage[PLATFORM_STORAGE_SIZE];
static char storagePath[64] = "/game_embedded/game_embedded.sav";

static void StorageInit(const char* appName)
{
	memset(storage, 0xFF, sizeof(storage));
	size_t n = 0;
	while (appName[n] && (appName[n] != ' ') && (n < 16))
		n++;
	if (n)
		snprintf(storagePath, sizeof(storagePath), "/%.*s_embedded/%.*s_embedded.sav",
		         (int)n, appName, (int)n, appName);

	sdReady = sd.begin(SD_CS_PIN, SD_SCK_MHZ(12));
	if (!sdReady)
	{
		Platform_Log("no SD card, nothing is saved\n");
		return;
	}
	File file = sd.open(storagePath, O_RDONLY);
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
	//storing what is already there does not touch the card
	if (memcmp(storage + offset, data, length) == 0)
		return;
	memcpy(storage + offset, data, length);
	if (!sdReady)
		return;
	//the folder is the part of the path before the file name
	char folder[sizeof(storagePath)];
	strcpy(folder, storagePath);
	char* slash = strrchr(folder, '/');
	if (slash && (slash != folder))
	{
		*slash = '\0';
		if (!sd.exists(folder))
			sd.mkdir(folder);
	}
	File file = sd.open(storagePath, O_WRONLY | O_CREAT | O_TRUNC);
	if (!file)
	{
		Platform_Log("could not write %s\n", storagePath);
		return;
	}
	file.write(storage, sizeof(storage));
	file.close();
}

#endif
