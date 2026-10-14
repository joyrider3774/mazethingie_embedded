//Platform.h for libretro, the game as a core for RetroArch and the other libretro frontends, built
//with libretro/CMakeLists.txt:
//  display  the game's 128x128 screen buffer, handed to the frontend as RGB565 every frame
//  buttons  the frontend's first joypad: d-pad, A, B, L and R
//  sound    a square wave, 44100 Hz stereo samples every frame
//  saves    the frontend's save RAM (the .srm file), PLATFORM_STORAGE_SIZE bytes
//  time     counts the frames the frontend ran, so pausing or fast forwarding the frontend does the
//           same to the game
//
//The frontend calls the retro_ functions at the bottom of this file. The game starts at the first
//retro_run: the frontend loads the save RAM after retro_load_game, and the game reads its saves
//while it starts.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_LIBRETRO

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libretro.h>
#include "PlatformGamebuinoFont.h"

//set by libretro/CMakeLists.txt
#ifndef LIBRETRO_GAME_NAME
#define LIBRETRO_GAME_NAME "Game"
#endif
#ifndef LIBRETRO_GAME_VERSION
#define LIBRETRO_GAME_VERSION "1.0"
#endif

#define AUDIO_SAMPLE_RATE 44100

static retro_environment_t environmentCallback = nullptr;
static retro_video_refresh_t videoCallback = nullptr;
static retro_audio_sample_batch_t audioBatchCallback = nullptr;
static retro_input_poll_t inputPollCallback = nullptr;
static retro_input_state_t inputStateCallback = nullptr;
static retro_log_printf_t logCallback = nullptr;

static PlatformLibretroDisplay display;
PlatformDisplay& platformDisplay = display;

//the whole frame is drawn in here and handed to the frontend at the end of every retro_run
PlatformBuffer screenBuffer;
#if SCREENBUFFER == 1
//the two colours a 1 bpp frame is shown in
static uint16_t bufferSetColor = 0xFFFF, bufferClearColor = 0x0000;
#endif
#if SCREENBUFFER == 8
//every RGB332 value as RGB565
static uint16_t bufferPalette[256];
#endif

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

//everything is drawn into the screen buffer
void PlatformLibretroDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformLibretroGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformLibretroGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

bool PlatformLibretroBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformLibretroBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

//the frame in the pixels the frontend takes
static uint16_t frame[WINDOW_WIDTH * WINDOW_HEIGHT];

//the game drew its frame, it goes out at the end of retro_run
void Platform_PresentFrame(void)
{
}

//the screen buffer as RGB565 pixels
static void ConvertFrame(void)
{
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src)
		return;
	for (uint32_t i = 0; i < (uint32_t)WINDOW_WIDTH * WINDOW_HEIGHT; i++)
	{
#if SCREENBUFFER == 16
		//byte swapped in the buffer
		frame[i] = (uint16_t)((src[i * 2] << 8) | src[i * 2 + 1]);
#elif SCREENBUFFER == 8
		frame[i] = bufferPalette[src[i]];
#else
		//most significant bit first as SetBufferBit writes
		frame[i] = (src[i >> 3] & (0x80 >> (i & 7))) ? bufferSetColor : bufferClearColor;
#endif
	}
}

// ===========================================================================
// Buttons
// ===========================================================================

uint8_t Platform_GetButtons(void)
{
	if (!inputStateCallback)
		return 0;
	static const struct { unsigned id; uint8_t button; } map[] = {
		{ RETRO_DEVICE_ID_JOYPAD_LEFT, BUTTON_LEFT },
		{ RETRO_DEVICE_ID_JOYPAD_UP, BUTTON_UP },
		{ RETRO_DEVICE_ID_JOYPAD_DOWN, BUTTON_DOWN },
		{ RETRO_DEVICE_ID_JOYPAD_RIGHT, BUTTON_RIGHT },
		{ RETRO_DEVICE_ID_JOYPAD_A, BUTTON_A },
		{ RETRO_DEVICE_ID_JOYPAD_B, BUTTON_B },
		{ RETRO_DEVICE_ID_JOYPAD_L, BUTTON_L },
		{ RETRO_DEVICE_ID_JOYPAD_R, BUTTON_R },
	};
	uint8_t buttons = 0;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (inputStateCallback(0, RETRO_DEVICE_JOYPAD, 0, map[i].id))
			buttons |= map[i].button;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

//microseconds of game time, a frame's worth more every retro_run
static uint64_t gameMicros = 0;

uint32_t Platform_Micros(void)
{
	return (uint32_t)gameMicros;
}

//the tone playing: its frequency (0 = silence), how many samples are left of it (0 = until the next
//tone or Platform_StopTone), and where in its period the wave is, in 1/65536 of a period
static uint32_t toneFreq = 0;
static uint32_t toneSamplesLeft = 0;
static uint32_t tonePhase = 0;

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	//a frequency of 0 is a rest
	toneFreq = freq;
	toneSamplesLeft = duration ? (uint32_t)((uint64_t)duration * AUDIO_SAMPLE_RATE / 1000) : 0;
	if (duration && !toneSamplesLeft)
		toneSamplesLeft = 1;
	tonePhase = 0;
}

//the samples of one frame of sound
static void RenderAudio(void)
{
	static int16_t samples[AUDIO_SAMPLE_RATE / 5 * 2];
	//the samples of a frame, and what is left over carried to the next one so no sample goes missing
	static uint32_t remainder = 0;
	const uint32_t total = AUDIO_SAMPLE_RATE + remainder;
	uint32_t count = total / FRAMERATE;
	remainder = total % FRAMERATE;
	if (count > sizeof(samples) / 2)
		count = sizeof(samples) / 2;
	const int16_t level = (int16_t)(16000 * SOUNDVOLUME / 100);
	for (uint32_t i = 0; i < count; i++)
	{
		int16_t value = 0;
		if (toneFreq)
		{
			value = (tonePhase < 32768) ? level : (int16_t)-level;
			tonePhase = (tonePhase + (uint32_t)((uint64_t)toneFreq * 65536 / AUDIO_SAMPLE_RATE)) & 0xFFFF;
			if (toneSamplesLeft && (--toneSamplesLeft == 0))
				toneFreq = 0;
		}
		samples[i * 2] = value;
		samples[i * 2 + 1] = value;
	}
	if (audioBatchCallback)
		audioBatchCallback(samples, count);
}

//the host's memory is not the game's to worry about
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
	return (uint32_t)time(nullptr) ^ (uint32_t)clock();
}

void Platform_Log(const char* format, ...)
{
	char text[256];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	if (logCallback)
		logCallback(RETRO_LOG_INFO, "%s", text);
}

// ===========================================================================
// Saved data
//
// The frontend's save RAM: it loads the .srm file into it after the core is started and writes it
// back when the game is closed or the frontend saves. Bytes never saved read as 0xFF, the way erased
// flash does on the ESPboy, so the game sees a never saved store.
// ===========================================================================

static uint8_t storage[PLATFORM_STORAGE_SIZE];

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	memcpy(storage + offset, data, length);
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	(void)appName;
	screenBuffer.setColorDepth(SCREENBUFFER);
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
#if SCREENBUFFER == 8
	//RGB332 to RGB565 the way LovyanGFX converts it, so the colours match the other devices
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
		bufferPalette[i] = (uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3));
	}
#endif
}

// ===========================================================================
// The libretro core
// ===========================================================================

static bool gameStarted = false;

RETRO_API void retro_set_environment(retro_environment_t callback)
{
	environmentCallback = callback;
	//the game is built in, the frontend starts it without a content file
	bool noGame = true;
	callback(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &noGame);
	struct retro_log_callback logging;
	if (callback(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		logCallback = logging.log;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t callback)
{
	videoCallback = callback;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t callback)
{
	(void)callback;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback)
{
	audioBatchCallback = callback;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t callback)
{
	inputPollCallback = callback;
}

RETRO_API void retro_set_input_state(retro_input_state_t callback)
{
	inputStateCallback = callback;
}

RETRO_API void retro_init(void)
{
	memset(storage, 0xFF, sizeof(storage));
}

RETRO_API void retro_deinit(void)
{
}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info* info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = LIBRETRO_GAME_NAME;
	info->library_version = LIBRETRO_GAME_VERSION;
	info->valid_extensions = "";
	info->need_fullpath = false;
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = WINDOW_WIDTH;
	info->geometry.base_height = WINDOW_HEIGHT;
	info->geometry.max_width = WINDOW_WIDTH;
	info->geometry.max_height = WINDOW_HEIGHT;
	info->geometry.aspect_ratio = (float)WINDOW_WIDTH / WINDOW_HEIGHT;
	info->timing.fps = FRAMERATE;
	info->timing.sample_rate = AUDIO_SAMPLE_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
	(void)port;
	(void)device;
}

//the game keeps no state that can be reset without starting the whole program again
RETRO_API void retro_reset(void)
{
}

RETRO_API void retro_run(void)
{
	if (inputPollCallback)
		inputPollCallback();
	if (!gameStarted)
	{
		//here and not in retro_load_game, the save RAM is loaded by now
		gameStarted = true;
		Game_Setup();
	}
	gameMicros += 1000000u / FRAMERATE;
	Game_Loop();
	RenderAudio();
	ConvertFrame();
	if (videoCallback)
		videoCallback(frame, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_WIDTH * sizeof(uint16_t));
}

//no save states
RETRO_API size_t retro_serialize_size(void)
{
	return 0;
}

RETRO_API bool retro_serialize(void* data, size_t size)
{
	(void)data;
	(void)size;
	return false;
}

RETRO_API bool retro_unserialize(const void* data, size_t size)
{
	(void)data;
	(void)size;
	return false;
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
	(void)index;
	(void)enabled;
	(void)code;
}

RETRO_API bool retro_load_game(const struct retro_game_info* game)
{
	(void)game;
	enum retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
	if (!environmentCallback || !environmentCallback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format))
	{
		Platform_Log("the frontend does not take RGB565 pixels\n");
		return false;
	}
	static const struct retro_input_descriptor descriptors[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
		{ 0, 0, 0, 0, nullptr },
	};
	environmentCallback(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)descriptors);
	return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t count)
{
	(void)type;
	(void)info;
	(void)count;
	return false;
}

RETRO_API void retro_unload_game(void)
{
}

RETRO_API unsigned retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

RETRO_API void* retro_get_memory_data(unsigned id)
{
	return (id == RETRO_MEMORY_SAVE_RAM) ? storage : nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	return (id == RETRO_MEMORY_SAVE_RAM) ? sizeof(storage) : 0;
}

#endif
