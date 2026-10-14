//Platform.h for the Pimoroni Tufty 2350, built with the arduino-pico core (board: Generic RP2350,
//chip variant RP2350B, PSRAM CS GPIO 8, PSRAM size 8MB, flash size 16MB (no FS)). arduino-pico has no
//Tufty board of its own. The hardware, as Pimoroni's Badgeware board file and the znax_tufty2350
//port have it:
//  display  ST7789 320x240 on an 8 bit parallel bus, on the same pins as the Pimoroni Explorer:
//           data 32-39, write strobe 30, read strobe 31, chip select 27, data/command 28,
//           backlight 26 (PWM). No reset pin. It is mounted the other way round than the Explorer's
//  buttons  one GPIO each, low while held: A 7, B 9, C 10 under the display, UP 11, DOWN 6, HOME
//           (BOOT) 22. RESET resets the chip, see Power
//  sound    none, the Tufty has no speaker
//  saves    arduino-pico's EEPROM, a sector of the flash
//  power    rear LEDs 0-3, the RESET button's level on 14, a PCF85063A RTC on I2C 0 (SDA 4, SCL 5)
//           whose power is switched on with 41, see Power
//
//Buttons, there is no d-pad: UP and DOWN are up and down, A is left, C is right, B is the A button
//and HOME the B button. HOME is also a shift: held, A is the left side button and C the right side
//button. So that a shifted press is not taken for B, a short tap of HOME is B for one frame when it
//is let go, held longer it is B for as long as it is held.
//
//With a screen buffer the second core sends the frames to the display: Platform_PresentFrame on the
//first core only hands a copy over, so the game does not wait for the transfer.
//
//The parallel bus is a PIO program that puts a byte on the data pins and pulses the write strobe,
//fed by DMA. The data pins are above GPIO 31: a PIO block reaches either GPIO 0-31 or 16-47, so the
//one driving the display is moved to 16-47 before its program is loaded.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_TUFTY

#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
//arduino-pico builds hardware_powman into its libpico but leaves its folder off the include path,
//it is found from the hardware_gpio include folder that is on it
#include <../../hardware_powman/include/hardware/powman.h>
#include <hardware/watchdog.h>
#include <hardware/i2c.h>
#include <hardware/resets.h>
#include <hardware/structs/usb.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <math.h>
#include "PlatformGamebuinoFont.h"

static void PowerStartup(void);

#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
//the game's screen sits in the middle, unless SCALESCREEN scales a buffered frame to SCALED_SIZE
#define DISPLAY_OFFSET_X ((DISPLAY_WIDTH - WINDOW_WIDTH) / 2)
#define DISPLAY_OFFSET_Y ((DISPLAY_HEIGHT - WINDOW_HEIGHT) / 2)
//scaled the frame is as high as the display and square, in the middle
#define SCALED_SIZE DISPLAY_HEIGHT
#define SCALED_OFFSET_X ((DISPLAY_WIDTH - SCALED_SIZE) / 2)

#define LCD_PIO pio1
//Pimoroni's own driver runs the parallel bus at up to 32 MHz of PIO clock, 3 PIO steps a byte here
#define LCD_PIO_MAX_HZ 32000000
#define LCD_D0_PIN 32
#define LCD_WR_PIN 30
#define LCD_RD_PIN 31
#define LCD_CS_PIN 27
#define LCD_DC_PIN 28
#define LCD_BACKLIGHT_PIN 26
//the GPIO range the display's PIO block is moved to, see the top of this file
#define LCD_PIO_GPIO_BASE 16
#define BUTTON_A_PIN 7
#define BUTTON_B_PIN 9
#define BUTTON_C_PIN 10
#define BUTTON_UP_PIN 11
#define BUTTON_DOWN_PIN 6
#define BUTTON_HOME_PIN 22
//HOME held at least this long without A or C is the B button held
#define HOME_HOLD_MS 250
//the rear LEDs, in the order the long press sweep lights them
#define LED_0_PIN 0
#define LED_1_PIN 1
#define LED_2_PIN 2
#define LED_3_PIN 3
//the RESET button's level, low while it is held, and the line all the front buttons pull low
#define RESET_BUTTON_PIN 14
#define BUTTONS_INT_PIN 15
#define VBUS_DETECT_PIN 12
#define RTC_ALARM_PIN 13
#define PSRAM_CS_PIN 8
#define RTC_I2C i2c0
#define RTC_I2C_ADDR 0x51
#define RTC_SDA_PIN 4
#define RTC_SCL_PIN 5
#define RTC_POWER_PIN 41

static PlatformTuftyDisplay display;
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
// Parallel bus
//
// Bytes are gathered in a small buffer and handed to DMA a block at a time, as the PicoSystem does
// with its SPI. The PIO program:
//     out pins, 8   side 1    the byte on the data pins, write strobe high
//     nop           side 0    write strobe low
//     nop           side 1    write strobe high, the display takes the byte
// It waits for the next byte at the first step, so the write strobe is high whenever chip select or
// data/command change. Pimoroni's two step program waits with it low instead: chip select going
// high then counts as one more write of the last byte, every frame, and the display's write position
// creeps on a pixel at a time, drawing garbage over the game.
// It shifts out the top 8 bits of each word: an 8 bit write to the FIFO register is copied to all
// 4 bytes of the word, so DMA can hand it single bytes
// ===========================================================================

static const uint16_t parallelInstructions[] = { 0x7008, 0xA042, 0xB042 };
static const pio_program_t parallelProgram = { parallelInstructions, 3, -1 };
static uint lcdSm = 0;
static uint lcdDma = 0;

static uint8_t busBuffer[512];
static uint16_t busUsed = 0;

//hands the bytes to the state machine and returns once DMA has put all of them in its FIFO, the data
//can change from then on. The last few may still be going out, BusDrain waits for those
static void BusSend(const uint8_t* data, uint32_t length)
{
	dma_channel_set_read_addr(lcdDma, data, false);
	dma_channel_set_transfer_count(lcdDma, dma_encode_transfer_count(length), true);
	dma_channel_wait_for_finish_blocking(lcdDma);
}

static void BusFlush(void)
{
	if (busUsed)
	{
		BusSend(busBuffer, busUsed);
		busUsed = 0;
	}
}

//Returns once every byte sent is on the display, before data/command or chip select change. The
//state machine stalls when it finds its FIFO empty, the stall flag is cleared and waited for
//again so a stall from before the last byte does not count
static void BusDrain(void)
{
	BusFlush();
	const uint32_t stalled = 1u << (PIO_FDEBUG_TXSTALL_LSB + lcdSm);
	while (!pio_sm_is_tx_fifo_empty(LCD_PIO, lcdSm))
		tight_loop_contents();
	LCD_PIO->fdebug = stalled;
	while (!(LCD_PIO->fdebug & stalled))
		tight_loop_contents();
}

static inline void BusWrite(uint8_t value)
{
	busBuffer[busUsed++] = value;
	if (busUsed == sizeof(busBuffer))
		BusFlush();
}

static void WriteCommand(uint8_t command)
{
	BusDrain();
	gpio_put(LCD_DC_PIN, 0);
	BusWrite(command);
	BusDrain();
	gpio_put(LCD_DC_PIN, 1);
}

//a command and its arguments in a transaction of its own, for the init
static void SendCommand(uint8_t command, uint8_t argCount = 0, const char* args = nullptr)
{
	gpio_put(LCD_CS_PIN, 0);
	WriteCommand(command);
	for (uint8_t i = 0; i < argCount; i++)
		BusWrite((uint8_t)args[i]);
	BusDrain();
	gpio_put(LCD_CS_PIN, 1);
}

static void BusInit(void)
{
	gpio_init(LCD_CS_PIN);
	gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
	gpio_put(LCD_CS_PIN, 1);
	gpio_init(LCD_DC_PIN);
	gpio_set_dir(LCD_DC_PIN, GPIO_OUT);
	gpio_put(LCD_DC_PIN, 1);
	//nothing is read from the display
	gpio_init(LCD_RD_PIN);
	gpio_set_dir(LCD_RD_PIN, GPIO_OUT);
	gpio_put(LCD_RD_PIN, 1);

	//only possible while the block has no program loaded yet
	if (pio_set_gpio_base(LCD_PIO, LCD_PIO_GPIO_BASE) != PICO_OK)
		Platform_Log("the display's PIO block can not reach its pins\n");
	lcdSm = (uint)pio_claim_unused_sm(LCD_PIO, true);
	const uint offset = (uint)pio_add_program(LCD_PIO, &parallelProgram);

	pio_sm_config config = pio_get_default_sm_config();
	sm_config_set_wrap(&config, offset, offset + 2);
	sm_config_set_sideset(&config, 1, false, false);
	sm_config_set_out_pins(&config, LCD_D0_PIN, 8);
	sm_config_set_sideset_pins(&config, LCD_WR_PIN);
	//the state machine only takes, both FIFOs are its input
	sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
	//most significant bit first, a new word every 8 bits
	sm_config_set_out_shift(&config, false, true, 8);
	//a whole divider, a fraction would make the strobe uneven
	const uint32_t sysHz = clock_get_hz(clk_sys);
	sm_config_set_clkdiv_int_frac8(&config, (sysHz + LCD_PIO_MAX_HZ - 1) / LCD_PIO_MAX_HZ, 0);
	pio_sm_init(LCD_PIO, lcdSm, offset, &config);

	pio_gpio_init(LCD_PIO, LCD_WR_PIN);
	for (uint8_t i = 0; i < 8; i++)
		pio_gpio_init(LCD_PIO, LCD_D0_PIN + i);
	pio_sm_set_consecutive_pindirs(LCD_PIO, lcdSm, LCD_WR_PIN, 1, true);
	pio_sm_set_consecutive_pindirs(LCD_PIO, lcdSm, LCD_D0_PIN, 8, true);
	pio_sm_set_enabled(LCD_PIO, lcdSm, true);

	lcdDma = (uint)dma_claim_unused_channel(true);
	dma_channel_config dmaConfig = dma_channel_get_default_config(lcdDma);
	channel_config_set_transfer_data_size(&dmaConfig, DMA_SIZE_8);
	channel_config_set_dreq(&dmaConfig, pio_get_dreq(LCD_PIO, lcdSm, true));
	//written to the state machine's FIFO, what is read is set for every transfer
	dma_channel_configure(lcdDma, &dmaConfig, &LCD_PIO->txf[lcdSm], nullptr, 0, false);
}

// ===========================================================================
// Display
// ===========================================================================

//the area of the display itself the pixels written next fill, both corners included
static void SetDisplayWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	WriteCommand(0x2A);
	BusWrite((uint8_t)(x0 >> 8));
	BusWrite((uint8_t)x0);
	BusWrite((uint8_t)(x1 >> 8));
	BusWrite((uint8_t)x1);
	WriteCommand(0x2B);
	BusWrite((uint8_t)(y0 >> 8));
	BusWrite((uint8_t)y0);
	BusWrite((uint8_t)(y1 >> 8));
	BusWrite((uint8_t)y1);
	WriteCommand(0x2C);
}

void PlatformTuftyDisplay::init(void)
{
	//dark until the display shows something: PWM with a 16 bit period
	gpio_set_function(LCD_BACKLIGHT_PIN, GPIO_FUNC_PWM);
	const uint backlightSlice = pwm_gpio_to_slice_num(LCD_BACKLIGHT_PIN);
	pwm_set_wrap(backlightSlice, 65535);
	pwm_set_gpio_level(LCD_BACKLIGHT_PIN, 0);
	pwm_set_enabled(backlightSlice, true);
	BusInit();

	//there is no reset pin, a software reset and then the init Pimoroni's driver uses for the
	//Explorer's display, the same panel, without its gamma tables
	SendCommand(0x01);                                  //software reset
	delay(150);
	SendCommand(0x35, 1, "\x00");                       //tearing effect line on
	SendCommand(0x3A, 1, "\x05");                       //16 bits per pixel
	SendCommand(0xB2, 5, "\x0c\x0c\x00\x33\x33");       //porch control
	SendCommand(0xC0, 1, "\x2c");                       //LCM control
	SendCommand(0xC2, 1, "\x01");                       //VDV and VRH from the commands
	SendCommand(0xC3, 1, "\x12");                       //VRH
	SendCommand(0xC4, 1, "\x20");                       //VDV
	SendCommand(0xD0, 2, "\xa4\xa1");                   //power control
	SendCommand(0xC6, 1, "\x0f");                       //frame rate 60 Hz
	SendCommand(0xB0, 2, "\x00\xc0");                   //RAM control, pixels high byte first
	SendCommand(0xB7, 1, "\x35");                       //gate control
	SendCommand(0xBB, 1, "\x1f");                       //VCOM
	SendCommand(0x36, 1, "\xa0");                       //MADCTL: 320 wide and 240 high, turned 180 degrees
	SendCommand(0x21);                                  //inversion on, the panel needs it
	SendCommand(0x11);                                  //out of sleep mode
	delay(120);
	SendCommand(0x29);                                  //display on

	//black until the game draws, the border around the game stays that way
	startWrite();
	SetDisplayWindow(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
	writeColor(0x0000, (uint32_t)DISPLAY_WIDTH * DISPLAY_HEIGHT);
	endWrite();

	pwm_set_gpio_level(LCD_BACKLIGHT_PIN, (uint16_t)(pow(BACKLIGHT / 255.0f, 2.8f) * 65535.0f + 0.5f));
}

void PlatformTuftyDisplay::startWrite(void)
{
	if (writeDepth++ == 0)
		gpio_put(LCD_CS_PIN, 0);
}

void PlatformTuftyDisplay::endWrite(void)
{
	if (writeDepth && (--writeDepth == 0))
	{
		BusDrain();
		gpio_put(LCD_CS_PIN, 1);
	}
}

void PlatformTuftyDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	startWrite();
	SetDisplayWindow((uint16_t)(x + DISPLAY_OFFSET_X), (uint16_t)(y + DISPLAY_OFFSET_Y),
	                 (uint16_t)(x + DISPLAY_OFFSET_X + w - 1), (uint16_t)(y + DISPLAY_OFFSET_Y + h - 1));
	endWrite();
}

void PlatformTuftyDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	startWrite();
	if (swap)
	{
		for (int32_t i = 0; i < length; i++)
		{
			BusWrite((uint8_t)(data[i] >> 8));
			BusWrite((uint8_t)data[i]);
		}
	}
	else
	{
		for (int32_t i = 0; i < length; i++)
		{
			BusWrite((uint8_t)data[i]);
			BusWrite((uint8_t)(data[i] >> 8));
		}
	}
	endWrite();
}

void PlatformTuftyDisplay::writeColor(uint16_t color, uint32_t length)
{
	const uint8_t high = (uint8_t)(color >> 8), low = (uint8_t)color;
	startWrite();
	while (length--)
	{
		BusWrite(high);
		BusWrite(low);
	}
	endWrite();
}

void PlatformTuftyDisplay::writeBytes(const uint8_t* data, uint32_t length)
{
	startWrite();
	//a block this big goes to DMA as it is, no need to copy it into the bus buffer first
	if (length >= 64)
	{
		BusFlush();
		BusSend(data, length);
	}
	else
	{
		while (length--)
			BusWrite(*data++);
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

void PlatformTuftyDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	startWrite();
	setAddrWindow(x, y, w, h);
	writeColor(color, (uint32_t)w * h);
	endWrite();
}

void PlatformTuftyGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformTuftyGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

bool PlatformTuftyBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformTuftyBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
	//first, a long press of RESET goes to sleep before anything shows
	PowerStartup();
	display.init();

	//every button pulls its pin low
	const uint8_t buttonPins[] = { BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_C_PIN, BUTTON_UP_PIN, BUTTON_DOWN_PIN, BUTTON_HOME_PIN };
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
//one ends. 1:1 in the middle of the display, or with SCALESCREEN nearest neighbour over 240x240 in
//its middle: 240 / 128 is not a whole number, so a game pixel is 1 or 2 display pixels wide
static uint16_t displayColumn[WINDOW_WIDTH + 1], displayRow[WINDOW_HEIGHT + 1];

static void MapDisplay(void)
{
	for (uint16_t x = 0; x <= WINDOW_WIDTH; x++)
  #if SCALESCREEN
		displayColumn[x] = (uint16_t)(SCALED_OFFSET_X + (x * SCALED_SIZE + WINDOW_WIDTH - 1) / WINDOW_WIDTH);
  #else
		displayColumn[x] = (uint16_t)(DISPLAY_OFFSET_X + x);
  #endif
	for (uint16_t y = 0; y <= WINDOW_HEIGHT; y++)
  #if SCALESCREEN
		displayRow[y] = (uint16_t)((y * SCALED_SIZE + WINDOW_HEIGHT - 1) / WINDOW_HEIGHT);
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
	if (!digitalRead(BUTTON_UP_PIN))
		buttons |= BUTTON_UP;
	if (!digitalRead(BUTTON_DOWN_PIN))
		buttons |= BUTTON_DOWN;
	if (!digitalRead(BUTTON_B_PIN))
		buttons |= BUTTON_A;
	const bool left = !digitalRead(BUTTON_A_PIN);
	const bool right = !digitalRead(BUTTON_C_PIN);
	const bool home = !digitalRead(BUTTON_HOME_PIN);

	//when HOME went down, whether A or C was pressed with it since, and whether it was down last time
	static uint32_t homeSince = 0;
	static bool homeShifted = false;
	static bool homeWasDown = false;
	if (home)
	{
		if (!homeWasDown)
		{
			homeSince = millis();
			homeShifted = false;
		}
		//held, left and right are the side buttons instead
		if (left)
		{
			buttons |= BUTTON_L;
			homeShifted = true;
		}
		if (right)
		{
			buttons |= BUTTON_R;
			homeShifted = true;
		}
		if (!homeShifted && (millis() - homeSince >= HOME_HOLD_MS))
			buttons |= BUTTON_B;
	}
	else
	{
		if (left)
			buttons |= BUTTON_LEFT;
		if (right)
			buttons |= BUTTON_RIGHT;
		//a short tap of HOME alone is B for this one frame
		if (homeWasDown && !homeShifted && (millis() - homeSince < HOME_HOLD_MS))
			buttons |= BUTTON_B;
	}
	homeWasDown = home;
	return buttons;
}

// ===========================================================================
// Power
//
// The Tufty's power management the way Pimoroni's Badgeware firmware does it, as the znax_tufty2350
// port has it. RESET resets the chip, so PowerStartup runs at every start, from Platform_Init:
//  - after a reset by the button, RESET still held: the rear LEDs sweep on and fade out. Held until
//    they are dark the Tufty goes to sleep, a front button or the RTC wakes it up again. With UP and
//    DOWN held as well it goes into shipping mode instead, where only RESET or USB power bring it
//    back. Let go earlier the game just starts
//  - after a reset by the button the double tap flag is set for a second, so a second tap of RESET
//    within it is seen as a double tap
//  - the RTC's timer interrupt is switched off, it would otherwise keep waking the Tufty
// ===========================================================================

#define POWER_DOUBLE_TAP_MS 1000
//the long press LED sweep: peak brightness, the delay from one LED to the next and while fading
//out, how much faster they fade out, the gamma, the time per step and the steps before they start
#define LED_PEAK_BRIGHTNESS 150
#define LED_IN_PHASE 30
#define LED_OUT_PHASE 0
#define LED_TOTAL (LED_PEAK_BRIGHTNESS + LED_IN_PHASE * 3)
#define LED_FADEOUT_SPEED 5
#define LED_GAMMA 1.8f
#define LED_DELAY_MS 5
#define LED_ON_DELAY (200 / LED_DELAY_MS)

static const uint8_t ledPins[4] = { LED_1_PIN, LED_2_PIN, LED_3_PIN, LED_0_PIN };
static powman_power_state powerOffState;
static powman_power_state powerOnState;

static void SetDoubleTapFlag(bool set)
{
	if (set)
		powman_set_bits(&powman_hw->chip_reset, POWMAN_CHIP_RESET_DOUBLE_TAP_BITS);
	else
		powman_clear_bits(&powman_hw->chip_reset, POWMAN_CHIP_RESET_DOUBLE_TAP_BITS);
}

static int64_t ClearDoubleTapFlag(alarm_id_t id, void* user)
{
	(void)id;
	(void)user;
	SetDoubleTapFlag(false);
	return 0;
}

//the RTC's I2C, its power comes up slowly
static void RtcI2CEnable(void)
{
	gpio_init(RTC_POWER_PIN);
	gpio_set_dir(RTC_POWER_PIN, GPIO_OUT);
	gpio_put(RTC_POWER_PIN, 1);
	sleep_ms(500);
	i2c_init(RTC_I2C, 400 * 1000);
	gpio_set_function(RTC_SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(RTC_SCL_PIN, GPIO_FUNC_I2C);
}

static void RtcI2CDisable(void)
{
	gpio_init(RTC_POWER_PIN);
	gpio_init(RTC_SDA_PIN);
	gpio_init(RTC_SCL_PIN);
}

static void RtcDisableInterrupt(void)
{
	//Timer_mode: timer and its interrupt off
	const uint8_t data[2] = { 0x11, 0x00 };
	i2c_write_blocking(RTC_I2C, RTC_I2C_ADDR, data, 2, false);
}

//the front buttons pulled up, and with everything also HOME, the interrupt lines, the LEDs, the RESET
//level and the RTC's power
static void PowerSetupGpio(bool buttonsOnly)
{
	const uint32_t buttonMask = (1u << BUTTON_A_PIN) | (1u << BUTTON_B_PIN) | (1u << BUTTON_C_PIN) |
	                            (1u << BUTTON_UP_PIN) | (1u << BUTTON_DOWN_PIN);
	gpio_init_mask(buttonMask);
	gpio_set_dir_in_masked(buttonMask);
	gpio_set_pulls(BUTTON_A_PIN, true, false);
	gpio_set_pulls(BUTTON_B_PIN, true, false);
	gpio_set_pulls(BUTTON_C_PIN, true, false);
	gpio_set_pulls(BUTTON_UP_PIN, true, false);
	gpio_set_pulls(BUTTON_DOWN_PIN, true, false);
	if (buttonsOnly)
		return;

	gpio_init(BUTTON_HOME_PIN);
	gpio_set_dir(BUTTON_HOME_PIN, GPIO_IN);
	gpio_set_pulls(BUTTON_HOME_PIN, true, false);
	gpio_init(BUTTONS_INT_PIN);
	gpio_set_dir(BUTTONS_INT_PIN, GPIO_IN);
	gpio_set_pulls(BUTTONS_INT_PIN, true, false);
	gpio_init(RTC_ALARM_PIN);
	gpio_set_dir(RTC_ALARM_PIN, GPIO_IN);
	gpio_set_pulls(RTC_ALARM_PIN, true, false);
	gpio_init(VBUS_DETECT_PIN);
	gpio_set_dir(VBUS_DETECT_PIN, GPIO_IN);
	gpio_set_pulls(VBUS_DETECT_PIN, false, false);

	//the LEDs off
	const uint32_t ledMask = (1u << LED_0_PIN) | (1u << LED_1_PIN) | (1u << LED_2_PIN) | (1u << LED_3_PIN);
	gpio_init_mask(ledMask);
	gpio_set_dir_out_masked(ledMask);
	gpio_put_masked(ledMask, 0);

	gpio_init(RESET_BUTTON_PIN);
	gpio_set_dir(RESET_BUTTON_PIN, GPIO_IN);
	gpio_pull_up(RESET_BUTTON_PIN);

	gpio_init(RTC_POWER_PIN);
	gpio_set_dir(RTC_POWER_PIN, GPIO_OUT);
	gpio_put(RTC_POWER_PIN, 1);
}

//everything that draws power set up for powering off: clocks from the USB PLL, every pin an input
//with a pull that keeps it quiet, the USB PHY off, the power manager's timer running and the power
//states to switch between
static void PowerPrepareOff(void)
{
	SetDoubleTapFlag(false);
	set_sys_clock_48mhz();

	for (int i = 0; i < (int)NUM_BANK0_GPIOS; ++i)
	{
		gpio_set_function(i, GPIO_FUNC_SIO);
		gpio_set_dir(i, GPIO_IN);
		gpio_set_input_enabled(i, false);
		switch (i)
		{
			case PSRAM_CS_PIN:
				gpio_set_pulls(i, true, false);
				break;
			case RESET_BUTTON_PIN:
			case BUTTON_HOME_PIN:
			case 40:
			case RTC_POWER_PIN:
			case 42:
				gpio_disable_pulls(i);
				break;
			case BUTTON_A_PIN:
			case BUTTON_B_PIN:
			case BUTTON_C_PIN:
			case BUTTON_UP_PIN:
			case BUTTON_DOWN_PIN:
				//pulled up, a press has to reach the button interrupt line
				break;
			default:
				gpio_set_pulls(i, false, true);
				break;
		}
	}

	hw_set_bits(&powman_hw->vreg_ctrl, POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);

	reset_block_mask(RESETS_RESET_USBCTRL_BITS);
	unreset_block_mask_wait_blocking(RESETS_RESET_USBCTRL_BITS);
	usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;
	usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;
	usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;
	usb_hw->inte = USB_INTS_BUFF_STATUS_BITS | USB_INTS_BUS_RESET_BITS | USB_INTS_SETUP_REQ_BITS |
	               USB_INTS_DEV_SUSPEND_BITS | USB_INTS_DEV_RESUME_FROM_HOST_BITS | USB_INTS_DEV_CONN_DIS_BITS;
	usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_PD_BITS | USB_USBPHY_DIRECT_RX_PD_BITS |
	                     USB_USBPHY_DIRECT_DM_PULLDN_EN_BITS | USB_USBPHY_DIRECT_DP_PULLDN_EN_BITS;
	usb_hw->phy_direct_override = USB_USBPHY_DIRECT_RX_DM_BITS | USB_USBPHY_DIRECT_RX_DP_BITS | USB_USBPHY_DIRECT_RX_DD_BITS |
		USB_USBPHY_DIRECT_OVERRIDE_TX_DIFFMODE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_OVERRIDE_EN_BITS |
		USB_USBPHY_DIRECT_OVERRIDE_TX_FSSLEW_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_PD_OVERRIDE_EN_BITS |
		USB_USBPHY_DIRECT_OVERRIDE_RX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
		USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS |
		USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLDN_EN_OVERRIDE_EN_BITS |
		USB_USBPHY_DIRECT_OVERRIDE_DP_PULLDN_EN_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_EN_OVERRIDE_EN_BITS |
		USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_HISEL_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_HISEL_OVERRIDE_EN_BITS;

	//2025/05/01 in milliseconds since the epoch
	powman_timer_start();
	powman_timer_set_ms(1746057600000ULL);
	powman_set_debug_power_request_ignored(true);

	//off: every domain down. On: the switched core and the XIP cache
	powerOffState = POWMAN_POWER_STATE_NONE;
	powerOnState = powman_power_state_with_domain_on(POWMAN_POWER_STATE_NONE, POWMAN_POWER_DOMAIN_SWITCHED_CORE);
	powerOnState = powman_power_state_with_domain_on(powerOnState, POWMAN_POWER_DOMAIN_XIP_CACHE);
}

//Powers off, a wake up starts the program from the beginning. In RAM: the flash goes off with the rest
static int __no_inline_not_in_flash_func(PowerOff)(void)
{
	if (!powman_configure_wakeup_state(powerOffState, powerOnState))
		return PICO_ERROR_INVALID_STATE;
	powman_hw->boot[0] = 0;
	powman_hw->boot[1] = 0;
	powman_hw->boot[2] = 0;
	powman_hw->boot[3] = 0;
	const int rc = powman_set_power_state(powerOffState);
	if (rc != PICO_OK)
		return rc;
	while (true)
		__wfi();
}

//gpio waking the Tufty on channel, once it has been idle, for at most a second
static void PowerWakeOnGpio(int channel, int gpio, bool edge, bool high)
{
	gpio_init(gpio);
	gpio_set_dir(gpio, false);
	gpio_set_input_enabled(gpio, true);
	gpio_set_pulls(gpio, !high, high);
	const absolute_time_t timeout = make_timeout_time_ms(1000);
	while ((gpio_get(gpio) == high) && !time_reached(timeout))
		sleep_ms(10);
	powman_enable_gpio_wakeup(channel, gpio, edge, high);
}

//sleep until a front button or the RTC's alarm
static void PowerSleep(void)
{
	PowerPrepareOff();
	PowerSetupGpio(true);
	PowerWakeOnGpio(1, RTC_ALARM_PIN, true, false);
	PowerWakeOnGpio(3, BUTTONS_INT_PIN, true, false);
	RtcI2CDisable();
	PowerOff();
}

//off until RESET or USB power
static void PowerShip(void)
{
	PowerPrepareOff();
	RtcI2CDisable();
	PowerOff();
}

//RESET is still held after the reset: the LED sweep, and sleep or shipping mode when it is held
//until the LEDs are dark again
static void PowerLongPress(void)
{
	pwm_config config = pwm_get_default_config();
	pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / 2048.0f);
	pwm_config_set_wrap(&config, 1024);
	for (uint8_t i = 0; i < 4; i++)
	{
		gpio_set_function(ledPins[i], GPIO_FUNC_PWM);
		pwm_init(pwm_gpio_to_slice_num(ledPins[i]), &config, true);
	}
	int step = 0;
	//let go early, the game starts
	while (!gpio_get(RESET_BUTTON_PIN))
	{
		const int sweep = (step < LED_ON_DELAY) ? 0 : step - LED_ON_DELAY;
		int brightness = (sweep >= LED_TOTAL) ? LED_TOTAL - (sweep - LED_TOTAL) * LED_FADEOUT_SPEED : sweep;
		const int phase = (sweep >= LED_TOTAL) ? LED_OUT_PHASE : LED_IN_PHASE;
		int level = 0;
		for (uint8_t i = 0; i < 4; i++)
		{
			const int v = (brightness < 0) ? 0 : ((brightness > LED_PEAK_BRIGHTNESS) ? LED_PEAK_BRIGHTNESS : brightness);
			pwm_set_gpio_level(ledPins[i], (uint16_t)(v * LED_GAMMA));
			level += (int)(v * LED_GAMMA);
			brightness -= phase;
		}
		if ((sweep > 0) && (level == 0))
		{
			SetDoubleTapFlag(false);
			if (!gpio_get(BUTTON_UP_PIN) && !gpio_get(BUTTON_DOWN_PIN))
				PowerShip();
			else
				PowerSleep();
			break;
		}
		step++;
		sleep_ms(LED_DELAY_MS);
	}
	for (uint8_t i = 0; i < 4; i++)
		pwm_set_enabled(pwm_gpio_to_slice_num(ledPins[i]), false);
	const uint32_t ledMask = (1u << LED_0_PIN) | (1u << LED_1_PIN) | (1u << LED_2_PIN) | (1u << LED_3_PIN);
	gpio_init_mask(ledMask);
	gpio_set_dir_out_masked(ledMask);
	gpio_put_masked(ledMask, 0);
}

static void PowerStartup(void)
{
	PowerSetupGpio(false);
	//a reset by the button, not a power on or the watchdog of an upload
	if ((powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) && !watchdog_caused_reboot())
	{
		if (!(powman_hw->chip_reset & POWMAN_CHIP_RESET_DOUBLE_TAP_BITS))
		{
			SetDoubleTapFlag(true);
			add_alarm_in_ms(POWER_DOUBLE_TAP_MS, ClearDoubleTapFlag, nullptr, false);
			if (!gpio_get(RESET_BUTTON_PIN))
				PowerLongPress();
		}
		else
			SetDoubleTapFlag(false);
	}
	RtcI2CEnable();
	RtcDisableInterrupt();
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return micros();
}

//the Tufty has no speaker
void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	(void)freq;
	(void)duration;
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
