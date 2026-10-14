//Platform.h for the PlayStation Portable, built with the pspdev toolchain (see psp/CMakeLists.txt):
//  display  480x272, 16 bit pixels in VRAM, two buffers so a frame is never seen half drawn. The
//           game's 128x128 frame is doubled to 256x256 in the middle of it
//  buttons  d-pad (the analog stick works as one too), cross, circle, L and R
//  sound    a square wave written into the sound channel by pspaudiolib's thread
//  saves    a file next to the EBOOT, ms0:/PSP/GAME/<game>/<game>.sav
//  time     the system clock, which counts microseconds
//
//main() here starts the game and runs Game_Loop until the PSP is asked to quit, the game holds its
//frames to its own frame rate itself.
//
//The PSP's 16 bit pixel format has red in the low bits, the other way round from the RGB565 the game
//works in, so pixels are turned around on their way to the display.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_PSP

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>
#include <psputils.h>
#include <pspaudiolib.h>
#include "PlatformGamebuinoFont.h"

//the name the PSP's menus show, and what the save file is called. The build passes it
#ifndef PSP_APP_NAME
#define PSP_APP_NAME "Game"
#endif

PSP_MODULE_INFO(PSP_APP_NAME, 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
//a row of the display is 512 pixels apart in VRAM, not 480
#define VRAM_STRIDE 512
#define VRAM_BUFFER_PIXELS (VRAM_STRIDE * SCREEN_HEIGHT)

//How many display pixels a game pixel is wide and high, and where the frame sits on the display.
//The game has a SCALE of its own in defines.h, so this one carries the platform's name
#if SCALESCREEN
#define PSP_SCALE 2
#else
#define PSP_SCALE 1
#endif
#define OUT_WIDTH (WINDOW_WIDTH * PSP_SCALE)
#define OUT_HEIGHT (WINDOW_HEIGHT * PSP_SCALE)
#define OFFSET_X ((SCREEN_WIDTH - OUT_WIDTH) / 2)
#define OFFSET_Y ((SCREEN_HEIGHT - OUT_HEIGHT) / 2)

#define AUDIO_SAMPLE_RATE 44100

static PlatformPSPDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the display when it is done
PlatformBuffer screenBuffer;
#else
//without a buffer the game draws into this frame, which is sent to the display the same way
static uint16_t frame[WINDOW_WIDTH * WINDOW_HEIGHT];
#endif
#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in, in the display's own pixel format
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as a pixel of the display's format
static uint16_t bufferPalette[256];
#endif

//the two buffers in VRAM, one on the display while the other is drawn into
static uint16_t* vramBuffer[2] = { nullptr, nullptr };
static uint8_t drawBuffer = 0;
//set while the PSP has not been asked to quit
static volatile bool running = true;
//the folder the EBOOT was started from, where the save file goes
static char storagePath[256] = "";

//RGB565, red in the top bits, to the display's pixel, red in the low bits
static inline uint16_t ToDisplay(uint16_t color)
{
	return (uint16_t)(((color >> 11) & 0x1F) | (((color >> 5) & 0x3F) << 5) | ((color & 0x1F) << 11));
}

// ===========================================================================
// Program start
// ===========================================================================

//the PSP asks the program to quit through this, HOME and the power switch both end up here
static int ExitCallback(int arg1, int arg2, void* common)
{
	(void)arg1;
	(void)arg2;
	(void)common;
	running = false;
	return 0;
}

static int CallbackThread(SceSize args, void* argp)
{
	(void)args;
	(void)argp;
	const int id = sceKernelCreateCallback("ExitCallback", ExitCallback, nullptr);
	sceKernelRegisterExitCallback(id);
	sceKernelSleepThreadCB();
	return 0;
}

static void SetupCallbacks(void)
{
	const int thread = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, nullptr);
	if (thread >= 0)
		sceKernelStartThread(thread, 0, nullptr);
}

static void DisplayInit(void)
{
	//VRAM starts with the two buffers, one after the other
	uint8_t* vram = (uint8_t*)sceGeEdramGetAddr();
	//bit 30 reaches VRAM without going through the cache, so what is written is on the display at once
	vram = (uint8_t*)((uintptr_t)vram | 0x40000000);
	vramBuffer[0] = (uint16_t*)vram;
	vramBuffer[1] = (uint16_t*)(vram + VRAM_BUFFER_PIXELS * 2);
	//black around the frame, in both buffers
	memset(vramBuffer[0], 0, VRAM_BUFFER_PIXELS * 2);
	memset(vramBuffer[1], 0, VRAM_BUFFER_PIXELS * 2);

	sceDisplaySetMode(0, SCREEN_WIDTH, SCREEN_HEIGHT);
	//NEXTFRAME, not IMMEDIATE: a buffer handed over with IMMEDIATE never reaches the display
	sceDisplaySetFrameBuf(vramBuffer[1], VRAM_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_565,
	                      PSP_DISPLAY_SETBUF_NEXTFRAME);
	drawBuffer = 0;
}

int main(int argc, char* argv[])
{
	SetupCallbacks();
	//the save file goes next to the EBOOT the PSP started
	if (argc > 0 && argv[0])
	{
		snprintf(storagePath, sizeof(storagePath), "%s", argv[0]);
		char* slash = strrchr(storagePath, '/');
		if (slash)
			slash[1] = '\0';
		else
			storagePath[0] = '\0';
	}
	strncat(storagePath, PSP_APP_NAME ".sav", sizeof(storagePath) - strlen(storagePath) - 1);

	Game_Setup();
	while (running)
		Game_Loop();
	sceKernelExitGame();
	return 0;
}

// ===========================================================================
// Drawing
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
//everything is drawn into the screen buffer
void PlatformPSPDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformPSPDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformPSPDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformPSPDisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the frame that is sent to the display. The area
//the pixels go into and where the next one goes, in game coordinates
static int32_t windowLeft = 0, windowRight = -1;
static int32_t cursorX = 0, cursorY = 0;

void PlatformPSPDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	windowLeft = x;
	windowRight = x + w - 1;
	cursorX = x;
	cursorY = y;
}

//the next pixel of the window, what falls outside the game's screen is left out
static inline void WritePixel(uint16_t color)
{
	if ((cursorX >= 0) && (cursorX < WINDOW_WIDTH) && (cursorY >= 0) && (cursorY < WINDOW_HEIGHT))
		frame[cursorY * WINDOW_WIDTH + cursorX] = color;
	if (++cursorX > windowRight)
	{
		cursorX = windowLeft;
		cursorY++;
	}
}

void PlatformPSPDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8));
		WritePixel(value);
	}
}

void PlatformPSPDisplay::writeColor(uint16_t color, uint32_t length)
{
	while (length--)
		WritePixel(color);
}

void PlatformPSPDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &frame[row * WINDOW_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = color;
	}
}
#endif

void PlatformPSPGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

//Draws the character the way LovyanGFX draws its default font: a 6x8 cell times the text
//size, the 5 columns of the glyph and a column of background, the background only when it
//differs from the text colour. Every column goes out as runs of one colour
size_t PlatformPSPGFX::drawChar(uint16_t c, int32_t x, int32_t y)
{
	const int32_t size = textSize;
	//the 'classic' character set LovyanGFX uses unless told otherwise
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

bool PlatformPSPBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformPSPBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	bufferSetColor = ToDisplay(setColor);
	bufferClearColor = ToDisplay(clearColor);
#else
	(void)setColor;
	(void)clearColor;
#endif
}

// ===========================================================================
// The frame on the display
// ===========================================================================

//The pixel of game column x of a row of what was drawn, in the display's own format. The screen
//buffer keeps its pixels the way LovyanGFX does, which is not the display's layout
static inline uint16_t FramePixel(const uint8_t* row, int32_t x)
{
#if SCREENBUFFER == 16
	const uint16_t value = ((const uint16_t*)(const void*)row)[x];
	//byte swapped in the buffer
	return ToDisplay((uint16_t)((value >> 8) | (value << 8)));
#elif SCREENBUFFER == 8
	return bufferPalette[row[x]];
#elif SCREENBUFFER == 1
	//most significant bit first as SetBufferBit writes
	return (row[x >> 3] & (0x80 >> (x & 7))) ? bufferSetColor : bufferClearColor;
#else
	return ToDisplay(((const uint16_t*)(const void*)row)[x]);
#endif
}

//bytes of one row of what was drawn
#if SCREENBUFFER == 8
#define FRAME_ROW_BYTES WINDOW_WIDTH
#elif SCREENBUFFER == 1
#define FRAME_ROW_BYTES ((WINDOW_WIDTH + 7) / 8)
#else
#define FRAME_ROW_BYTES (WINDOW_WIDTH * 2)
#endif

void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
#else
	const uint8_t* src = (const uint8_t*)frame;
#endif
	if (!src || !vramBuffer[drawBuffer])
		return;

	//the buffer that is not on the display is drawn into, so nothing is ever seen half drawn
	uint16_t* out = vramBuffer[drawBuffer] + OFFSET_Y * VRAM_STRIDE + OFFSET_X;
	for (int32_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * FRAME_ROW_BYTES];
#if PSP_SCALE == 2
		//a game pixel is two display pixels wide: both of them are written as one 32 bit value, and
		//the row of them is copied to make it two display rows high
		uint32_t* line = (uint32_t*)(void*)out;
		for (int32_t x = 0; x < WINDOW_WIDTH; x++)
		{
			const uint32_t pixel = FramePixel(row, x);
			line[x] = pixel | (pixel << 16);
		}
		memcpy(out + VRAM_STRIDE, out, OUT_WIDTH * 2);
		out += VRAM_STRIDE * 2;
#else
		for (int32_t x = 0; x < WINDOW_WIDTH; x++)
			out[x] = FramePixel(row, x);
		out += VRAM_STRIDE;
#endif
	}

	//The buffer goes on the display at the next vertical blank, and the wait is for that blank: only
	//then is the other buffer off the display and safe for the next frame to be drawn into
	sceDisplaySetFrameBuf(vramBuffer[drawBuffer], VRAM_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_565,
	                      PSP_DISPLAY_SETBUF_NEXTFRAME);
	sceDisplayWaitVblankStart();
	drawBuffer ^= 1;
}

// ===========================================================================
// Buttons
// ===========================================================================

//how far the analog stick has to be pushed from the middle before it counts as a direction
#define ANALOG_THRESHOLD 48

uint8_t Platform_GetButtons(void)
{
	SceCtrlData pad;
	sceCtrlPeekBufferPositive(&pad, 1);
	uint8_t buttons = 0;
	if (pad.Buttons & PSP_CTRL_LEFT)
		buttons |= BUTTON_LEFT;
	if (pad.Buttons & PSP_CTRL_UP)
		buttons |= BUTTON_UP;
	if (pad.Buttons & PSP_CTRL_DOWN)
		buttons |= BUTTON_DOWN;
	if (pad.Buttons & PSP_CTRL_RIGHT)
		buttons |= BUTTON_RIGHT;
	if (pad.Buttons & PSP_CTRL_CROSS)
		buttons |= BUTTON_A;
	if (pad.Buttons & PSP_CTRL_CIRCLE)
		buttons |= BUTTON_B;
	if (pad.Buttons & PSP_CTRL_LTRIGGER)
		buttons |= BUTTON_L;
	if (pad.Buttons & PSP_CTRL_RTRIGGER)
		buttons |= BUTTON_R;
	//the analog stick works as a d-pad as well, as the PyGamer's joystick does
	const int x = (int)pad.Lx - 128, y = (int)pad.Ly - 128;
	if (x < -ANALOG_THRESHOLD)
		buttons |= BUTTON_LEFT;
	if (x > ANALOG_THRESHOLD)
		buttons |= BUTTON_RIGHT;
	if (y < -ANALOG_THRESHOLD)
		buttons |= BUTTON_UP;
	if (y > ANALOG_THRESHOLD)
		buttons |= BUTTON_DOWN;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return (uint32_t)sceKernelGetSystemTimeLow();
}

//the tone playing: its frequency (0 = silence), how many samples are left of it (0 = until the next
//tone or Platform_StopTone), and where in its period the wave is, in 1/65536 of a period. The sound
//thread reads these while the game writes them, a tone starting a moment late is not worth locking for
static volatile uint32_t toneFreq = 0;
static volatile uint32_t toneSamplesLeft = 0;
static uint32_t tonePhase = 0;

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	//a frequency of 0 is a rest
	toneSamplesLeft = duration ? (uint32_t)((uint64_t)duration * AUDIO_SAMPLE_RATE / 1000) : 0;
	if (duration && !toneSamplesLeft)
		toneSamplesLeft = 1;
	tonePhase = 0;
	toneFreq = freq;
}

//pspaudiolib's thread asks for the next block of samples here, two channels of 16 bit samples
static void AudioCallback(void* buffer, unsigned int samples, void* userdata)
{
	(void)userdata;
	int16_t* out = (int16_t*)buffer;
	const int16_t level = (int16_t)(16000 * SOUNDVOLUME / 100);
	for (unsigned int i = 0; i < samples; i++)
	{
		int16_t value = 0;
		const uint32_t freq = toneFreq;
		if (freq)
		{
			value = (tonePhase < 32768) ? level : (int16_t)-level;
			tonePhase = (tonePhase + (uint32_t)((uint64_t)freq * 65536 / AUDIO_SAMPLE_RATE)) & 0xFFFF;
			if (toneSamplesLeft && (--toneSamplesLeft == 0))
				toneFreq = 0;
		}
		out[i * 2] = value;
		out[i * 2 + 1] = value;
	}
}

uint32_t Platform_FreeHeap(void)
{
	return (uint32_t)sceKernelTotalFreeMemSize();
}

//the PSP does not say how much of the thread's stack is left
uint32_t Platform_FreeStack(void)
{
	return 0;
}

uint32_t Platform_RandomSeed(void)
{
	return (uint32_t)sceKernelGetSystemTimeLow();
}

void Platform_Log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	//only reaches a computer with psplink, on the PSP itself it goes nowhere
	vprintf(format, args);
	va_end(args);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in a file next to the EBOOT. Bytes the file does not have yet read
// as 0xFF, the way erased flash does on the ESPboy, so the game sees a never saved store.
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
	{
		Platform_Log("could not write %s\n", storagePath);
		return;
	}
	fwrite(block, 1, PLATFORM_STORAGE_SIZE, file);
	fclose(file);
}

// ===========================================================================
// Start up
// ===========================================================================

void Platform_Init(const char* appName)
{
	(void)appName;
	DisplayInit();

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	pspAudioInit();
	pspAudioSetChannelCallback(0, AudioCallback, nullptr);

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
		bufferPalette[i] = ToDisplay(color);
	}
#endif
}

#endif
