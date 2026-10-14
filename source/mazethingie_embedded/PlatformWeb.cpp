//Platform.h for the browser, built to WebAssembly with Emscripten (see web/CMakeLists.txt):
//  display  a canvas the game's 128x128 frame is drawn into through SDL2, scaled by the page
//  buttons  the arrow keys, X and C, and S and D for the two side buttons
//  sound    a square wave made in SDL's audio callback
//  saves    the browser's localStorage, one entry named after the game
//  time     performance.now(), which is what the browser counts in
//
//main() here starts the game and hands the loop to the browser: a page that never returns to it
//freezes the tab, so Game_Loop is called once per animation frame instead of from a loop of its
//own. The game still holds its frames to its own frame rate.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_WEB

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <emscripten.h>
#include <SDL2/SDL.h>
#include "PlatformGamebuinoFont.h"

static PlatformWebDisplay display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and sent to the canvas when the frame is done
PlatformBuffer screenBuffer;
#endif

#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as a 16 bit pixel
static uint16_t bufferPalette[256];
#endif

//the frame as the canvas takes it: plain RGB565, which is what the texture holds
static uint16_t frameTexture[WINDOW_WIDTH * WINDOW_HEIGHT];

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture* texture = NULL;

//the name the save is kept under, taken from the name Platform_Init is given
static char storageKey[64] = "game";

// ===========================================================================
// Program start
// ===========================================================================

//Tells the page the game is up. It can not work this out for itself: emscripten_set_main_loop is
//given the loop and never returns, so the runtime unwinds out of main and whatever the page hung on
//onRuntimeInitialized is never called
EM_JS(void, PlatformWeb_Ready, (), {
	window.gameReady = true;
	var status = document.getElementById("status");
	if (status)
		status.textContent = "click or tap to play";
});

//The microseconds the game is given, moved on once per animation frame.
//
//The game asks for a frame every 1/FPS of a second and holds back until that much has gone by. The
//browser hands out frames at its own rate, and for a 30 frame game on a 60 Hz screen the two land a
//microsecond apart: two frames of the browser are 33334 microseconds against the 33333 the game
//waits for. A browser also reports time in steps of a tenth of a millisecond, so the figure usually
//comes out a hair under, the game holds back another whole frame of the browser, and what should be
//30 runs at 20. The clock is given a hundredth more than went by, which is more than the tenth of a
//millisecond the browser rounds to and far less than anybody can hear or see: the game gets the
//microsecond it is short of and runs at the rate it asks for, whatever the screen refreshes at
//microseconds, and the clock moves on by the same amount every frame rather than by whatever was
//measured: a browser hands out frames a little unevenly, and passing that on would now and then
//push one frame over the line and drop the game to the next beat down for it
#define WEB_CLOCK_SLACK 1.01
static uint32_t webMicros = 0;
static double webLastNow = -1.0;
static double webPeriod = 1000000.0 / 60.0;

static void ClockTick(void)
{
	const double now = emscripten_get_now();
	if (webLastNow < 0.0)
		webLastNow = now;
	const double elapsed = (now - webLastNow) * 1000.0;
	webLastNow = now;
	//A tab that was in the background comes back with a gap of seconds, and a frame that was handed
	//out early says as little about the rate, so only the believable ones move the average. 2000 is
	//500 frames a second and 50000 is 20 of them, no screen is outside that
	if ((elapsed > 2000.0) && (elapsed < 50000.0))
		webPeriod += (elapsed - webPeriod) / 16.0;
	webMicros += (uint32_t)(webPeriod * WEB_CLOCK_SLACK);
}

static void MainLoop(void)
{
	//the events are pumped here, that is what keeps the keyboard state up to date
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		//nothing is done with them, Platform_GetButtons reads the state SDL keeps
	}
	ClockTick();
	Game_Loop();
}

int main(int, char**)
{
	Game_Setup();
	//0: once per animation frame, which is what the browser draws at. 1: this call never returns,
	//so nothing after it runs
	emscripten_set_main_loop(MainLoop, 0, 1);
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
void PlatformWebDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformWebDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformWebDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformWebDisplay::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the frame the canvas is handed. The area the
//pixels go into and where the next one goes, in game coordinates
static int32_t windowLeft = 0, windowRight = -1, cursorX = 0, cursorY = 0;

void PlatformWebDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
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
		frameTexture[cursorY * WINDOW_WIDTH + cursorX] = color;
	if (++cursorX > windowRight)
	{
		cursorX = windowLeft;
		cursorY++;
	}
}

void PlatformWebDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
		WritePixel(swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8)));
}

void PlatformWebDisplay::writeColor(uint16_t color, uint32_t length)
{
	while (length--)
		WritePixel(color);
}

void PlatformWebDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &frameTexture[row * WINDOW_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = color;
	}
}
#endif

void PlatformWebGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformWebGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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
bool PlatformWebBuffer::createSprite(int32_t w, int32_t h)
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

void PlatformWebBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
#endif

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

#if SCREENBUFFER
//bytes of one row of the screen buffer
#if SCREENBUFFER == 16
#define BUFFER_ROW_BYTES (WINDOW_WIDTH * 2)
#elif SCREENBUFFER == 8
#define BUFFER_ROW_BYTES WINDOW_WIDTH
#else
#define BUFFER_ROW_BYTES ((WINDOW_WIDTH + 7) / 8)
#endif

//the 16 bit pixel of game pixel x of a buffer row
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
#endif

//Sends the frame the game drew to the canvas
void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	//the buffer holds the frame in the colours a sprite keeps, the texture wants plain RGB565
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	for (uint16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * BUFFER_ROW_BYTES];
		uint16_t* d = &frameTexture[y * WINDOW_WIDTH];
		for (uint16_t x = 0; x < WINDOW_WIDTH; x++)
			d[x] = BufferPixel(row, x);
	}
#endif
	if (!renderer || !texture)
		return;
	SDL_UpdateTexture(texture, NULL, frameTexture, WINDOW_WIDTH * (int)sizeof(uint16_t));
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

static void DisplayInit(const char* appName)
{
	//The canvas is the game's own 128x128 and nothing else: the page stretches it to whatever room
	//it has, which the browser does while it puts the page together and costs the game nothing.
	//Drawing into a bigger canvas here would be the same picture at the price of every pixel of it
	window = SDL_CreateWindow(appName, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	                          WINDOW_WIDTH, WINDOW_HEIGHT, 0);
	//a pixel of the game stays a square of canvas pixels instead of being smeared
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	//the graphics card through WebGL, which is what a browser is quick at. The software renderer is
	//only there for the browser that has no WebGL to give
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (!renderer)
	{
		Platform_Log("no accelerated renderer (%s), falling back\n", SDL_GetError());
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (!renderer)
	{
		Platform_Log("no renderer: %s\n", SDL_GetError());
		return;
	}
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(renderer, &info) == 0)
		Platform_Log("renderer: %s\n", info.name);
	//the canvas is that size already, this keeps it so if it is ever given another one
	SDL_RenderSetLogicalSize(renderer, WINDOW_WIDTH, WINDOW_HEIGHT);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
	                            WINDOW_WIDTH, WINDOW_HEIGHT);
	if (!texture)
		Platform_Log("no texture: %s\n", SDL_GetError());
}

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	//MainLoop pumps the events, that keeps this state up to date
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
	//moved on once an animation frame by ClockTick, so every read inside one frame gives the same
	//moment. It wraps around after about 71 minutes, which is what the game is told to expect
	return webMicros;
}

//A square wave made in SDL's audio callback, which the browser calls for from its own audio thread.
//The game only sets the wave, so a tone that starts halfway through a buffer costs nothing
#define AUDIO_RATE 22050
static SDL_AudioDeviceID audioDevice = 0;
//samples of a half wave (0 is silence), samples still to play, and whether it plays until the next
//tone
static uint32_t toneHalfWave = 0, toneSamplesLeft = 0, tonePosition = 0;
static bool tonePlaysOn = false;

static void SDLCALL AudioCallback(void*, Uint8* stream, int len)
{
	int16_t* out = (int16_t*)stream;
	const int samples = len / (int)sizeof(int16_t);
	const int16_t level = (int16_t)(0x7FFF * SOUNDVOLUME / 100);
	for (int i = 0; i < samples; i++)
	{
		if (toneHalfWave && (tonePlaysOn || toneSamplesLeft))
		{
			out[i] = ((tonePosition / toneHalfWave) & 1) ? level : (int16_t)-level;
			tonePosition++;
			if (!tonePlaysOn)
				toneSamplesLeft--;
		}
		else
			out[i] = 0;
	}
}

static void SoundInit(void)
{
	SDL_AudioSpec wanted;
	SDL_zero(wanted);
	wanted.freq = AUDIO_RATE;
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 1;
	//a short buffer, so a tone is heard about as soon as the game asks for it
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

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	if (!audioDevice)
		return;
	SDL_LockAudioDevice(audioDevice);
	//a frequency of 0 is a rest
	toneHalfWave = freq ? (uint32_t)(AUDIO_RATE / (2 * (uint32_t)freq)) : 0;
	if (freq && !toneHalfWave)
		toneHalfWave = 1;
	//0 keeps playing until the next tone
	tonePlaysOn = (duration == 0);
	toneSamplesLeft = (uint32_t)duration * AUDIO_RATE / 1000;
	tonePosition = 0;
	SDL_UnlockAudioDevice(audioDevice);
}

//not every game asks for this, the ones that do use it to cut a tone short
void Platform_StopTone(void)
{
	if (!audioDevice)
		return;
	SDL_LockAudioDevice(audioDevice);
	toneHalfWave = 0;
	toneSamplesLeft = 0;
	tonePlaysOn = false;
	tonePosition = 0;
	SDL_UnlockAudioDevice(audioDevice);
}

//a browser has no figures worth showing next to a handheld's, the debug line shows 0
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
	//the moment the page was opened, which is never quite the same twice
	return (uint32_t)(emscripten_get_now() * 1000.0) ^ (uint32_t)(emscripten_random() * 4294967296.0);
}

void Platform_Log(const char* format, ...)
{
	//goes to the browser's console, the canvas is the game's
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

// ===========================================================================
// Saved data
//
// One localStorage entry named after the game, holding the whole block as hex. A browser that
// refuses it (a private window, storage turned off) still plays the game, it just forgets it.
// ===========================================================================

//the block as hex, so it survives being kept as text
EM_JS(void, PlatformWeb_StorageSave, (const char* key, const uint8_t* data, int length), {
	var name = "";
	for (var i = key; HEAPU8[i]; i++)
		name += String.fromCharCode(HEAPU8[i]);
	var hex = "";
	for (var i = 0; i < length; i++)
		hex += ("0" + HEAPU8[data + i].toString(16)).slice(-2);
	try { localStorage.setItem(name, hex); } catch (e) {}
});

//fills data with what was stored, and leaves it alone when there is nothing
EM_JS(int, PlatformWeb_StorageLoad, (const char* key, uint8_t* data, int length), {
	var name = "";
	for (var i = key; HEAPU8[i]; i++)
		name += String.fromCharCode(HEAPU8[i]);
	var hex = null;
	try { hex = localStorage.getItem(name); } catch (e) { return 0; }
	if (hex === null || hex.length < length * 2)
		return 0;
	for (var i = 0; i < length; i++)
		HEAPU8[data + i] = parseInt(hex.substr(i * 2, 2), 16);
	return 1;
});

//the block is kept here as well, so a read costs nothing and a write only goes out when something
//actually changed
static uint8_t storage[PLATFORM_STORAGE_SIZE];

static void StorageInit(const char* appName)
{
	//the save takes the first word of the name, "Blips v1.0" is stored as Blips
	size_t n = 0;
	while (appName[n] && (appName[n] != ' ') && (n < sizeof(storageKey) - 1))
	{
		storageKey[n] = appName[n];
		n++;
	}
	if (n)
		storageKey[n] = '\0';
	//nothing stored reads as erased flash does on a handheld, so the game sees a fresh save
	memset(storage, 0xFF, sizeof(storage));
	PlatformWeb_StorageLoad(storageKey, storage, (int)sizeof(storage));
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
	PlatformWeb_StorageSave(storageKey, storage, (int)sizeof(storage));
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
		Platform_Log("SDL did not start: %s\n", SDL_GetError());
	StorageInit(appName);
	DisplayInit(appName);
	SoundInit();
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
		bufferPalette[i] = (uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3));
	}
#endif
	PlatformWeb_Ready();
}

#endif
