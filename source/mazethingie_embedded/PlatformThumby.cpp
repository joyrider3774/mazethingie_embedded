//Platform.h for the TinyCircuits Thumby Color, built with the arduino-pico core (board: Generic
//RP2350, chip variant RP2350A, flash size 16 MB). The hardware, as CircuitPython's board files and
//TinyCircuits' own game engine have it:
//  display  GC9107 128x128 on SPI0: SCK 18, MOSI 19, chip select 17, data/command 16, reset 4,
//           backlight 7 (PWM)
//  buttons  one GPIO each, low while held: left 0, up 1, right 2, down 3, A 21, B 25, left bumper 6,
//           right bumper 22, menu 26
//  sound    a speaker on pin 23, switched on with 20
//  saves    arduino-pico's EEPROM, a sector of the flash
//
//Buttons: d-pad, A, B, left bumper = L, right bumper = R. The menu button is not used by the game.
//
//The display is 128x128, the size of the game's own screen, so a frame goes out 1:1 with nothing to
//scale or centre. With a screen buffer the second core sends the frames to the display:
//Platform_PresentFrame on the first core only hands a copy over, so the game does not wait for the
//SPI transfer.
//
//The display goes through the Pico SDK's SPI functions and not the SPI library, the same way as on
//the PicoSystem: what the game needs of it is a few calls, and this keeps the pins to this file.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_THUMBY

#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <pico/time.h>
#include "PlatformGamebuinoFont.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 128

#define LCD_SPI spi0
//the SPI block runs at half the peripheral clock at most, 75 MHz at the 150 MHz the core is set to,
//and the panel takes 80 MHz
#define LCD_SPI_FREQ 75000000
#define LCD_SCK_PIN 18
#define LCD_MOSI_PIN 19
#define LCD_CS_PIN 17
#define LCD_DC_PIN 16
#define LCD_RESET_PIN 4
#define LCD_BACKLIGHT_PIN 7
#define AUDIO_PIN 23
#define SPEAKER_ENABLE_PIN 20
#define BUTTON_LEFT_PIN 0
#define BUTTON_UP_PIN 1
#define BUTTON_RIGHT_PIN 2
#define BUTTON_DOWN_PIN 3
#define BUTTON_A_PIN 21
#define BUTTON_B_PIN 25
#define BUTTON_LB_PIN 6
#define BUTTON_RB_PIN 22

static PlatformThumbyDisplay display;
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
// would cost a call and a wait for every one of the 32 KB of a frame
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

void PlatformThumbyDisplay::init(void)
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
	spi_init(LCD_SPI, LCD_SPI_FREQ);
	gpio_set_function(LCD_SCK_PIN, GPIO_FUNC_SPI);
	gpio_set_function(LCD_MOSI_PIN, GPIO_FUNC_SPI);

	//a reset pulse, then the init TinyCircuits' own game engine gives the GC9107
	gpio_put(LCD_RESET_PIN, 0);
	delay(10);
	gpio_put(LCD_RESET_PIN, 1);
	delay(120);
	SendCommand(0xFE);                                  //inter register enable 1
	SendCommand(0xEF);                                  //inter register enable 2
	SendCommand(0xB0, 1, "\xc0");
	SendCommand(0xB1, 1, "\x80");
	SendCommand(0xB2, 1, "\x2f");
	SendCommand(0xB3, 1, "\x03");
	SendCommand(0xB7, 1, "\x01");
	SendCommand(0xB6, 1, "\x19");
	SendCommand(0xAC, 1, "\xc8");                       //RGB565 the usual way round
	SendCommand(0xAB, 1, "\x0f");
	SendCommand(0x3A, 1, "\x05");                       //16 bits per pixel
	SendCommand(0xB4, 1, "\x04");
	SendCommand(0xA8, 1, "\x07");                       //frame rate
	SendCommand(0xB8, 1, "\x08");
	SendCommand(0xE7, 1, "\x5a");                       //VREG control
	SendCommand(0xE8, 1, "\x23");                       //VGH
	SendCommand(0xE9, 1, "\x47");                       //VGL
	SendCommand(0xEA, 1, "\x99");                       //VGH/VGL clock
	SendCommand(0xC6, 1, "\x30");
	SendCommand(0xC7, 1, "\x1f");
	SendCommand(0xF0, 14, "\x05\x1D\x51\x2F\x85\x2A\x11\x62\x00\x07\x07\x0F\x08\x1F");   //gamma 1
	SendCommand(0xF1, 14, "\x2E\x41\x62\x56\xA5\x3A\x3f\x60\x0F\x07\x0A\x18\x18\x1D");   //gamma 2
	SendCommand(0x11);                                  //out of sleep mode
	delay(120);
	SendCommand(0x29);                                  //display on
	delay(10);

	//black until the game draws
	startWrite();
	SetDisplayWindow(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
	writeColor(0x0000, (uint32_t)DISPLAY_WIDTH * DISPLAY_HEIGHT);
	endWrite();

	//the backlight on PWM, with a gamma so the setting's steps look about even
	gpio_set_function(LCD_BACKLIGHT_PIN, GPIO_FUNC_PWM);
	const uint slice = pwm_gpio_to_slice_num(LCD_BACKLIGHT_PIN);
	pwm_set_wrap(slice, 255);
	pwm_set_gpio_level(LCD_BACKLIGHT_PIN, (uint16_t)(powf(BACKLIGHT / 255.0f, 2.8f) * 255.0f + 0.5f));
	pwm_set_enabled(slice, true);
}

void PlatformThumbyDisplay::startWrite(void)
{
	if (writeDepth++ == 0)
		gpio_put(LCD_CS_PIN, 0);
}

void PlatformThumbyDisplay::endWrite(void)
{
	if (writeDepth && (--writeDepth == 0))
	{
		SpiFlush();
		gpio_put(LCD_CS_PIN, 1);
	}
}

void PlatformThumbyDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	startWrite();
	SetDisplayWindow((uint16_t)x, (uint16_t)y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
	endWrite();
}

void PlatformThumbyDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
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

void PlatformThumbyDisplay::writeColor(uint16_t color, uint32_t length)
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

void PlatformThumbyDisplay::writeBytes(const uint8_t* data, uint32_t length)
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

void PlatformThumbyDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	startWrite();
	setAddrWindow(x, y, w, h);
	writeColor(color, (uint32_t)w * h);
	endWrite();
}

void PlatformThumbyGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformThumbyGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

bool PlatformThumbyBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformThumbyBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

static void AudioInit(void);

void Platform_Init(const char* appName)
{
	(void)appName;
	display.init();

	//every button pulls its pin low
	const uint8_t buttonPins[] = { BUTTON_UP_PIN, BUTTON_DOWN_PIN, BUTTON_LEFT_PIN, BUTTON_RIGHT_PIN,
	                               BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_LB_PIN, BUTTON_RB_PIN };
	for (uint8_t i = 0; i < sizeof(buttonPins); i++)
		pinMode(buttonPins[i], INPUT_PULLUP);

	AudioInit();

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
//just the player moves that is a few hundred bytes instead of the whole frame
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
		SetDisplayWindow(first, (uint16_t)runStart, last, (uint16_t)(y - 1));
		for (int16_t sy = runStart; sy < y; sy++)
		{
			const uint8_t* row = &src[sy * BUFFER_ROW_BYTES];
			uint8_t* dst = line;
			for (uint16_t sx = first; sx <= last; sx++)
			{
				const uint8_t* pixel = PixelBytes(row, sx);
				*dst++ = pixel[0];
				*dst++ = pixel[1];
			}
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
	if (!digitalRead(BUTTON_LB_PIN))
		buttons |= BUTTON_L;
	if (!digitalRead(BUTTON_RB_PIN))
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

//The speaker pin is PWM far above what can be heard, working as a DAC: the speaker only follows
//its average. A tone is that level switched between 0 and TONE_LEVEL every half period by a timer,
//so the volume is the height of the square wave. A pulse width at the tone's own frequency hardly
//changes how loud it is. arduino-pico's tone() would take a PIO state machine as well
#define AUDIO_PWM_TOP 1023
#define TONE_LEVEL ((uint16_t)((AUDIO_PWM_TOP + 1) * SOUNDVOLUME / 100))

static repeating_timer_t toneTimer;
static volatile bool tonePlaying = false;
//half periods left before the tone ends, 0 plays it until the next tone is played or it is stopped
static volatile uint32_t toneHalfPeriodsLeft = 0;
static bool toneHigh = false;

static void ToneSilence(void)
{
	pwm_set_gpio_level(AUDIO_PIN, 0);
	gpio_put(SPEAKER_ENABLE_PIN, 0);
}

static void ToneStop(void)
{
	if (tonePlaying)
	{
		cancel_repeating_timer(&toneTimer);
		tonePlaying = false;
	}
	ToneSilence();
}

//every half period of the tone, in the timer interrupt
static bool ToneHalfPeriod(repeating_timer_t* timer)
{
	(void)timer;
	if (toneHalfPeriodsLeft && (--toneHalfPeriodsLeft == 0))
	{
		ToneSilence();
		tonePlaying = false;
		//false: the timer stops
		return false;
	}
	toneHigh = !toneHigh;
	pwm_set_gpio_level(AUDIO_PIN, toneHigh ? TONE_LEVEL : 0);
	return true;
}

static void AudioInit(void)
{
	//silent and with the speaker switched off until a tone plays
	pinMode(SPEAKER_ENABLE_PIN, OUTPUT);
	digitalWrite(SPEAKER_ENABLE_PIN, LOW);
	gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
	const uint slice = pwm_gpio_to_slice_num(AUDIO_PIN);
	//no divider: 150 MHz / 1024 is about 146 kHz
	pwm_set_clkdiv_int_frac4(slice, 1, 0);
	pwm_set_wrap(slice, AUDIO_PWM_TOP);
	pwm_set_gpio_level(AUDIO_PIN, 0);
	pwm_set_enabled(slice, true);
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	ToneStop();
	//a frequency of 0 is a rest
	if (!freq || !TONE_LEVEL)
		return;
	toneHalfPeriodsLeft = 0;
	if (duration)
	{
		//2 half periods per period, duration is in milliseconds
		toneHalfPeriodsLeft = (uint32_t)freq * duration / 500;
		if (!toneHalfPeriodsLeft)
			toneHalfPeriodsLeft = 1;
	}
	toneHigh = true;
	pwm_set_gpio_level(AUDIO_PIN, TONE_LEVEL);
	gpio_put(SPEAKER_ENABLE_PIN, 1);
	//negative: from the start of one call to the next, so the time the interrupt takes does not add up
	tonePlaying = add_repeating_timer_us(-(int64_t)(500000 / freq), ToneHalfPeriod, nullptr, &toneTimer);
	if (!tonePlaying)
		ToneSilence();
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
	//the RP2350's true random number generator
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
