//Platform.h for the CHGame, Kevin Bates' CH32X035 handheld. Built with its board package
//(github.com/bateske/CHGame, 0.3.0 or later), using the Arduino core with its SPI library for the
//display and the board's CHGame library for the buttons and the sound:
//  display  ST7735S 128x128 on SPI1, chip select PA4, data/command PB0, reset PB12
//  buttons  one GPIO each, pulled up and low while held, set up and read by the CHGame library
//  sound    the buzzer on PB10, played by the CHGame library's sound engine, see the sound further
//           down
//  saves    a flash page the bootloader leaves alone, see the saved data further down
//
//The display numbers are the ones CHGfx (github.com/bateske/CHGfx) uses for this panel, which is
//the library written for this board: MADCTL 0xC8 with the picture starting at column 2, row 3.
//
//Buttons: d-pad, A, B, SELECT = left side button, START = right side button.
//
//Two traps of this chip, both of which CHGfx documents and both of which this file stays clear of
//by going through the core rather than the registers:
//  - GPIOB's CFGHR is write only. Reading it back does not return what was written, so the core
//    keeps a RAM shadow and a raw write behind its back is undone by the next pinMode on any pin
//    of that half of the port. Every pin here is set through pinMode and digitalWrite
//  - the reset line drifts below its threshold if it is left alone for about a second, and the
//    panel then takes that for a reset and blanks to white while the game carries on drawing into
//    it. It is driven high again on every window change, which is four instructions against the
//    eleven SPI transfers that follow it

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_CHGAME

#include <stdarg.h>
#include <stdio.h>
#include <malloc.h>
#include <Arduino.h>
#include <SPI.h>
//The board's CHGame library, which comes with its board package, for its buttons and its sound
//only: the rest of it, and CHGfx under it, is drawing this game does itself. The whole library is
//included because a sketch only finds a library by its main header. What is not used costs
//nothing as long as the libraries are linked from archives (dot_a_linkage=true in their
//library.properties, in the board package after 0.3.0). With 0.3.0 as released, CHGfx's DMA
//interrupt handler keeps its 8 KB framebuffer in every sketch that includes it, which leaves this
//game too little RAM
#include <CHGame.h>
//for SPI1's own registers, which the pixels go out through, see SpiSend below
extern "C" {
#include "ch32x035.h"
}
#include "PlatformGamebuinoFont.h"

//the game's 128x128 screen is the whole display, there is no border to keep black
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 128
//where this panel's first pixel sits in the controller's memory, from CHGfx
#define DISPLAY_OFFSET_X 2
#define DISPLAY_OFFSET_Y 3

//24 MHz, the SPI1 clock divided by two. The panel is only ever written to, so the transfers ask
//for transmit only: the core's block transfer otherwise writes what comes back over the buffer
//it was given
#define DISPLAY_SPI_HZ 24000000
#define DISPLAY_SPI_SETTINGS SPISettings(DISPLAY_SPI_HZ, MSBFIRST, SPI_MODE0, SPI_TRANSMITONLY)
//the card is happy slower, and is talked to while no display transaction is open
#define SD_SPI_HZ 12000000

#if CHGAME_TIMING
//What the strips cost since the last report. With SCREENBUFFER 0 the frame does not leave through
//Platform_PresentFrame at all, it leaves a strip at a time through writePixels, so without these
//the whole of a frame would be counted as the game's own drawing
static uint32_t stripSendUs = 0;
static uint32_t stripPixels = 0;
#endif

static PlatformCHGameDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the display once at the end of Game_Loop
PlatformBuffer screenBuffer;
//the two colours a 1 bpp frame is shown in, high byte first as the display takes them
static uint8_t bufferSetColor[2] = { 0xFF, 0xFF }, bufferClearColor[2] = { 0x00, 0x00 };
//The eight pixels of every byte a 1 bpp buffer can hold, ready for the display. Working them out
//a pixel at a time cost 18 of the 44 ms a frame took, as much as the whole transfer; this way a
//row is sixteen copies of sixteen bytes. It is 4 KB of the 20, and is built again whenever the
//two colours change
static uint8_t bufferExpand[256][16];

static void BuildExpandTable(void)
{
	for (int i = 0; i < 256; i++)
	{
		for (int b = 0; b < 8; b++)
		{
			const uint8_t* color = (i & (0x80 >> b)) ? bufferSetColor : bufferClearColor;
			bufferExpand[i][b * 2] = color[0];
			bufferExpand[i][b * 2 + 1] = color[1];
		}
	}
}
#endif

//The board links a libprintf of its own ahead of the C library (-lprintf, see its boards.txt) which
//gives printf, sprintf and snprintf but no vsnprintf. Its conversions take no length modifier: %ld
//and %lu write nothing at all and leave every argument after them reading the wrong one, so a score
//printed with %ld came out blank and whatever followed it came out as rubbish. Print with %d and %u
//and cast, which is enough since an int is 32 bits here. Platform_Log is not affected: vsnprintf
//comes from the C library and takes the modifiers as usual, which is why the frame report below is
//written with %lu and prints
//Pixels are handed to the SPI in RAM lumps rather than a byte at a time: a call per byte costs
//more than the byte takes on the wire. It also keeps what is sent away from the caller's data,
//which may be in flash and cannot be written over
#define SPI_CHUNK_PIXELS 64
//Two of them, so the chunk being byte swapped is never the one the DMA is reading, see
//writePixels. writeColor fills one and sends it over and over, and uses only the first
static uint8_t spiChunk[2][SPI_CHUNK_PIXELS * 2];

static void PaintStack(void);
static void ToneInit(void);
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
// SPI and the panel's lines
// ===========================================================================

//Bytes go out through SPI1's own register rather than through the library. Its transfer waits on
//the flag through a function call and asks the tick counter twice a byte to see whether it has
//timed out, which is around a hundred thousand calls for a frame of 32 KB: the game ran at 8
//frames a second with them and the wire itself only takes 11 ms of the 33 a frame has. The panel
//is only ever written to, so nothing is read back and the receive buffer is left to overrun.
//SPI.beginTransaction still sets the clock and the mode, only the data goes this way
static inline void SpiSend(const uint8_t* data, size_t length)
{
	while (length--)
	{
		while (!(SPI1->STATR & SPI_STATR_TXE))
			;
		SPI1->DATAR = *data++;
	}
}

static inline void SpiSendByte(uint8_t value)
{
	while (!(SPI1->STATR & SPI_STATR_TXE))
		;
	SPI1->DATAR = value;
}

//waits for the last byte to leave, before the data/command line or the chip select moves
static inline void SpiWait(void)
{
	while (SPI1->STATR & SPI_STATR_BSY)
		;
}

// ---------------------------------------------------------------------------
// The pixels go out by DMA
//
// Feeding the register by hand only keeps the wire half busy: the clock really is 24 MHz, but the
// loop cannot fetch, test and store fast enough out of flash to hand it a byte every 16 cycles, so
// a frame took 22 ms where the bytes themselves are 11. DMA1 channel 3 is SPI1's transmit request,
// and once it is started the frame costs what its bytes cost and the processor is free to build
// the next row meanwhile. This is what CHGfx does, and why it reaches the wire's own limit
// ---------------------------------------------------------------------------

//Memory to peripheral, a byte at a time, the memory address stepping and the peripheral's standing
//still, at the highest priority. The names are the chip header's own: leaving MINC out sends the
//first byte over and over, and channel 3's finished flag is 0x200 where 0x20 is channel 2's
#define DMA_CFGR_TO_SPI (DMA_CFGR1_DIR | DMA_CFGR1_MINC | DMA_CFGR1_PL)
#define DMA_CFGR_ENABLE DMA_CFGR1_EN
#define DMA_FLAG_TC3 DMA1_FLAG_TC3
//all four of channel 3's flags, cleared together as CHGfx does
#define DMA_FLAGS_CH3 ((uint32_t)0x00000F00)

static bool dmaBusy = false;

static void DmaInit(void)
{
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
	DMA1_Channel3->CFGR = 0;
	DMA1_Channel3->PADDR = (uint32_t)&SPI1->DATAR;
	SPI1->CTLR2 |= SPI_I2S_DMAReq_Tx;
}

//Set once the DMA has been seen not to work, after which the rows go out by hand instead. A frame
//that never finishes would leave the screen black and nothing to read, which is exactly what a
//wrong channel or a wrong flag did while this was being written
static bool dmaBroken = false;

//hands the bytes over and returns at once, they are on their way while this goes on
static void DmaSend(const uint8_t* data, uint16_t length)
{
	if (dmaBroken)
	{
		SpiSend(data, length);
		return;
	}
	//The order is CHGfx's, which is the one that works: the transmit request is turned off while
	//the channel is set up, the settings are written without the enable bit, the request is turned
	//on, and only then is the channel started. SPI.beginTransaction also puts CTLR2 back as it was
	//for every transaction, so the request is asked for here and not once at the start
	SPI1->CTLR2 &= (uint16_t)~SPI_I2S_DMAReq_Tx;
	DMA1_Channel3->CFGR = 0;
	DMA1->INTFCR = DMA_FLAGS_CH3;
	DMA1_Channel3->MADDR = (uint32_t)data;
	DMA1_Channel3->CNTR = length;
	DMA1_Channel3->CFGR = DMA_CFGR_TO_SPI;
	SPI1->CTLR2 |= SPI_I2S_DMAReq_Tx;
	DMA1_Channel3->CFGR |= DMA_CFGR_ENABLE;
	dmaBusy = true;
}

static void DmaWait(void)
{
	if (!dmaBusy)
		return;
	//A row is 256 bytes, which at 24 MHz is about 85 us. This waits many times longer than that
	//and then gives up rather than standing here for good
	uint32_t spins = 2000000;
	while (!(DMA1->INTFR & DMA_FLAG_TC3))
	{
		if (--spins == 0)
		{
			dmaBroken = true;
			break;
		}
	}
	DMA1_Channel3->CFGR = 0;
	DMA1->INTFCR = DMA_FLAGS_CH3;
	SPI1->CTLR2 &= (uint16_t)~SPI_I2S_DMAReq_Tx;
	dmaBusy = false;
	//the last byte sits in the shift register for a while after the DMA has done with it
	SpiWait();
}

//see the note at the top: the panel takes a drifting reset line for a real reset
static inline void ResetDriveHigh(void)
{
	digitalWrite(PIN_LCD_RST, HIGH);
}

//The data/command line only means anything while the byte it belongs to is on the wire, so it is
//moved with the wire idle on both sides of the command
static void WriteCommand(uint8_t command)
{
	SpiWait();
	digitalWrite(PIN_LCD_DC, LOW);
	SpiSendByte(command);
	SpiWait();
	digitalWrite(PIN_LCD_DC, HIGH);
}

// ===========================================================================
// Display
// ===========================================================================

#define ST7735_DELAY 0x80

//The init CHGfx uses for this panel: command, argument count (with ST7735_DELAY a delay in ms
//follows the arguments), arguments. The frame rate triplet 0x05,0x3A,0x3A scans the glass
//faster than the usual 0x01,0x2C,0x2D, MADCTL 0xC8 is the 1.44" green tab's rotation 0
static const uint8_t displayResetCommands[] = {
	2,
	0x01, ST7735_DELAY, 150,        //software reset
	0x11, ST7735_DELAY, 255,        //out of sleep mode
};
static const uint8_t displayInitCommands[] = {
	17,
	0xB1, 3, 0x05, 0x3A, 0x3A,      //frame rate, normal mode
	0xB2, 3, 0x05, 0x3A, 0x3A,      //frame rate, idle mode
	0xB3, 6, 0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A,  //frame rate, partial mode
	0xB4, 1, 0x07,                  //no inversion
	0xC0, 3, 0xA2, 0x02, 0x84,      //power control
	0xC1, 1, 0xC5,
	0xC2, 2, 0x0A, 0x00,
	0xC3, 2, 0x8A, 0x2A,
	0xC4, 2, 0x8A, 0xEE,
	0xC5, 1, 0x0E,
	0x20, 0,                        //inversion off
	0x36, 1, 0xC8,                  //MADCTL: MY | MX | BGR
	0x3A, 1, 0x05,                  //16 bit colour
	0xE0, 16, 0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
	0xE1, 16, 0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
	0x13, ST7735_DELAY, 10,         //normal display on
	0x29, ST7735_DELAY, 100,        //display on
};

static void RunCommands(const uint8_t* list)
{
	uint8_t count = *list++;
	SPI.beginTransaction(DISPLAY_SPI_SETTINGS);
	digitalWrite(PIN_LCD_CS, LOW);
	while (count--)
	{
		WriteCommand(*list++);
		uint8_t args = *list++;
		const bool delayAfter = (args & ST7735_DELAY) != 0;
		args &= ~ST7735_DELAY;
		while (args--)
			SpiSendByte(*list++);
		if (delayAfter)
		{
			SpiWait();
			delay(*list++);
		}
	}
	SpiWait();
	digitalWrite(PIN_LCD_CS, HIGH);
	SPI.endTransaction();
}

//the area of the display itself the pixels written next fill, both corners included
static void SetDisplayWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
	ResetDriveHigh();
	WriteCommand(0x2A);
	SpiSendByte(0);
	SpiSendByte(x0);
	SpiSendByte(0);
	SpiSendByte(x1);
	WriteCommand(0x2B);
	SpiSendByte(0);
	SpiSendByte(y0);
	SpiSendByte(0);
	SpiSendByte(y1);
	WriteCommand(0x2C);
}

void PlatformCHGameDisplay::init(void)
{
	//the card shares the SPI and has to stay deselected while the panel is talked to
	pinMode(PIN_SD_CS, OUTPUT);
	digitalWrite(PIN_SD_CS, HIGH);
	pinMode(PIN_LCD_CS, OUTPUT);
	digitalWrite(PIN_LCD_CS, HIGH);
	pinMode(PIN_LCD_DC, OUTPUT);
	digitalWrite(PIN_LCD_DC, HIGH);
	pinMode(PIN_LCD_RST, OUTPUT);

	//a reset pulse, then the line stays high for as long as the game runs
	digitalWrite(PIN_LCD_RST, HIGH);
	delay(10);
	digitalWrite(PIN_LCD_RST, LOW);
	delay(10);
	digitalWrite(PIN_LCD_RST, HIGH);
	delay(120);

	SPI.begin();
	RunCommands(displayResetCommands);
	RunCommands(displayInitCommands);

	//whatever the panel powered up holding is cleared before the game draws
	startWrite();
	setAddrWindow(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	writeColor(0x0000, (uint32_t)WINDOW_WIDTH * WINDOW_HEIGHT);
	endWrite();
}

void PlatformCHGameDisplay::startWrite(void)
{
	if (writeDepth++ == 0)
	{
		SPI.beginTransaction(DISPLAY_SPI_SETTINGS);
		digitalWrite(PIN_LCD_CS, LOW);
	}
}

void PlatformCHGameDisplay::endWrite(void)
{
	if (writeDepth && (--writeDepth == 0))
	{
		//the chip select only goes up once the last byte has really left
		SpiWait();
		digitalWrite(PIN_LCD_CS, HIGH);
		SPI.endTransaction();
	}
}

void PlatformCHGameDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	startWrite();
	SetDisplayWindow((uint8_t)(x + DISPLAY_OFFSET_X), (uint8_t)(y + DISPLAY_OFFSET_Y),
	                 (uint8_t)(x + DISPLAY_OFFSET_X + w - 1), (uint8_t)(y + DISPLAY_OFFSET_Y + h - 1));
	endWrite();
}

//One call carries a whole run of pixels. It went out through SpiSend,
//which keeps the wire about half busy for the reason given at the top, so a strip cost twice what
//its bytes cost. It now goes by DMA in the same pattern Platform_PresentFrame uses for its rows:
//the chunk that was filled last travels while the next one is byte swapped
void PlatformCHGameDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
#if CHGAME_TIMING
	const uint32_t tPush = micros();
	stripPixels += (uint32_t)((length > 0) ? length : 0);
#endif
	startWrite();
	uint8_t which = 0;
	while (length > 0)
	{
		const int32_t run = (length > SPI_CHUNK_PIXELS) ? SPI_CHUNK_PIXELS : length;
		uint8_t* dst = spiChunk[which];
		if (swap)
		{
			for (int32_t i = 0; i < run; i++)
			{
				*dst++ = (uint8_t)(data[i] >> 8);
				*dst++ = (uint8_t)data[i];
			}
		}
		else
		{
			for (int32_t i = 0; i < run; i++)
			{
				*dst++ = (uint8_t)data[i];
				*dst++ = (uint8_t)(data[i] >> 8);
			}
		}
		//the chunk before this one is given until now to finish, then this one is handed over
		DmaWait();
		DmaSend(spiChunk[which], (uint16_t)(run * 2));
		which ^= 1;
		data += run;
		length -= run;
	}
	//the panel is only let go once the last chunk is off the wire
	DmaWait();
	endWrite();
#if CHGAME_TIMING
	stripSendUs += micros() - tPush;
#endif
}

void PlatformCHGameDisplay::writeColor(uint16_t color, uint32_t length)
{
	const uint8_t high = (uint8_t)(color >> 8), low = (uint8_t)color;
	startWrite();
	//the lump is filled once and sent as often as it takes
	uint32_t filled = (length > SPI_CHUNK_PIXELS) ? SPI_CHUNK_PIXELS : length;
	for (uint32_t i = 0; i < filled; i++)
	{
		spiChunk[0][i * 2] = high;
		spiChunk[0][i * 2 + 1] = low;
	}
	while (length)
	{
		const uint32_t run = (length > filled) ? filled : length;
		SpiSend(spiChunk[0], (size_t)run * 2);
		length -= run;
	}
	endWrite();
}

void PlatformCHGameDisplay::writeBytes(const uint8_t* data, uint32_t length)
{
	//nothing is written over what it is given any more, so the caller's bytes go out where they lie
	startWrite();
	SpiSend(data, length);
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

void PlatformCHGameDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	startWrite();
	setAddrWindow(x, y, w, h);
	writeColor(color, (uint32_t)w * h);
	endWrite();
}

void PlatformCHGameGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformCHGameGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

//Same character as the one above, but painted as one window: a run of pixels through fillRect
//costs a window of its own, and a window is command bytes the SPI has to drain before anything
//else can follow. A whole line of text was more of a frame than the board it was drawn over.
//A background the same as the text colour still goes the other way, there the pixels between the
//glyph are left as they are
#define FASTCHARSIZE 2   //text this size or smaller is built in the buffer below
//only taken from the heap the first time text is drawn this way, and kept from then on.
//NULL until then, or when that allocation failed, and the run path draws the character
static uint16_t* charCell = NULL;

size_t PlatformCHGameDisplay::drawChar(uint16_t c, int32_t x, int32_t y)
{
#if SCREENBUFFER
	//With a screen buffer nothing is ever written to the display a character at a time: the game
	//draws into the buffer and the finished frame goes out in one piece. The cell below would
	//never be built, but drawChar is virtual, so without this the whole of it is still linked in
	return PlatformCHGameGFX::drawChar(c, x, y);
#else
	const int32_t size = textSize;
	if (c >= 176)
		c++;
	if (c > 255)
		return 6 * size;
	const int32_t w = 6 * size, h = 8 * size;
	//a character that hangs off the screen is left to the clipping the run path does
	if ((textBackground == textColor) || (size > FASTCHARSIZE) ||
		(x < 0) || (y < 0) || (x + w > WINDOW_WIDTH) || (y + h > WINDOW_HEIGHT))
		return PlatformCHGameGFX::drawChar(c, x, y);

	if (!charCell)
	{
		charCell = (uint16_t*)malloc(6 * FASTCHARSIZE * 8 * FASTCHARSIZE * sizeof(uint16_t));
		//without it the character still goes out, a window per run of pixels
		if (!charCell)
			return PlatformCHGameGFX::drawChar(c, x, y);
	}

	//the cell is filled a glyph pixel at a time, never worked out per screen pixel
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
#endif
}

bool PlatformCHGameBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (size_t)((w + 7) / 8) * h;
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformCHGameBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!pixels || !ClipRect(x, y, w, h))
		return;
#if SCREENBUFFER == 1
	(void)depth;
	//A 1 bpp fill is whole bytes, eight pixels at a time, whenever every pixel of the rectangle
	//comes out the same bit. Pixel by pixel it was a brightness test, a shift, a mask and a read
	//modify write each time, and clearing the screen alone is WINDOW_WIDTH * WINDOW_HEIGHT of them.
	//With DITHERING a shade between the ends of the pattern does differ per pixel, and that still
	//goes the slow way; the ends of it, and every colour without it, do not
	const uint16_t fillLum = (uint16_t)((((((color >> 11) & 0x1F) << 3) * 77)
	                                   + ((((color >> 5) & 0x3F) << 2) * 150)
	                                   + (((color & 0x1F) << 3) * 29)) >> 8);
  #if DITHERING
	//8 to 248 is the pattern's range, see SetBufferBit in Platform.h
	const bool uniform = (fillLum <= 8) || (fillLum > 248);
  #else
	const bool uniform = true;
  #endif
	if (uniform && (WINDOW_WIDTH % 8 == 0))
	{
		const bool set = (fillLum >= 128);
		const int32_t stride = WINDOW_WIDTH >> 3;
		uint8_t* base = (uint8_t*)pixels;
		for (int32_t row = y; row < y + h; row++)
		{
			uint8_t* line = base + row * stride;
			int32_t column = x;
			const int32_t end = x + w;
			//the byte the rectangle starts part way into
			while ((column < end) && ((column & 7) != 0))
			{
				const uint8_t bit = (uint8_t)(0x80 >> (column & 7));
				if (set)
					line[column >> 3] |= bit;
				else
					line[column >> 3] &= (uint8_t)~bit;
				column++;
			}
			//the whole bytes between the ends
			const int32_t whole = (end - column) >> 3;
			if (whole > 0)
			{
				memset(line + (column >> 3), set ? 0xFF : 0x00, (size_t)whole);
				column += whole << 3;
			}
			//and the byte it stops part way into
			while (column < end)
			{
				const uint8_t bit = (uint8_t)(0x80 >> (column & 7));
				if (set)
					line[column >> 3] |= bit;
				else
					line[column >> 3] &= (uint8_t)~bit;
				column++;
			}
		}
		return;
	}
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
	display.init();
	//after the display, so the SPI is already up when its transmit request is turned on
	DmaInit();
	StorageInit(appName);

	//the buzzer, through the CHGame library's sound engine
	ToneInit();

	//every button pulled up and reading low while it is held, set up by the CHGame library
	chgame.boot();
	//the pin changes above rewrite the port's configuration, see the note at the top
	ResetDriveHigh();

#if SCREENBUFFER
	screenBuffer.setColorDepth(SCREENBUFFER);
	//black and white until the game says otherwise, so the table is never read unbuilt
	BuildExpandTable();
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
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
	//the table holds these two colours, so it is built again whenever they change
	BuildExpandTable();
#else
	(void)setColor;
	(void)clearColor;
#endif
}

void Platform_PresentFrame(void)
{
#if CHGAME_TIMING
	static uint32_t sendSum = 0, buildSum = 0, frameSum = 0, lastStart = 0;
	static uint32_t pixelSum = 0;
	static uint16_t counted = 0;
	const uint32_t tStart = micros();
	uint32_t buildUs = 0;
#endif
#if SCREENBUFFER
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	//Two rows, so the one being built is never the one on the wire. The row is turned into the
	//display's bytes while the row before it is still going out, which is what keeps the wire from
	//ever standing idle
	static uint8_t lines[2][WINDOW_WIDTH * 2];
	uint8_t which = 0;
	display.startWrite();
	display.setAddrWindow(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	for (int16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		uint8_t* line = lines[which];
		which ^= 1;
#if CHGAME_TIMING
		const uint32_t tRow = micros();
#endif
		uint8_t* dst = line;
		//a byte of the buffer is eight pixels the table already holds
		for (int16_t x = 0; x < WINDOW_WIDTH; x += 8, dst += 16)
			memcpy(dst, bufferExpand[*src++], 16);
#if CHGAME_TIMING
		buildUs += micros() - tRow;
#endif
		//the row before this one is given until now to finish, then this one goes on its way and
		//the next is built while it travels
		DmaWait();
		DmaSend(line, WINDOW_WIDTH * 2);
	}
	DmaWait();
	display.endWrite();
#endif
#if CHGAME_TIMING
	const uint32_t tEnd = micros();
	sendSum += (tEnd - tStart) - buildUs;
	buildSum += buildUs;
	//what went out through writePixels since the last frame, which is the whole of the frame with
	//SCREENBUFFER 0
	sendSum += stripSendUs;
	pixelSum += stripPixels;
	stripSendUs = 0;
	stripPixels = 0;
	if (lastStart)
		frameSum += tStart - lastStart;
	lastStart = tStart;
	if (++counted >= 30)
	{
		//the frame is everything between one present and the next: the game's own drawing is
		//whatever is left once the two below are taken off it
		Platform_Log("frame %6lu us  build %5lu  send %5lu  game %6lu  px %5lu  heap %5lu  stack %4lu%s\n",
		             (unsigned long)(frameSum / counted), (unsigned long)(buildSum / counted),
		             (unsigned long)(sendSum / counted),
		             (unsigned long)((frameSum - buildSum - sendSum) / counted),
		             (unsigned long)(pixelSum / counted),
		             (unsigned long)Platform_FreeHeap(), (unsigned long)Platform_FreeStack(),
		             dmaBroken ? "  (DMA gave up, rows go out by hand)" : "");
		sendSum = buildSum = frameSum = pixelSum = 0;
		counted = 0;
	}
#endif
}

// ===========================================================================
// Buttons
// ===========================================================================

//Back to the SD game menu, the way the CHGame library's own games do it: a reset that carries no
//boot request, so the bootloader with the SD game menu shows its menu, with this game preselected.
//A bootloader without a menu starts the game again
void Platform_Exit(void)
{
	chgame_exitToMenu();
}

//START held for 3 seconds calls Platform_Exit, as in those games. Counted in milliseconds rather
//than frames, so it takes as long whatever the frame rate. The library counts it in calls of its
//pollButtons, which only comes to 3 seconds at 60 of them a second, so the buttons are read with
//its chgame_readButtons and the hold is timed here
#define CHGAME_EXIT_HOLD_MS 3000
//when START went down, 0 while it is up
static uint32_t startHeldSince = 0;

uint8_t Platform_GetButtons(void)
{
	//every button in one read of each port, pressed = 1
	const uint8_t held = chgame_readButtons();
	uint8_t buttons = 0;
	if (held & LEFT_BUTTON)
		buttons |= BUTTON_LEFT;
	if (held & UP_BUTTON)
		buttons |= BUTTON_UP;
	if (held & DOWN_BUTTON)
		buttons |= BUTTON_DOWN;
	if (held & RIGHT_BUTTON)
		buttons |= BUTTON_RIGHT;
	if (held & A_BUTTON)
		buttons |= BUTTON_A;
	if (held & B_BUTTON)
		buttons |= BUTTON_B;
	//the two side buttons, which the game uses to page through things
	if (held & SELECT_BUTTON)
		buttons |= BUTTON_L;
	if (held & START_BUTTON)
	{
		buttons |= BUTTON_R;
		//| 1 keeps it from reading as "up" in the one millisecond where millis() is 0
		if (!startHeldSince)
			startHeldSince = millis() | 1;
		else if ((millis() - startHeldSince) >= CHGAME_EXIT_HOLD_MS)
			Platform_Exit();
	}
	else
		startHeldSince = 0;
	return buttons;
}

// ===========================================================================
// Sound: the CHGame library's sound engine (chgame/Audio.h)
//
// The board package builds without the timer module unless the Peripherals menu is set to Full,
// and the core's tone() is then an empty function: the game would run and say nothing. Leaving
// that module out is worth about 5 KB, which is the difference between fitting this device and
// not. The CHGame library plays the buzzer without it: TIM1 channel 2 on PB10, stepped by the
// core's 1 kHz tick, the way the board's own games sound.
//
// Every tone is one of the library's effects: steps of a pitch in 20 Hz units and a length of up
// to 510 ms, so a longer tone is several steps of the same pitch. A tone with no length plays
// until the next tone, as an effect of the longest length (4 s), which the next tone cuts off.
// As before, every tone replaces whatever was sounding, so what plays is always the last tone
// asked for.
// ===========================================================================

//the longest tone is this many steps of 510 ms
#define TONE_MAX_STEPS 8

//Two of everything, so the steps being filled are never the ones the 1 kHz tick may still be
//reading: each tone takes the pair the one before it did not
static audio::Step toneSteps[2][TONE_MAX_STEPS];
static audio::Effect toneEffects[2];
static uint8_t toneWhich = 0;

//The effects are of the highest priority, 15, so that each one replaces whatever sounds, the rest
//ToneSilence plays included
static void ToneInit(void)
{
	toneEffects[0] = { toneSteps[0], 1, 15 };
	toneEffects[1] = { toneSteps[1], 1, 15 };
	audio::begin(toneEffects, 2);
}

//what is sounding goes quiet: a rest of 2 ms above every effect cuts it off
static void ToneSilence(void)
{
	audio::note(0, 2, 15);
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	ToneSilence();
	//a frequency of 0 is a rest
	if (!freq)
		return;
	const uint8_t hz = (uint8_t)((freq >= 5100) ? 255 : (freq + 10) / 20);
	//a duration of 0 plays until the next tone
	uint32_t left = duration ? duration : (uint32_t)TONE_MAX_STEPS * 510;
	toneWhich ^= 1;
	audio::Step* steps = toneSteps[toneWhich];
	uint8_t n = 0;
	while (left && (n < TONE_MAX_STEPS))
	{
		const uint32_t ms = (left > 510) ? 510 : left;
		steps[n].hz = hz;
		steps[n].endHz = 0;
		steps[n].ms = (uint8_t)((ms + 1) / 2);
		left -= ms;
		n++;
	}
	toneEffects[toneWhich].n = n;
	audio::sfx(toneWhich);
}

void Platform_StopTone(void)
{
	ToneSilence();
}


// ===========================================================================
// Time and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return micros();
}

uint32_t Platform_Millis(void)
{
	return millis();
}

//Where the linker put things, from link_chgame_app.ld. This chip does not lay its RAM out the way
//the Gamebuino does: there the heap grows up and the stack down into one shared gap, here the
//stack is a region of its own of __stack_size bytes at the very top and the heap has a ceiling of
//its own below it. So the two are measured apart, and neither can be worked out from the other
extern "C" {
extern char _end[];         //the end of the program's data, where the heap starts
extern char _heap_end[];    //as far up as the heap may ever grow
extern char _susrstack[];   //the bottom of the stack, which is as far down as it may grow
extern char _eusrstack[];   //the top of RAM, where the stack starts and grows downwards
}

//What is left for the heap: the part of its room it has not asked for yet, and what it has asked
//for but handed back. sbrk is not used, the C library here does not have to offer one
uint32_t Platform_FreeHeap(void)
{
	const struct mallinfo info = mallinfo();
	const uint32_t room = (uint32_t)(_heap_end - _end);
	const uint32_t taken = (uint32_t)info.arena;
	return (taken > room ? 0 : room - taken) + (uint32_t)info.fordblks;
}

#define STACK_PAINT 0xA5

//Fills the stack below what is in use with a pattern. What the stack reaches later writes over it,
//so what is left of it says how close the stack has ever come to the bottom
static void PaintStack(void)
{
	//everything under this function's own frame, with a little room for the calls it makes
	char* to = (char*)__builtin_frame_address(0) - 64;
	if (to > _eusrstack)
		to = _eusrstack;
	for (char* p = _susrstack; p < to; p++)
		*p = (char)STACK_PAINT;
}

//the least stack that has been free since Platform_Init: the pattern still standing at the bottom
uint32_t Platform_FreeStack(void)
{
	const char* p = _susrstack;
	uint32_t count = 0;
	while ((p < _eusrstack) && (*p == (char)STACK_PAINT))
	{
		p++;
		count++;
	}
	return count;
}

uint32_t Platform_RandomSeed(void)
{
	//the time since power on hardly differs from one start to the next, the noise on an analog
	//input that nothing drives does
	return (micros() * micros()) ^ ((uint32_t)analogRead(PIN_A0) * analogRead(PIN_A1)) ^
	       ((uint32_t)analogRead(PIN_A2) << 16);
}

void Platform_Log(const char* format, ...)
{
	char text[128];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	//Only to a computer that has the USB serial port open, it does not wait for one. This class
	//has no conversion to bool the way the other cores' do, dtr() is what says a host is there
	if (Serial.dtr())
		Serial.print(text);
}

// ===========================================================================
// Saved data
//
// The part has no EEPROM the game can use: its own library stores into the option bytes and holds
// 26 of them, where the game's block is PLATFORM_STORAGE_SIZE. What it does have is flash the
// bootloader leaves alone. The bootloader erases only the pages a new sketch occupies, so the
// pages between the end of the image and the metadata page at 0xF700 come through an upload
// untouched, and the game's block lives in one of those. This is what CHBlackjack does.
//
// Two pages are written in turn, each record stamped with a magic value, a version, a sequence
// number that counts up and a CRC over everything before it. Whichever page reads back valid with
// the higher sequence number is the newest save, so a power cut part way through a write can lose
// at most the save being made and never the one before it. Where the image reaches into the first
// of the two pages only the second is used, and where it reaches into both, saving turns itself
// off rather than write over code.
//
// The whole block is kept in RAM as well and that is what the game reads, so a load costs nothing
// and a write only happens when something actually changed.
//
// The magic value is the game's own. Every game of this kind keeps its save in the same two pages,
// and CHGame's SD card menu installs a game by writing only the pages its image needs, so the
// save of the game that was on before is still there when the next one starts. With one magic
// for all of them a game took another's block for its own; the magic is made from the first word
// of the name Platform_Init gets (the version after it changes from build to build, the game does
// not) and CHGAME_SAVE_VARIANT, which a build of one level pack of a game sets so that each pack
// keeps a save of its own.
// ===========================================================================

//the pages, and the size of one. The metadata page the bootloader keeps is at 0xF700
#define CHGAME_FLASH_PAGE   256
#define CHGAME_SAVE_PAGE_A  0xF500u
#define CHGAME_SAVE_PAGE_B  0xF600u
#define CHGAME_SAVE_VERSION 1
#ifndef CHGAME_SAVE_VARIANT
#define CHGAME_SAVE_VARIANT 0
#endif

typedef struct ChGameSave ChGameSave;
struct ChGameSave
{
	uint32_t magic;
	uint16_t version;
	uint16_t seq;                          //counts up, the higher of the two pages is the newer
	uint8_t  block[PLATFORM_STORAGE_SIZE];
	uint32_t crc;                          //covers every byte before it
};

//What is left for the block once the stamp and the sum are in it. The two pages sit next to each
//other, 0xF500 + 256 being 0xF600, so a block too big for one page is written across both: that
//gives up writing them in turn, and with it the promise that a power cut can only lose the newest
//save, which is why it is only done where the block leaves no choice
#define CHGAME_SAVE_PAYLOAD (CHGAME_FLASH_PAGE - 12)
#define CHGAME_SAVE_PAYLOAD_BOTH ((CHGAME_FLASH_PAGE * 2) - 12)
//1 while a record fits one page, so the two can be written in turn
#define CHGAME_SAVE_ALTERNATES (PLATFORM_STORAGE_SIZE <= CHGAME_SAVE_PAYLOAD)
static_assert(sizeof(ChGameSave) == PLATFORM_STORAGE_SIZE + 12, "the record is not the shape it says");

static uint8_t storage[PLATFORM_STORAGE_SIZE];

#if PLATFORM_STORAGE_SIZE <= CHGAME_SAVE_PAYLOAD_BOTH
static uint16_t storageSeq = 0;
//this game's magic value, set by StorageInit from its name
static uint32_t storageMagic = 0;
//1 once a write has failed to read back, after which nothing more is written
static bool storageBroken = false;

//where the image ends, which is what says how many of the two pages are free
extern "C" uint32_t _data_lma, _data_vma, _edata;

static uint32_t StorageImageEnd(void)
{
	return (uint32_t)&_data_lma + ((uint32_t)&_edata - (uint32_t)&_data_vma);
}

//1 while there is room above the image for the record at all. A record running across both pages
//needs the image to stop below the first of them; one that fits a page needs only the last
static bool StorageAvailable(void)
{
#if CHGAME_SAVE_ALTERNATES
	return !storageBroken && (StorageImageEnd() <= CHGAME_SAVE_PAGE_B);
#else
	return !storageBroken && (StorageImageEnd() <= CHGAME_SAVE_PAGE_A);
#endif
}

#if CHGAME_SAVE_ALTERNATES
//1 while both pages are free, so that the newest save never overwrites the one before it
static bool StorageTwoPages(void)
{
	return StorageImageEnd() <= CHGAME_SAVE_PAGE_A;
}
#endif

static uint32_t StorageCrc(const uint8_t* data, uint32_t length)
{
	uint32_t crc = 0xFFFFFFFFu;
	while (length--)
	{
		crc ^= *data++;
		for (uint8_t bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(0u - (crc & 1u)));
	}
	return ~crc;
}

static bool StorageRecordOk(const ChGameSave* rec)
{
	return (rec->magic == storageMagic) && (rec->version == CHGAME_SAVE_VERSION) &&
	       (rec->crc == StorageCrc((const uint8_t*)rec, (uint32_t)(sizeof(ChGameSave) - sizeof(uint32_t))));
}

//The flash controller, following CH32SerialBoot's own flash.c. This has to run from RAM with
//interrupts off: the vector table is in flash, and flash cannot be read while it is being written.
//.srodata is a RAM section in the linker script, so the function is copied there at start up
#define CHGAME_RAMFUNC __attribute__((section(".srodata.chgameflash"), noinline))
#define CHGAME_FLASH_PROG(a) ((a) + 0x08000000u)

CHGAME_RAMFUNC static void StoragePageWrite(uint32_t addr, const uint32_t* words)
{
	uint32_t saved;
	__asm volatile("csrr %0, 0x800" : "=r"(saved));
	__asm volatile("csrw 0x800, %0" : : "r"(saved & ~0x88u));

	FLASH->KEYR = 0x45670123u;
	FLASH->KEYR = 0xCDEF89ABu;
	FLASH->MODEKEYR = 0x45670123u;
	FLASH->MODEKEYR = 0xCDEF89ABu;

	//erase the page
	FLASH->CTLR |= 0x00020000u;                      //page erase
	FLASH->ADDR = CHGAME_FLASH_PROG(addr);
	FLASH->CTLR |= 0x00000040u;                      //start
	while (FLASH->STATR & 0x00000001u) ;             //busy
	FLASH->CTLR &= ~0x00020000u;

	//empty the page buffer
	FLASH->CTLR |= 0x00010000u;                      //page program
	FLASH->CTLR |= 0x00080000u;                      //buffer reset
	while (FLASH->STATR & 0x00000001u) ;
	FLASH->CTLR &= ~0x00010000u;

	//fill it a word at a time
	for (uint32_t i = 0; i < CHGAME_FLASH_PAGE / 4; i++)
	{
		FLASH->CTLR |= 0x00010000u;
		*(volatile uint32_t*)(CHGAME_FLASH_PROG(addr) + i * 4) = words[i];
		FLASH->CTLR |= 0x00040000u;                  //buffer load
		while (FLASH->STATR & 0x00000001u) ;
		FLASH->CTLR &= ~0x00010000u;
	}

	//and write the buffer to the page
	FLASH->CTLR |= 0x00010000u;
	FLASH->ADDR = CHGAME_FLASH_PROG(addr);
	FLASH->CTLR |= 0x00000040u;
	while (FLASH->STATR & 0x00000001u) ;
	FLASH->CTLR &= ~0x00010000u;

	FLASH->CTLR |= 0x00008000u;                      //lock

	__asm volatile("csrw 0x800, %0" : : "r"(saved));
}

static const ChGameSave* StoragePage(uint32_t addr)
{
	return (const ChGameSave*)addr;
}

//writes one record and reads it back, false when the page did not take it
static bool StorageWritePage(uint32_t addr, const ChGameSave* rec)
{
	static uint32_t page[CHGAME_FLASH_PAGE / 4];
	const uint8_t* from = (const uint8_t*)rec;
	uint32_t left = (uint32_t)sizeof(ChGameSave);
	uint32_t at = addr;
	//one page at a time, which is a single page unless the record runs across both
	while (left)
	{
		const uint32_t n = (left > CHGAME_FLASH_PAGE) ? CHGAME_FLASH_PAGE : left;
		memset(page, 0xFF, sizeof(page));
		memcpy(page, from, n);
		StoragePageWrite(at, page);
		if (memcmp((const void*)at, page, CHGAME_FLASH_PAGE) != 0)
			return false;
		from += n;
		left -= n;
		at += CHGAME_FLASH_PAGE;
	}
	return true;
}

//FNV-1a over the first word of the name, case folded, then over the variant. Erased flash and
//zeroed flash are never taken for a save, whatever the name hashes to
static uint32_t StorageMagic(const char* appName)
{
	uint32_t h = 0x811C9DC5u;
	for (const char* p = appName ? appName : ""; *p && (*p != ' '); p++)
		h = (h ^ (uint8_t)((*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p)) * 0x01000193u;
	const uint32_t variant = (uint32_t)(CHGAME_SAVE_VARIANT);
	for (uint8_t i = 0; i < 4; i++)
		h = (h ^ ((variant >> (i * 8)) & 0xFFu)) * 0x01000193u;
	if ((h == 0u) || (h == 0xFFFFFFFFu))
		h = 0x47414843u;
	return h;
}

static void StorageInit(const char* appName)
{
	storageMagic = StorageMagic(appName);
	//nothing saved yet reads as erased flash does elsewhere, which the game takes as never written
	memset(storage, 0xFF, sizeof(storage));
	storageSeq = 0;

	if (!StorageAvailable())
	{
		Platform_Log("the image reaches into the save pages, nothing is saved\n");
		return;
	}

#if CHGAME_SAVE_ALTERNATES
	const ChGameSave* a = StoragePage(CHGAME_SAVE_PAGE_A);
	const ChGameSave* b = StoragePage(CHGAME_SAVE_PAGE_B);
	const bool okA = StorageTwoPages() && StorageRecordOk(a);
	const bool okB = StorageRecordOk(b);
	const ChGameSave* best = NULL;
	if (okA && okB)
		//the sequence numbers wrap, so it is the difference that says which is the newer
		best = ((int16_t)(a->seq - b->seq) > 0) ? a : b;
	else if (okA)
		best = a;
	else if (okB)
		best = b;
#else
	//the one record starts at the first page and runs on into the second
	const ChGameSave* one = StoragePage(CHGAME_SAVE_PAGE_A);
	const ChGameSave* best = StorageRecordOk(one) ? one : NULL;
#endif

	if (!best)
	{
		Platform_Log("no save found\n");
		return;
	}
	memcpy(storage, best->block, sizeof(storage));
	storageSeq = best->seq;
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

	if (!StorageAvailable())
		return;

	static ChGameSave rec;
	memset(&rec, 0, sizeof(rec));
	rec.magic = storageMagic;
	rec.version = CHGAME_SAVE_VERSION;
	rec.seq = (uint16_t)(storageSeq + 1);
	memcpy(rec.block, storage, sizeof(storage));
	rec.crc = StorageCrc((const uint8_t*)&rec, (uint32_t)(sizeof(ChGameSave) - sizeof(uint32_t)));

#if CHGAME_SAVE_ALTERNATES
	//the page that is not holding the newest save, so that one survives a write that fails
	uint32_t addr = CHGAME_SAVE_PAGE_B;
	if (StorageTwoPages())
	{
		const ChGameSave* b = StoragePage(CHGAME_SAVE_PAGE_B);
		addr = (StorageRecordOk(b) && (b->seq == storageSeq)) ? CHGAME_SAVE_PAGE_A : CHGAME_SAVE_PAGE_B;
	}
#else
	//the record fills both pages, so there is only the one place it can go
	const uint32_t addr = CHGAME_SAVE_PAGE_A;
#endif

	if (!StorageWritePage(addr, &rec))
	{
		storageBroken = true;
		Platform_Log("the save page did not take, nothing more is saved\n");
		return;
	}
	storageSeq = rec.seq;
}

#else

//The block this game keeps is larger than what fits in a flash page beside the stamp and the sum,
//and only whole pages come through an upload with anything in them, so there is nowhere to put it.
//The game runs and reads back whatever it wrote while it is on, and nothing is kept once the power
//goes. A game that kept PLATFORM_STORAGE_SIZE bytes or fewer would save here like the rest
static void StorageInit(const char* appName)
{
	(void)appName;
	memset(storage, 0xFF, sizeof(storage));
	Platform_Log("the save block is %d bytes where two flash pages hold %d, nothing is kept\n",
	             (int)PLATFORM_STORAGE_SIZE, (int)CHGAME_SAVE_PAYLOAD_BOTH);
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	memcpy(storage + offset, data, length);
}

#endif

#endif
