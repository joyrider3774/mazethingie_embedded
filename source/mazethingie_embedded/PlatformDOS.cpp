//Platform.h for MS-DOS, built with DJGPP (see dos/CMakeLists.txt):
//  display  VGA mode X, 320x240 in 256 colours, the game's 128x128 frame in the middle of it
//  buttons  the arrow keys, X and C, and S and D for the two side buttons. Escape quits
//  sound    a square wave on the PC speaker, which is what that speaker does
//  saves    a .sav file next to the program, named after the game
//  time     uclock, which counts the ticks of the 8253 timer
//
//Mode X rather than plain mode 13h: 13h is 320x200 stretched over a 4:3 screen, so its pixels are
//taller than they are wide and the game would come out a fifth too tall. 320x240 fills the same
//screen with square pixels. The cost is that mode X is planar, so a frame goes out in four passes,
//one for each of the four columns in every group of four.
//
//The 256 colours are set to the RGB332 cube, which is exactly what the game's 8 bpp screen buffer
//holds: with SCREENBUFFER 8 a frame reaches the card without a single colour being worked out.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_DOS

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/nearptr.h>
#include <sys/farptr.h>
#include "PlatformGamebuinoFont.h"

//the screen mode this sets up, and how big the game is drawn in it
#define SCREEN_DOS_WIDTH 320
#define SCREEN_DOS_HEIGHT 240
#if SCALESCREEN
//as high as the screen, and square, so 240 by 240
#define FRAME_SIZE SCREEN_DOS_HEIGHT
#else
#define FRAME_SIZE WINDOW_WIDTH
#endif
//Where it sits. Both of these land on a multiple of four, which is what keeps a column of the frame
//in the same plane all the way down and the blit below simple
#define FRAME_X ((SCREEN_DOS_WIDTH - FRAME_SIZE) / 2)
#define FRAME_Y ((SCREEN_DOS_HEIGHT - FRAME_SIZE) / 2)
//a row of mode X is 80 bytes, a quarter of the 320 pixels in each of the four planes
#define PLANE_STRIDE (SCREEN_DOS_WIDTH / 4)

static PlatformDOSDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the card when the frame is done
PlatformBuffer screenBuffer;
#endif

#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in, as palette entries
static uint8_t bufferSetColor = 0xFF, bufferClearColor = 0x00;
#endif

#if SCREENBUFFER != 8
//the frame as the card takes it: one palette entry a pixel. With an 8 bpp screen buffer the game
//already draws in exactly this, so there the buffer itself is sent and this is not needed
static uint8_t frameIndex[WINDOW_WIDTH * WINDOW_HEIGHT];
#endif

//video memory, or NULL while the program has not been let at it
static uint8_t* screen = NULL;
static bool nearPointers = false;
//what the video mode was before this started, so it can be put back
static uint8_t oldVideoMode = 0x03;

//the microsecond a tone ends at, 0 while none has to end
static uint32_t toneEnd = 0;
//the file the save block is kept in, named after the game
static char storagePath[64] = "GAME.SAV";

static void ToneOff(void);
static void Shutdown(void);

//RGB565 to the palette entry that holds it, which is the RGB332 cube this sets up
static inline uint8_t ToIndex(uint16_t color)
{
	return (uint8_t)(((color & 0xE000) >> 8) | ((color & 0x0700) >> 6) | ((color & 0x0018) >> 3));
}

// ===========================================================================
// Buttons
//
// The keyboard is read where it happens, from the interrupt the controller raises, because the game
// asks which keys are being held and DOS only offers the ones that have been typed. The handler
// keeps a byte per key and nothing else, and the program puts the old one back when it leaves.
// ===========================================================================

//scan codes of the keys the game uses
#define SCAN_ESCAPE 0x01
#define SCAN_S      0x1F
#define SCAN_D      0x20
#define SCAN_X      0x2D
#define SCAN_C      0x2E
#define SCAN_LEFT   0x4B
#define SCAN_RIGHT  0x4D
#define SCAN_UP     0x48
#define SCAN_DOWN   0x50

static volatile uint8_t keyHeld[128];
static _go32_dpmi_seginfo oldKeyboard, newKeyboard;
static bool keyboardHooked = false;

static void KeyboardHandler(void)
{
	const uint8_t code = inportb(0x60);
	//The arrow keys announce themselves with an 0xE0 first and then the same code the keypad uses.
	//Letting that byte go by on its own means both sets of keys land in the same place
	if (code != 0xE0)
	{
		//the top bit says the key came back up
		if (code & 0x80)
			keyHeld[code & 0x7F] = 0;
		else
			keyHeld[code & 0x7F] = 1;
	}
	//the controller is told the interrupt has been dealt with
	outportb(0x20, 0x20);
}

//marks the end of the handler, so everything between it and the handler can be locked in memory
static void KeyboardHandlerEnd(void) {}

static void ButtonsInit(void)
{
	memset((void*)keyHeld, 0, sizeof(keyHeld));
	//The handler runs from an interrupt, which can arrive while the program is paged out. Locking
	//keeps it and what it touches in memory, otherwise the machine faults inside the interrupt
	_go32_dpmi_lock_data((void*)keyHeld, sizeof(keyHeld));
	_go32_dpmi_lock_code((void*)KeyboardHandler,
	                     (unsigned long)KeyboardHandlerEnd - (unsigned long)KeyboardHandler);

	_go32_dpmi_get_protected_mode_interrupt_vector(9, &oldKeyboard);
	newKeyboard.pm_offset = (int)KeyboardHandler;
	newKeyboard.pm_selector = _go32_my_cs();
	if (_go32_dpmi_allocate_iret_wrapper(&newKeyboard) == 0)
	{
		_go32_dpmi_set_protected_mode_interrupt_vector(9, &newKeyboard);
		keyboardHooked = true;
	}
}

static void ButtonsDone(void)
{
	if (!keyboardHooked)
		return;
	_go32_dpmi_set_protected_mode_interrupt_vector(9, &oldKeyboard);
	_go32_dpmi_free_iret_wrapper(&newKeyboard);
	keyboardHooked = false;
}

uint8_t Platform_GetButtons(void)
{
	uint8_t buttons = 0;
	if (keyHeld[SCAN_LEFT])
		buttons |= BUTTON_LEFT;
	if (keyHeld[SCAN_UP])
		buttons |= BUTTON_UP;
	if (keyHeld[SCAN_DOWN])
		buttons |= BUTTON_DOWN;
	if (keyHeld[SCAN_RIGHT])
		buttons |= BUTTON_RIGHT;
	if (keyHeld[SCAN_X])
		buttons |= BUTTON_A;
	if (keyHeld[SCAN_C])
		buttons |= BUTTON_B;
	if (keyHeld[SCAN_S])
		buttons |= BUTTON_L;
	if (keyHeld[SCAN_D])
		buttons |= BUTTON_R;
	return buttons;
}

// ===========================================================================
// Program start
// ===========================================================================

int main(void)
{
	Game_Setup();
	while (!keyHeld[SCAN_ESCAPE])
	{
		//a tone that has played long enough stops here, the ones with no length of their own play
		//until the next tone
		if (toneEnd && ((int32_t)(Platform_Micros() - toneEnd) >= 0))
			ToneOff();
		Game_Loop();
	}
	Shutdown();
	return 0;
}

// ===========================================================================
// Display
// ===========================================================================

//clips x, y, w, h to the game's screen, false when nothing of it is left
static bool ClipRect(int32_t& x, int32_t& y, int32_t& w, int32_t& h)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > WINDOW_WIDTH) w = WINDOW_WIDTH - x;
	if (y + h > WINDOW_HEIGHT) h = WINDOW_HEIGHT - y;
	return (w > 0) && (h > 0);
}

#if SCREENBUFFER
//everything is drawn into the screen buffer, these are here for the paths that ask for them
void PlatformDOSDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformDOSDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformDOSDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformDOSDisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the frame the card is handed, which holds one
//palette entry a pixel. The area the pixels go into and where the next one goes, in game coordinates
static int32_t windowLeft = 0, windowRight = -1, cursorX = 0, cursorY = 0;

void PlatformDOSDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	windowLeft = x;
	windowRight = x + w - 1;
	cursorX = x;
	cursorY = y;
}

//the next pixel of the window, what falls outside the game's screen is left out
static inline void WritePixel(uint8_t index)
{
	if ((cursorX >= 0) && (cursorX < WINDOW_WIDTH) && (cursorY >= 0) && (cursorY < WINDOW_HEIGHT))
		frameIndex[cursorY * WINDOW_WIDTH + cursorX] = index;
	if (++cursorX > windowRight)
	{
		cursorX = windowLeft;
		cursorY++;
	}
}

void PlatformDOSDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8));
		WritePixel(ToIndex(value));
	}
}

void PlatformDOSDisplay::writeColor(uint16_t color, uint32_t length)
{
	const uint8_t index = ToIndex(color);
	while (length--)
		WritePixel(index);
}

void PlatformDOSDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	const uint8_t index = ToIndex(color);
	for (int32_t row = y; row < y + h; row++)
		memset(&frameIndex[row * WINDOW_WIDTH + x], index, w);
}
#endif

void PlatformDOSGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if ((w <= 0) || (h <= 0))
		return;
	fillRect(x, y, w, 1, color);
	fillRect(x, y + h - 1, w, 1, color);
	if (h > 2)
	{
		fillRect(x, y + 1, 1, h - 2, color);
		fillRect(x + w - 1, y + 1, 1, h - 2, color);
	}
}

//Draws the character the way LovyanGFX draws its default font: a 6x8 cell times the text size, the
//5 columns of the glyph and a column of background, the background only when it differs from the
//text colour. Every column goes out as runs of one colour
size_t PlatformDOSGFX::drawChar(uint16_t c, int32_t x, int32_t y)
{
	const int32_t size = textSize;
	//the classic character set LovyanGFX uses unless told otherwise
	if (c >= 176)
		c++;
	if (c > 255)
		return 6 * size;
	const bool fillBackground = (textBackground != textColor);
	const uint8_t* glyph = &platformFont[c * 5];
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
	return 6 * size;
}

#if SCREENBUFFER
//the screen buffer, read and written for every pixel the game draws and again when the frame is sent
bool PlatformDOSBuffer::createSprite(int32_t w, int32_t h)
{
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	if (pixels)
		free(pixels);
	pixels = (uint8_t*)malloc(bytes);
	if (!pixels)
		return false;
	memset(pixels, 0, bytes);
	return true;
}

void PlatformDOSBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
	const uint8_t value = ToIndex(color);
	for (int32_t row = y; row < y + h; row++)
		memset(&pixels[row * WINDOW_WIDTH + x], value, w);
#else
	for (int32_t row = y; row < y + h; row++)
		for (int32_t column = x; column < x + w; column++)
			SetBufferBit(pixels, column, row, color);
#endif
}
#endif

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	bufferSetColor = ToIndex(setColor);
	bufferClearColor = ToIndex(clearColor);
#else
	(void)setColor;
	(void)clearColor;
#endif
}

//Which pixel of the game every row and column of the frame on screen comes from. Worked out once,
//because 240 over 128 is not a whole number and doing that division per pixel would be the most
//expensive thing in the blit
static uint8_t frameFrom[FRAME_SIZE];

static void ScaleInit(void)
{
	for (int16_t i = 0; i < FRAME_SIZE; i++)
		frameFrom[i] = (uint8_t)((int32_t)i * WINDOW_WIDTH / FRAME_SIZE);
}

//Sends the frame the game drew to the card, blown up to fill the height of the screen. Mode X keeps
//every fourth column in a plane of its own, so this goes round four times, and because the frame
//starts on a multiple of four the plane a column belongs to is the lowest two bits of its place in
//the row
static void BlitFrame(const uint8_t* src)
{
	if (!screen)
		return;
	for (uint8_t plane = 0; plane < 4; plane++)
	{
		//the map mask register, which says what planes a write reaches
		outportb(0x3C4, 0x02);
		outportb(0x3C5, (uint8_t)(1 << plane));
		for (int16_t y = 0; y < FRAME_SIZE; y++)
		{
			const uint8_t* s = &src[frameFrom[y] * WINDOW_WIDTH];
			uint8_t* d = &screen[(FRAME_Y + y) * PLANE_STRIDE + (FRAME_X / 4)];
			const uint8_t* from = &frameFrom[plane];
			for (int16_t x = 0; x < FRAME_SIZE / 4; x++, from += 4)
				d[x] = s[*from];
		}
	}
}

void Platform_PresentFrame(void)
{
#if SCREENBUFFER == 8
	//the buffer already holds what the card takes, one palette entry a pixel
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	BlitFrame(src);
#elif SCREENBUFFER == 16
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	for (int16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * WINDOW_WIDTH * 2];
		uint8_t* d = &frameIndex[y * WINDOW_WIDTH];
		for (int16_t x = 0; x < WINDOW_WIDTH; x++)
			//byte swapped in the buffer
			d[x] = ToIndex((uint16_t)((row[x * 2] << 8) | row[x * 2 + 1]));
	}
	BlitFrame(frameIndex);
#elif SCREENBUFFER == 1
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	for (int16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * ((WINDOW_WIDTH + 7) / 8)];
		uint8_t* d = &frameIndex[y * WINDOW_WIDTH];
		for (int16_t x = 0; x < WINDOW_WIDTH; x++)
			//most significant bit first as SetBufferBit writes
			d[x] = (row[x >> 3] & (0x80 >> (x & 7))) ? bufferSetColor : bufferClearColor;
	}
	BlitFrame(frameIndex);
#else
	//the game drew straight into it
	BlitFrame(frameIndex);
#endif
}

//The 256 colours are set to the RGB332 cube: three bits of red, three of green and two of blue,
//which is what the game's 8 bpp buffer holds and what everything else is turned into. The card takes
//six bits a channel
static void PaletteInit(void)
{
	outportb(0x3C8, 0);
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = (uint8_t)(i >> 5), g3 = (uint8_t)((i >> 2) & 7), b2 = (uint8_t)(i & 3);
		outportb(0x3C9, (uint8_t)((r3 * 63) / 7));
		outportb(0x3C9, (uint8_t)((g3 * 63) / 7));
		outportb(0x3C9, (uint8_t)((b2 * 63) / 3));
	}
}

//VGA mode X: mode 13h to begin with, then the card is told to stop chaining the four planes together
//and to put 240 lines on the screen instead of 200
static void SetModeX(void)
{
	__dpmi_regs r;
	//what the mode was, so it can be put back when the game leaves
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x0F00;
	__dpmi_int(0x10, &r);
	oldVideoMode = (uint8_t)(r.h.al & 0x7F);

	memset(&r, 0, sizeof(r));
	r.x.ax = 0x0013;
	__dpmi_int(0x10, &r);

	//chain 4 off, which is what turns mode 13h into mode X
	outportb(0x3C4, 0x04);
	outportb(0x3C5, (uint8_t)((inportb(0x3C5) & ~0x08) | 0x04));
	//every plane writable, so the screen can be cleared in one go
	outportb(0x3C4, 0x02);
	outportb(0x3C5, 0x0F);

	//the timing of a 480 line screen, which 240 lines are shown double height in
	outportb(0x3C2, 0xE3);
	//the CRTC holds its own settings until it is unlocked
	outportb(0x3D4, 0x11);
	outportb(0x3D5, (uint8_t)(inportb(0x3D5) & 0x7F));

	//byte addressing rather than the doubleword addressing mode 13h uses
	outportb(0x3D4, 0x14);
	outportb(0x3D5, (uint8_t)(inportb(0x3D5) & ~0x40));
	outportb(0x3D4, 0x17);
	outportb(0x3D5, (uint8_t)(inportb(0x3D5) | 0x40));

	//the rest of the timing: 240 visible lines out of a 480 line frame
	static const uint8_t crtc[][2] = {
		{0x06, 0x0D}, {0x07, 0x3E}, {0x09, 0x41}, {0x10, 0xEA},
		{0x11, 0xAC}, {0x12, 0xDF}, {0x15, 0xE7}, {0x16, 0x06},
	};
	for (size_t i = 0; i < sizeof(crtc) / sizeof(crtc[0]); i++)
	{
		outportb(0x3D4, crtc[i][0]);
		outportb(0x3D5, crtc[i][1]);
	}
}

static void SetTextMode(void)
{
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = oldVideoMode ? oldVideoMode : 0x0003;
	__dpmi_int(0x10, &r);
}

static void DisplayInit(void)
{
	//Video memory is where the machine keeps it, at A0000, which a protected mode program can not
	//simply write to. With near pointers it can, and every DPMI host a DOS machine has allows them
	nearPointers = (__djgpp_nearptr_enable() != 0);
	if (nearPointers)
		screen = (uint8_t*)(__djgpp_conventional_base + 0xA0000);
	else
		Platform_Log("no near pointers, the screen stays blank\n");

	ScaleInit();
	SetModeX();
	PaletteInit();
	//everything the mode left behind, cleared in all four planes at once
	if (screen)
	{
		outportb(0x3C4, 0x02);
		outportb(0x3C5, 0x0F);
		memset(screen, 0, PLANE_STRIDE * SCREEN_DOS_HEIGHT);
	}
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	//uclock counts the ticks of the timer, 1193180 of them a second
	return (uint32_t)((uint64_t)uclock() * 1000000ULL / UCLOCKS_PER_SEC);
}

//The PC speaker, which makes a square wave of one frequency and nothing else. That is exactly what
//the game asks for, so the tone goes straight to it
static void ToneOff(void)
{
	//the speaker is disconnected from the timer and switched off
	outportb(0x61, (uint8_t)(inportb(0x61) & ~0x03));
	toneEnd = 0;
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	ToneOff();
	//a frequency of 0 is a rest, and the volume is only ever on or off on a speaker like this
	if ((freq == 0) || (SOUNDVOLUME == 0))
		return;
	//the timer counts down from this, 1193180 times a second
	uint32_t divisor = 1193180u / freq;
	if (divisor > 0xFFFF)
		divisor = 0xFFFF;
	if (divisor == 0)
		divisor = 1;
	//channel 2, both bytes, square wave
	outportb(0x43, 0xB6);
	outportb(0x42, (uint8_t)(divisor & 0xFF));
	outportb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
	//the speaker connected to the timer and switched on
	outportb(0x61, (uint8_t)(inportb(0x61) | 0x03));

	//0 keeps playing until the next tone, the others end in Platform_Micros' own time
	if (duration)
	{
		toneEnd = Platform_Micros() + (uint32_t)duration * 1000u;
		//a tone that ends exactly at 0 microseconds would be taken for one that plays on
		if (toneEnd == 0)
			toneEnd = 1;
	}
	else
		toneEnd = 0;
}

void Platform_StopTone(void)
{
	ToneOff();
}

uint32_t Platform_FreeHeap(void)
{
	//what the DPMI host says is left of the memory it hands out
	__dpmi_free_mem_info info;
	if (__dpmi_get_free_memory_information(&info) != 0)
		return 0;
	return (uint32_t)info.largest_available_free_block_in_bytes;
}

uint32_t Platform_FreeStack(void)
{
	return 0;
}

uint32_t Platform_RandomSeed(void)
{
	//the timer as it stands, which is never quite the same twice
	return (uint32_t)uclock();
}

void Platform_Log(const char* format, ...)
{
	//Whatever is printed while the game is on screen would land in the middle of it, so this is kept
	//for what is written before the mode is set and after it is put back
	if (screen)
		return;
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in a file next to the program. Bytes the file does not have yet
// read as 0xFF, the way erased flash does on a handheld, so the game sees a store never written to.
// ===========================================================================

static void StorageLoad(uint8_t* block)
{
	memset(block, 0xFF, PLATFORM_STORAGE_SIZE);
	FILE* file = fopen(storagePath, "rb");
	if (file)
	{
		size_t got = fread(block, 1, PLATFORM_STORAGE_SIZE, file);
		(void)got;
		fclose(file);
	}
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	uint8_t block[PLATFORM_STORAGE_SIZE];
	StorageLoad(block);
	memcpy(data, block + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	uint8_t block[PLATFORM_STORAGE_SIZE];
	StorageLoad(block);
	//storing what is already there does not touch the file
	if (memcmp(block + offset, data, length) == 0)
		return;
	memcpy(block + offset, data, length);
	FILE* file = fopen(storagePath, "wb");
	if (!file)
		return;
	fwrite(block, 1, PLATFORM_STORAGE_SIZE, file);
	fclose(file);
}

// ===========================================================================
// Start and finish
// ===========================================================================

static void Shutdown(void)
{
	ToneOff();
	ButtonsDone();
	screen = NULL;
	SetTextMode();
	if (nearPointers)
	{
		__djgpp_nearptr_disable();
		nearPointers = false;
	}
}

void Platform_Init(const char* appName)
{
	//the save file takes the first word of the name, "Blips v1.0" saves to BLIPS.SAV
	size_t n = 0;
	while (appName[n] && (appName[n] != ' ') && (n < sizeof(storagePath) - 5))
	{
		storagePath[n] = (char)((appName[n] >= 'a' && appName[n] <= 'z') ? appName[n] - 32 : appName[n]);
		n++;
	}
	if (n)
	{
		//DOS takes eight characters and three, so a longer name is cut down to fit
		if (n > 8)
			n = 8;
		memcpy(storagePath + n, ".SAV", 5);
	}

	//whatever happens from here, the screen and the keyboard are put back the way they were
	atexit(Shutdown);
	ButtonsInit();
	DisplayInit();
#if SCREENBUFFER
	screenBuffer.setColorDepth(SCREENBUFFER);
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
#endif
}

#endif
