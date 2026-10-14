//Platform.h for the Nintendo 3DS, built with devkitARM and libctru (see 3ds/CMakeLists.txt):
//  display  the top screen, 400x240 of RGB565 pixels. The game's 128x128 screen buffer is written
//           into it scaled to 240x240 in the middle, the bottom screen stays dark
//  buttons  d-pad or circle pad, A, B, L and R
//  sound    a square wave on one of the DSP's channels, silent when the DSP has no firmware
//  saves    a file on the SD card, in sdmc:/3ds/<game>/
//  time     the ARM11's own tick counter, microseconds
//
//main() here starts the game and runs Game_Loop once every frame, the game holds its frames to its
//own frame rate itself.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_3DS

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/stat.h>
#include <3ds.h>
#include "PlatformGamebuinoFont.h"

//the top screen, the one the game is shown on. Its pixels are kept in columns, bottom to top
#define SCREEN_TOP_WIDTH 400
#define SCREEN_TOP_HEIGHT 240

static Platform3DSDisplay display;
PlatformDisplay& platformDisplay = display;

//the whole frame is drawn in here and written to the screen when the frame is done
PlatformBuffer screenBuffer;

#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as an RGB565 pixel
static uint16_t bufferPalette[256];
#endif

//the game pixel every screen row and column of the frame comes from, worked out once
static uint16_t frameColumn[SCREEN_TOP_HEIGHT], frameRow[SCREEN_TOP_HEIGHT];
//where the frame sits on the screen and how big it is
static int frameX = 0, frameSize = WINDOW_WIDTH;

//whether sound started at all, and whether it plays through CSND rather than the DSP (see SoundInit)
static bool soundReady = false;
static bool useCsnd = false;
//the microsecond a tone ends at, 0 while none has to end
static uint32_t toneEnd = 0;

static void ToneOff(void);
static void StorageFlush(void);

// ===========================================================================
// Program start
// ===========================================================================

int main(void)
{
	Game_Setup();
	while (aptMainLoop())
	{
		//a tone that has played long enough stops here, the ones with no length of their own play
		//until the next tone
		if (toneEnd && ((int32_t)(Platform_Micros() - toneEnd) >= 0))
			ToneOff();
		Game_Loop();
		//a save that was written this frame reaches the card here, once the drawing is done
		StorageFlush();
	}
	ToneOff();
	if (soundReady)
	{
		if (useCsnd)
			csndExit();
		else
			ndspExit();
	}
	gfxExit();
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

//everything is drawn into the screen buffer, these are here for the paths that ask for them
void Platform3DSDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void Platform3DSDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void Platform3DSDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void Platform3DSDisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}

void Platform3DSGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t Platform3DSGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

//the screen buffer, read and written for every pixel the game draws and again when the frame is sent
bool Platform3DSBuffer::createSprite(int32_t w, int32_t h)
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

void Platform3DSBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
#else
	for (int32_t row = y; row < y + h; row++)
		for (int32_t column = x; column < x + w; column++)
			SetBufferBit(pixels, column, row, color);
#endif
}

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	bufferSetColor = setColor;
	bufferClearColor = clearColor;
#else
	(void)setColor;
	(void)clearColor;
#endif
}

//bytes of one row of the screen buffer
#if SCREENBUFFER == 16
#define BUFFER_ROW_BYTES (WINDOW_WIDTH * 2)
#elif SCREENBUFFER == 8
#define BUFFER_ROW_BYTES WINDOW_WIDTH
#else
#define BUFFER_ROW_BYTES ((WINDOW_WIDTH + 7) / 8)
#endif

//the RGB565 pixel of game pixel x of a buffer row
static inline uint16_t BufferPixel(const uint8_t* row, uint16_t x)
{
#if SCREENBUFFER == 16
	//byte swapped in the buffer
	return (uint16_t)((row[x * 2] << 8) | row[x * 2 + 1]);
#elif SCREENBUFFER == 8
	return bufferPalette[row[x]];
#else
	//most significant bit first as SetBufferBit writes
	return (row[x >> 3] & (0x80 >> (x & 7))) ? bufferSetColor : bufferClearColor;
#endif
}

//Writes the frame the game drew to the top screen. The screen keeps its pixels in columns, from the
//bottom of the screen up, so a column of the screen is written in one run and the game's rows are
//read across the buffer
void Platform_PresentFrame(void)
{
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	uint16_t* fb = (uint16_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	if (fb)
	{
		for (int column = 0; column < frameSize; column++)
		{
			//the screen column this one lands in, and where its pixels start
			uint16_t* d = &fb[(frameX + column) * SCREEN_TOP_HEIGHT];
			const uint16_t gameX = frameColumn[column];
			for (int row = 0; row < frameSize; row++)
			{
				const uint8_t* line = &src[frameRow[row] * BUFFER_ROW_BYTES];
				//the screen counts its rows from the bottom
				d[SCREEN_TOP_HEIGHT - 1 - row] = BufferPixel(line, gameX);
			}
		}
	}
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

static void DisplayInit(void)
{
	gfxInitDefault();
	//RGB565 like the game's own pixels, so nothing is converted per pixel
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	//the bottom screen has nothing on it
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
	for (int i = 0; i < 2; i++)
	{
		uint16_t width = 0, height = 0;
		uint16_t* fb = (uint16_t*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
		if (fb)
			memset(fb, 0, (size_t)width * height * sizeof(uint16_t));
		fb = (uint16_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
		if (fb)
			memset(fb, 0, (size_t)width * height * sizeof(uint16_t));
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}

	//how big the frame is on screen and which game pixel every screen pixel of it comes from
#if SCALESCREEN
	frameSize = SCREEN_TOP_HEIGHT;
#else
	frameSize = WINDOW_WIDTH;
#endif
	frameX = (SCREEN_TOP_WIDTH - frameSize) / 2;
	for (int i = 0; i < frameSize; i++)
	{
		frameColumn[i] = (uint16_t)((i * WINDOW_WIDTH) / frameSize);
		frameRow[i] = (uint16_t)((i * WINDOW_HEIGHT) / frameSize);
	}
}

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	hidScanInput();
	//KEY_UP and the others are the d-pad and the circle pad together
	const uint32_t keys = hidKeysHeld();
	uint8_t buttons = 0;
	if (keys & KEY_LEFT)
		buttons |= BUTTON_LEFT;
	if (keys & KEY_UP)
		buttons |= BUTTON_UP;
	if (keys & KEY_DOWN)
		buttons |= BUTTON_DOWN;
	if (keys & KEY_RIGHT)
		buttons |= BUTTON_RIGHT;
	if (keys & KEY_A)
		buttons |= BUTTON_A;
	if (keys & KEY_B)
		buttons |= BUTTON_B;
	if (keys & KEY_L)
		buttons |= BUTTON_L;
	if (keys & KEY_R)
		buttons |= BUTTON_R;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

//the tick the game started at, so the microseconds start near zero and wrap where the game expects
static uint64_t startTick = 0;

uint32_t Platform_Micros(void)
{
	const uint64_t ticks = svcGetSystemTick() - startTick;
	return (uint32_t)(ticks / (uint64_t)(SYSCLOCK_ARM11 / 1000000));
}

//One cycle of a square wave as 16 bit samples, looped for as long as the note plays. The DSP mixes
//at 32728 Hz and a channel can not be asked for more, so the note's pitch is the length of the
//cycle rather than the rate the samples go out at: a cycle of 32728 / freq samples. The DSP reads
//the samples itself, so they live in memory it can reach
#define TONE_RATE 32728
#define TONE_MAX_SAMPLES 2048
#define TONE_AMPLITUDE 8000
#define TONE_CHANNEL 0
static int16_t* toneWave = NULL;
static ndspWaveBuf toneBuf;

static void ToneOff(void)
{
	if (soundReady)
	{
		if (useCsnd)
		{
			CSND_SetPlayState(TONE_CHANNEL, 0);
			csndExecCmds(false);
		}
		else
			ndspChnWaveBufClear(TONE_CHANNEL);
	}
	toneEnd = 0;
}

static void SoundInit(void)
{
	//The DSP only starts with a firmware that has been dumped to the card it plays from, which
	//neither a fresh console nor an emulator has. CSND needs nothing, so it is what the tones are
	//played with when the DSP is not there
	if (R_SUCCEEDED(ndspInit()))
		useCsnd = false;
	else if (R_SUCCEEDED(csndInit()))
		useCsnd = true;
	else
	{
		Platform_Log("sound: neither the DSP nor CSND started, the game plays without them\n");
		return;
	}
	toneWave = (int16_t*)linearAlloc(TONE_MAX_SAMPLES * sizeof(int16_t));
	if (!toneWave)
	{
		if (useCsnd)
			csndExit();
		else
			ndspExit();
		return;
	}
	if (useCsnd)
	{
		//CSND is told everything about a note when it is played, nothing to set up here
		soundReady = true;
		return;
	}
	ndspSetMasterVol(1.0f);
	ndspSetOutputMode(NDSP_OUTPUT_MONO);
	ndspChnSetInterp(TONE_CHANNEL, NDSP_INTERP_NONE);
	ndspChnSetFormat(TONE_CHANNEL, NDSP_FORMAT_MONO_PCM16);
	ndspChnSetRate(TONE_CHANNEL, (float)TONE_RATE);
	float mix[12];
	memset(mix, 0, sizeof(mix));
	mix[0] = mix[1] = SOUNDVOLUME / 100.0f;
	ndspChnSetMix(TONE_CHANNEL, mix);
	soundReady = true;
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	if (!soundReady)
		return;
	ndspChnWaveBufClear(TONE_CHANNEL);
	//a frequency of 0 is a rest
	if (freq == 0)
	{
		toneEnd = 0;
		return;
	}
	//the cycle that carries this note, half of it high and half of it low
	int samples = (TONE_RATE + freq / 2) / freq;
	if (samples < 2)
		samples = 2;
	if (samples > TONE_MAX_SAMPLES)
		samples = TONE_MAX_SAMPLES;
	const int high = samples / 2;
	for (int i = 0; i < samples; i++)
		toneWave[i] = (i < high) ? TONE_AMPLITUDE : -TONE_AMPLITUDE;
	DSP_FlushDataCache(toneWave, (size_t)samples * sizeof(int16_t));

	if (useCsnd)
	{
		//the cycle repeats for as long as the note plays. CSND reads it itself, so what the cache
		//holds has to be in memory
		GSPGPU_FlushDataCache(toneWave, (u32)samples * sizeof(int16_t));
		csndPlaySound(TONE_CHANNEL, SOUND_REPEAT | SOUND_FORMAT_16BIT, TONE_RATE,
		              SOUNDVOLUME / 100.0f, 0.0f, toneWave, toneWave, (u32)samples * sizeof(int16_t));
		csndExecCmds(false);
	}
	else
	{
		memset(&toneBuf, 0, sizeof(toneBuf));
		toneBuf.data_vaddr = toneWave;
		toneBuf.nsamples = samples;
		toneBuf.looping = true;
		ndspChnSetPaused(TONE_CHANNEL, false);
		ndspChnWaveBufAdd(TONE_CHANNEL, &toneBuf);
	}
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

uint32_t Platform_FreeHeap(void)
{
	//what the loader left for the program, plus what malloc was given back
	return (uint32_t)(osGetMemRegionFree(MEMREGION_APPLICATION) + mallinfo().fordblks);
}

//The loader gives the main thread a stack of __stacksize__ bytes, which the symbol carries as its
//address, and nothing says where it ends. What is left is counted from where the stack stood when
//the game started instead
extern char __stacksize__[];
static char* stackStart = NULL;

uint32_t Platform_FreeStack(void)
{
	uint32_t size = (uint32_t)(uintptr_t)__stacksize__;
	//32 KB is what a program gets unless it asks for more
	if ((size < 4096) || (size > 1024 * 1024))
		size = 32 * 1024;
	if (!stackStart)
		return size;
	const uint32_t used = (uint32_t)(stackStart - (char*)__builtin_frame_address(0));
	return (used < size) ? (size - used) : 0;
}

uint32_t Platform_RandomSeed(void)
{
	return (uint32_t)svcGetSystemTick();
}

void Platform_Log(const char* format, ...)
{
	//goes to the debugger of an emulator, the screens are the game's
	char message[128];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	svcOutputDebugString(message, strlen(message));
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in memory and in a file on the SD card, which is written at the
// end of a frame it changed in.
// ===========================================================================

static uint8_t storage[PLATFORM_STORAGE_SIZE];
static char storagePath[96];
static bool storageOnCard = false;
static bool storageChanged = false;

static void StorageInit(const char* appName)
{
	memset(storage, 0xFF, sizeof(storage));
	char name[32];
	snprintf(name, sizeof(name), "%s", (appName && appName[0]) ? appName : "game");
	//the name is a folder on the card, a space in it would do no harm but is easier to type without
	for (char* c = name; *c; c++)
		if ((*c == ' ') || (*c == '/') || (*c == '\\'))
			*c = '_';
	char folder[64];
	snprintf(folder, sizeof(folder), "sdmc:/3ds/%s", name);
	mkdir("sdmc:/3ds", 0777);
	mkdir(folder, 0777);
	snprintf(storagePath, sizeof(storagePath), "%s/%s.sav", folder, name);
	storageOnCard = true;
	FILE* file = fopen(storagePath, "rb");
	if (!file)
		return;
	fread(storage, 1, sizeof(storage), file);
	fclose(file);
}

//writes the block to the card, called at the end of a frame it changed in
static void StorageFlush(void)
{
	if (!storageChanged)
		return;
	storageChanged = false;
	if (!storageOnCard)
		return;
	FILE* file = fopen(storagePath, "wb");
	if (!file)
		return;
	fwrite(storage, 1, sizeof(storage), file);
	fclose(file);
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	if (memcmp(storage + offset, data, length) == 0)
		return;
	memcpy(storage + offset, data, length);
	storageChanged = true;
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	startTick = svcGetSystemTick();
	stackStart = (char*)__builtin_frame_address(0);
	DisplayInit();
	SoundInit();
	StorageInit(appName);
	screenBuffer.setColorDepth(SCREENBUFFER);
	screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT);
#if SCREENBUFFER == 8
	//RGB332 to RGB565 the way LovyanGFX converts it, so the colours match the other devices
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
		bufferPalette[i] = (uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3));
	}
#endif
}

#endif
