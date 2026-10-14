//Platform.h for Windows (and anything else SDL2 runs on). LovyanGFX's SDL panel opens the
//window and keeps it updated from the main thread, the game runs in a thread of its own.
//
//Keys:  arrows = d-pad,  X = A,  C = B,  S = left side button,  D = right side button

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_SDL

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <LGFX_AUTODETECT.hpp>

//how much bigger than the game's resolution the window opens, it can be resized after
//A build can still set it itself
#ifndef WINDOW_SCALE
#define WINDOW_SCALE 4
#endif

static LGFX display(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_SCALE);
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the window once at the end of Game_Loop
PlatformBuffer screenBuffer(&display);
#endif
#if (SCREENBUFFER == 1) && LOVYANGFX
//the two colours a 1 bpp frame is shown in, in display byte order
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if (SCREENBUFFER == 8) && LOVYANGFX
//every RGB332 value as a 16 bit pixel in display byte order, see Platform_PresentFrame
static uint16_t bufferPalette[256];
#endif

//the save storage is a file next to where the game is started from, named after the game
static char storagePath[64] = "game.sav";

// ===========================================================================
// Program start
// ===========================================================================

static int GameThread(bool* running)
{
	Game_Setup();
	while (*running)
	{
		Game_Loop();
		//Game_Loop returns straight away while it is not yet time for the next frame,
		//without a pause this thread would keep a whole core busy waiting
		SDL_Delay(1);
	}
	return 0;
}

int main(int, char**)
{
	//runs the window until it is closed, and GameThread next to it
	return lgfx::Panel_sdl::main(GameThread);
}

// ===========================================================================
// Sound: a square wave generated in the SDL audio callback
// ===========================================================================

#define AUDIO_RATE 22050
#define AUDIO_VOLUME 3000

static SDL_AudioDeviceID audioDevice = 0;
static uint32_t toneHalfWave = 0;      //samples per half wave, 0 is silence
static uint32_t toneSamplesLeft = 0;   //samples until the tone stops, unless it plays on
static bool tonePlaysOn = false;
static uint32_t tonePosition = 0;

static void SDLCALL AudioCallback(void*, Uint8* stream, int len)
{
	int16_t* out = (int16_t*)stream;
	int samples = len / (int)sizeof(int16_t);
	for (int i = 0; i < samples; i++)
	{
		if (toneHalfWave && (tonePlaysOn || toneSamplesLeft))
		{
			out[i] = ((tonePosition / toneHalfWave) & 1) ? AUDIO_VOLUME : -AUDIO_VOLUME;
			tonePosition++;
			if (!tonePlaysOn)
				toneSamplesLeft--;
		}
		else
			out[i] = 0;
	}
}

static void OpenAudio(void)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		Platform_Log("no audio: %s\n", SDL_GetError());
		return;
	}
	SDL_AudioSpec wanted;
	SDL_zero(wanted);
	wanted.freq = AUDIO_RATE;
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 1;
	wanted.samples = 512;
	wanted.callback = AudioCallback;
	audioDevice = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
	if (!audioDevice)
	{
		Platform_Log("no audio device: %s\n", SDL_GetError());
		return;
	}
	SDL_PauseAudioDevice(audioDevice, 0);
}

// ===========================================================================
// Display
// ===========================================================================

void Platform_Init(const char* appName)
{
	//the save file takes the first word of the name, "Blockdude v1.0" saves to Blockdude.sav
	size_t n = 0;
	while (appName[n] && (appName[n] != ' ') && (n < sizeof(storagePath) - 5))
		n++;
	if (n)
		snprintf(storagePath, sizeof(storagePath), "%.*s.sav", (int)n, appName);

	((lgfx::Panel_sdl*)display.getPanel())->setWindowTitle(appName);
	display.init();
	OpenAudio();
#if SCREENBUFFER
	//LovyanGFX makes a 1 bpp sprite a two colour palette sprite, drawing into it only
	//looks at the lowest bit of a colour: ColorWhite sets a bit, ColorBlack clears it
	screenBuffer.setColorDepth(SCREENBUFFER);
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
#endif
#if (SCREENBUFFER == 8) && LOVYANGFX
	//the library's own RGB332 to RGB565 conversion, so the colours are the ones pushSprite showed
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

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	//the main thread pumps the SDL events, that keeps this state up to date
	const Uint8* keys = SDL_GetKeyboardState(NULL);
	uint8_t buttons = 0;
	if (keys[SDL_SCANCODE_LEFT])
		buttons |= BUTTON_LEFT;
	if (keys[SDL_SCANCODE_UP])
		buttons |= BUTTON_UP;
	if (keys[SDL_SCANCODE_DOWN])
		buttons |= BUTTON_DOWN;
	if (keys[SDL_SCANCODE_RIGHT])
		buttons |= BUTTON_RIGHT;
	if (keys[SDL_SCANCODE_X])
		buttons |= BUTTON_A;
	if (keys[SDL_SCANCODE_C])
		buttons |= BUTTON_B;
	if (keys[SDL_SCANCODE_S])
		buttons |= BUTTON_L;
	if (keys[SDL_SCANCODE_D])
		buttons |= BUTTON_R;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return (uint32_t)lgfx::micros();
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	if (!audioDevice)
		return;
	SDL_LockAudioDevice(audioDevice);
	//a frequency of 0 is a rest
	toneHalfWave = freq ? (uint32_t)(AUDIO_RATE / (2 * (uint32_t)freq)) : 0;
	if (freq && !toneHalfWave)
		toneHalfWave = 1;
	tonePlaysOn = (duration == 0);
	toneSamplesLeft = (uint32_t)duration * AUDIO_RATE / 1000;
	tonePosition = 0;
	SDL_UnlockAudioDevice(audioDevice);
}

//a PC has no figures worth showing next to the ESPboy's, the debug line shows 0
uint32_t Platform_FreeHeap(void)
{
	return 0;
}

uint32_t Platform_FreeStack(void)
{
	return 0;
}

uint32_t Platform_RandomSeed(void)
{
	return (uint32_t)time(NULL) ^ (uint32_t)SDL_GetPerformanceCounter();
}

void Platform_Log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	fflush(stdout);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in the file. Bytes the file does not have yet read as
// 0xFF, the way erased flash does on the ESPboy, so the game sees a never saved store.
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

#endif
