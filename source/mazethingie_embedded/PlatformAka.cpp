//Platform.h for the Gamebuino AKA. Everything the device does goes through the AKA library
//(components/gamebuino), nothing here touches a register or the library's framebuffer:
//  display  gb_graphics, 320x240 RGB565. The game's frame is scaled to 240x240 in the middle
//  buttons  gb_core's expander keys, the joystick stands in for the d-pad as well
//  sound    a gb_audio_track_tone square wave on the shared gb_audio_player
//  saves    a file of PLATFORM_STORAGE_SIZE bytes on the SD card, in a folder named after the game
//
//Buttons: d-pad or joystick, A, B, and L1/R1. MENU is left to the device's own menu.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_AKA

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//mkdir and the reason it failed, for the folder the save sits in
#include <sys/stat.h>
#include <errno.h>

#include "PlatformGamebuinoFont.h"
#include "gamebuino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
//for Platform_Exit: the launcher's partition and the restart into it
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
//gb_ll_expander_read: the keys without the library's poll, see Platform_GetButtons
#include "gb_ll_i2c.h"
//gb_ll_expander_power_off: RUN on its own, see Platform_GetButtons
#include "gb_ll_expander.h"

//the AKA's panel
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
//the game's screen sits in the middle, unless SCALESCREEN scales a buffered frame to SCALED_SIZE
#define DISPLAY_OFFSET_X ((DISPLAY_WIDTH - WINDOW_WIDTH) / 2)
#define DISPLAY_OFFSET_Y ((DISPLAY_HEIGHT - WINDOW_HEIGHT) / 2)
//scaled the frame is as high as the display and square, in the middle
#define SCALED_SIZE DISPLAY_HEIGHT
#define SCALED_OFFSET_X ((DISPLAY_WIDTH - SCALED_SIZE) / 2)

//how often the sound mixer runs, the AKA's own examples use the same period
#define AUDIO_TASK_MS 5
#define AUDIO_VOLUME 200
//the square wave the game's tones are played as, at a fixed level
#define TONE_VOLUME 0.6f

// ===========================================================================
// The library's objects
// ===========================================================================

static gb_core core;
static gb_graphics gfx;
static gb_audio_player audioPlayer;
static gb_audio_track_tone toneTrack;


static PlatformAkaDisplay display;
PlatformDisplay& platformDisplay = display;
#if SCREENBUFFER
PlatformBuffer screenBuffer;
#endif

//the name Platform_Init was given, the save file sits in a folder of that name
static char saveFolder[64] = MOUNT_POINT "/game";
static char savePath[96] = MOUNT_POINT "/game/save.bin";

// ===========================================================================
// Colours
//
// The game's colours are RGB565, the AKA's panel does not take that order. makeColor is the
// library's own conversion, so the order stays the library's business and not this port's
// ===========================================================================

static inline uint16_t AkaColor(uint16_t rgb565)
{
	return gfx.makeColor((uint8_t)(((rgb565 >> 11) & 0x1F) << 3),
	                     (uint8_t)(((rgb565 >> 5) & 0x3F) << 2),
	                     (uint8_t)((rgb565 & 0x1F) << 3));
}

// ===========================================================================
// Where a game pixel lands on the display
//
// Game pixel x covers displayColumn[x] up to displayColumn[x + 1], so neighbours meet with no
// seam: 240 / 128 is not a whole number and a game pixel is 1 or 2 display pixels wide
// ===========================================================================

static uint16_t displayColumn[WINDOW_WIDTH + 1], displayRow[WINDOW_HEIGHT + 1];

static void MapDisplay(void)
{
	for (uint16_t x = 0; x <= WINDOW_WIDTH; x++)
#if SCALESCREEN && SCREENBUFFER
		displayColumn[x] = (uint16_t)(SCALED_OFFSET_X + (x * SCALED_SIZE + WINDOW_WIDTH - 1) / WINDOW_WIDTH);
#else
		displayColumn[x] = (uint16_t)(DISPLAY_OFFSET_X + x);
#endif
	for (uint16_t y = 0; y <= WINDOW_HEIGHT; y++)
#if SCALESCREEN && SCREENBUFFER
		displayRow[y] = (uint16_t)((y * SCALED_SIZE + WINDOW_HEIGHT - 1) / WINDOW_HEIGHT);
#else
		displayRow[y] = (uint16_t)(DISPLAY_OFFSET_Y + y);
#endif
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

//the game rectangle x, y, w, h as one gb_graphics rectangle in the display's pixels
static void FillMapped(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!ClipRect(x, y, w, h))
		return;
	const int32_t px = displayColumn[x], py = displayRow[y];
	const int32_t pw = displayColumn[x + w] - px, ph = displayRow[y + h] - py;
	if ((pw <= 0) || (ph <= 0))
		return;
	gfx.setColor(AkaColor(color));
	gfx.fillRect((int16_t)px, (int16_t)py, (int16_t)pw, (int16_t)ph);
}

// ===========================================================================
// Display
// ===========================================================================

void PlatformAkaDisplay::init(void)
{
	MapDisplay();
	gfx.setColor(gfx.makeColor(0, 0, 0));
	gfx.clear();
	gfx.update();
}

void PlatformAkaDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	windowX = x;
	windowY = y;
	windowW = w;
	windowH = h;
	windowPos = 0;
}

//the next pixel of the window opened last, wrapping the way a display's own window does
void PlatformAkaDisplay::putNext(uint16_t color)
{
	if ((windowW <= 0) || (windowH <= 0))
		return;
	const int32_t x = windowX + (windowPos % windowW);
	const int32_t y = windowY + (windowPos / windowW);
	if (++windowPos >= windowW * windowH)
		windowPos = 0;
	FillMapped(x, y, 1, 1, color);
}

void PlatformAkaDisplay::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = data[i];
		putNext(swap ? value : (uint16_t)((value >> 8) | (value << 8)));
	}
}

void PlatformAkaDisplay::writeColor(uint16_t color, uint32_t length)
{
	while (length--)
		putNext(color);
}

void PlatformAkaDisplay::writeBytes(const uint8_t* data, uint32_t length)
{
	for (uint32_t i = 0; i + 1 < length; i += 2)
		putNext((uint16_t)((data[i] << 8) | data[i + 1]));
}

void PlatformAkaDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	FillMapped(x, y, w, h, color);
}

// ===========================================================================
// The screen buffer
// ===========================================================================

#if SCREENBUFFER
bool PlatformAkaBuffer::createSprite(int32_t w, int32_t h)
{
	free(pixels);
	size_t bytes;
	if (depth == 16)
		bytes = (size_t)w * h * 2;
	else if (depth == 8)
		bytes = (size_t)w * h;
	else
		bytes = (size_t)((w + 7) / 8) * h;
	//the buffer is read once a frame and written all over, SRAM is worth it over PSRAM
	pixels = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	if (!pixels)
		pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformAkaBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!pixels || !ClipRect(x, y, w, h))
		return;
	for (int32_t row = y; row < y + h; row++)
		for (int32_t col = x; col < x + w; col++)
			SetBufferPixel(pixels, (int16_t)col, (int16_t)row, color);
}
#endif

// ===========================================================================
// What the display and the buffer share
// ===========================================================================

void PlatformAkaGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

//Draws the character the way LovyanGFX draws its default font: a 6x8 cell times the text size,
//the 5 columns of the glyph and a column of background, the background only when it differs
//from the text colour
size_t PlatformAkaGFX::drawChar(uint16_t c, int32_t x, int32_t y)
{
	if (c > 255)
		c = '?';
	for (uint8_t col = 0; col < 6; col++)
	{
		const uint8_t bits = (col < 5) ? PLATFORM_READ_BYTE(&platformFont[c * 5 + col]) : 0x00;
		for (uint8_t row = 0; row < 8; row++)
		{
			const bool set = (bits >> row) & 1;
			if (!set && (textBackground == textColor))
				continue;
			fillRect(x + col * textSize, y + row * textSize, textSize, textSize,
			         set ? textColor : textBackground);
		}
	}
	return (size_t)(6 * textSize);
}

// ===========================================================================
// The frame
// ===========================================================================

#if SCREENBUFFER == 1
static uint16_t presentSetColor = 0xFFFF, presentClearColor = 0x0000;
#endif

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	presentSetColor = setColor;
	presentClearColor = clearColor;
#else
	(void)setColor;
	(void)clearColor;
#endif
}

#if SCREENBUFFER
//the colour of game pixel x of a buffer row, as RGB565
static inline uint16_t BufferPixel(const uint8_t* row, int32_t x)
{
  #if SCREENBUFFER == 16
	//byte swapped in the buffer, the way a LovyanGFX sprite keeps it
	const uint16_t value = ((const uint16_t*)row)[x];
	return (uint16_t)((value >> 8) | (value << 8));
  #elif SCREENBUFFER == 8
	const uint8_t value = row[x];
	return (uint16_t)(((value & 0xE0) << 8) | ((value & 0x1C) << 6) | ((value & 0x03) << 3));
  #else
	return (row[x >> 3] & (0x80 >> (x & 7))) ? presentSetColor : presentClearColor;
  #endif
}
#endif

void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	const uint8_t* pixels = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (pixels)
	{
		//a run of game pixels of one colour goes out as one rectangle: the frame is mostly
		//flat colour, and a rectangle a pixel costs 16384 calls where this costs far fewer
		const size_t stride = (SCREENBUFFER == 16) ? WINDOW_WIDTH * 2
		                    : (SCREENBUFFER == 8) ? WINDOW_WIDTH : (WINDOW_WIDTH + 7) / 8;
		for (int32_t y = 0; y < WINDOW_HEIGHT; y++)
		{
			const uint8_t* row = pixels + (size_t)y * stride;
			int32_t start = 0;
			uint16_t color = BufferPixel(row, 0);
			for (int32_t x = 1; x <= WINDOW_WIDTH; x++)
			{
				const uint16_t next = (x < WINDOW_WIDTH) ? BufferPixel(row, x) : (uint16_t)(color ^ 0xFFFF);
				if (next != color)
				{
					FillMapped(start, y, x - start, 1, color);
					start = x;
					color = next;
				}
			}
		}
	}
#endif
	gfx.update();
}

// ===========================================================================
// Buttons
// ===========================================================================

//Back to the launcher, the way Jicehel's AKA games return to it (components/aka_runtime in his
//ports, github.com/Jicehel-Aka): the launcher lives in the second app partition, OTA_1 ("loader"
//in platforms/aka/partitions.csv, the App Store's app1), so that becomes the partition to boot
//and the AKA restarts into it. Without one (a build flashed on its own with another table) it
//does nothing and the game carries on
void Platform_Exit(void)
{
	const esp_partition_t* loader = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
		ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
	if (!loader)
		return;
	esp_ota_set_boot_partition(loader);
	esp_restart();
}

//RUN and MENU held together for half a second call Platform_Exit: Jicehel's convention for the
//AKA, and his timing (check_return_to_loader in his aka_runtime). In his games a long MENU on
//its own takes a screenshot and a long RUN switches the AKA off, so neither is used alone here.
//Counted in milliseconds, so it takes as long whatever the frame rate
#define AKA_EXIT_HOLD_MS 500
//when both went down, 0 while they are not
static int64_t exitHeldSince = 0;
//RUN is down, and whether it has been on its own all the while (no MENU), see Platform_GetButtons
static bool runDown = false;
static bool runAlone = false;

uint8_t Platform_GetButtons(void)
{
	//The library's poll (core.pool, gb_buttons::update) switches the AKA off as soon as it sees
	//RUN: it waits in gb_ll_expander_power_off for RUN to be released and then cuts the 3.3 V,
	//so a RUN held with MENU would never reach the game (Jicehel's games build against a copy of
	//the library where RUN no longer does that, set_run_power_off; this one builds against the
	//official one). So the keys are read straight from the expander, the read that poll makes
	//itself, and while RUN is down the poll is left out: the buttons come from that read
	//and the joystick is read on its own. RUN is then handled below, the way the library would
	//have, except when MENU joins it
	const uint16_t raw = gb_ll_expander_read() & EXPANDER_KEY;
	uint16_t keys;
	if (raw & GB_KEY_RUN)
	{
		core.joystick.update();
		keys = raw;
	}
	else
	{
		core.pool();
		keys = core.buttons.state();
	}
	//RUN on its own switches the AKA off when it comes up, as the library does it (through its
	//own power off, which cuts the 3.3 V once RUN is released). RUN that MENU joined at any point
	//does not: that is the way back to the launcher, see Platform_Exit
	if (keys & GB_KEY_RUN)
	{
		if (!runDown)
		{
			runDown = true;
			runAlone = true;
		}
		if (keys & GB_KEY_MENU)
			runAlone = false;
	}
	else if (runDown)
	{
		runDown = false;
		if (runAlone)
			gb_ll_expander_power_off();
	}
	uint8_t buttons = 0;
	if (keys & GB_KEY_LEFT)  buttons |= BUTTON_LEFT;
	if (keys & GB_KEY_RIGHT) buttons |= BUTTON_RIGHT;
	if (keys & GB_KEY_UP)    buttons |= BUTTON_UP;
	if (keys & GB_KEY_DOWN)  buttons |= BUTTON_DOWN;
	if (keys & GB_KEY_A)     buttons |= BUTTON_A;
	if (keys & GB_KEY_B)     buttons |= BUTTON_B;
	if (keys & GB_KEY_L1)    buttons |= BUTTON_L;
	if (keys & GB_KEY_R1)    buttons |= BUTTON_R;
	//the joystick stands in for the d-pad, so either way of steering works
	const int16_t jx = core.joystick.get_x(), jy = core.joystick.get_y();
	if (jx < -500) buttons |= BUTTON_LEFT;
	if (jx >  500) buttons |= BUTTON_RIGHT;
	if (jy < -500) buttons |= BUTTON_UP;
	if (jy >  500) buttons |= BUTTON_DOWN;
	//RUN and MENU together, see Platform_Exit
	if ((keys & GB_KEY_RUN) && (keys & GB_KEY_MENU))
	{
		const int64_t now = esp_timer_get_time() / 1000;
		if (!exitHeldSince)
			exitHeldSince = now | 1;
		else if ((now - exitHeldSince) >= AKA_EXIT_HOLD_MS)
			Platform_Exit();
	}
	else
		exitHeldSince = 0;
	return buttons;
}

// ===========================================================================
// Sound
// ===========================================================================

//the mixer has to keep running while the game thinks, so it gets a task of its own
static void AudioTask(void*)
{
	for (;;)
	{
		audioPlayer.pool();
		vTaskDelay(pdMS_TO_TICKS(AUDIO_TASK_MS));
	}
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	//a frequency of 0 is a rest, as on every other platform: it silences the tone. Every
	//tune in Sound.cpp ends with one, and dropping it leaves the last note sounding for the
	//0xFFFF milliseconds below, until the next tone happens to replace it
	if (!freq)
	{
		toneTrack.stop_playing();
		return;
	}
	//a duration of 0 means "until it is stopped", which the track has no notion of: the
	//longest note it takes stands in, the next tone or Platform_StopTone ends it
	toneTrack.play_tone((float)freq, TONE_VOLUME, duration ? duration : 0xFFFF,
	                    gb_audio_track_tone::SQUARE);
}

void Platform_StopTone(void)
{
	toneTrack.stop_playing();
}

// ===========================================================================
// Time and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	return (uint32_t)core.get_micros();
}

uint32_t Platform_FreeHeap(void)
{
	return (uint32_t)core.free_sram();
}

uint32_t Platform_FreeStack(void)
{
	return (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
}

uint32_t Platform_RandomSeed(void)
{
	return esp_random();
}

void Platform_Log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

// ===========================================================================
// Saving
// ===========================================================================

//Makes the folder the save sits in, if it is not there already. A file cannot be opened for
//writing in a folder that does not exist, and nothing was creating it: the first save did nothing
//at all, fopen returning NULL and the write being dropped on the floor without a word.
//The card is FAT, so long file names have to be on for a folder of more than eight characters to
//be made at all, which Blockdude and Puzzleland both are. See CONFIG_FATFS_LFN_HEAP in
//platforms/aka/sdkconfig
static bool EnsureSaveFolder(void)
{
	if ((mkdir(saveFolder, 0777) == 0) || (errno == EEXIST))
		return true;
	Platform_Log("cannot make %s: %s\n", saveFolder, strerror(errno));
	return false;
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memset(data, 0, length);
	FILE* f = fopen(savePath, "rb");
	if (!f)
		return;
	if (fseek(f, offset, SEEK_SET) == 0)
		fread(data, 1, length, f);
	fclose(f);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	//the whole block is kept, so a write of part of it reads the rest back first
	uint8_t block[PLATFORM_STORAGE_SIZE];
	memset(block, 0, sizeof(block));
	FILE* f = fopen(savePath, "rb");
	if (f)
	{
		fread(block, 1, sizeof(block), f);
		fclose(f);
	}
	if (offset + length > sizeof(block))
		return;
	memcpy(block + offset, data, length);
	//the folder has to be there before a file can be opened in it
	if (!EnsureSaveFolder())
		return;
	f = fopen(savePath, "wb");
	if (!f)
		return;
	fwrite(block, 1, sizeof(block), f);
	fclose(f);
}

// ===========================================================================
// Starting up
// ===========================================================================

void Platform_Init(const char* appName)
{
	core.init();
	gfx.set_backlight_percent(BACKLIGHT);
	gfx.set_refresh_rate(60);

	display.init();

#if SCREENBUFFER
	screenBuffer.setColorDepth(SCREENBUFFER);
	screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT);
#endif

	audioPlayer.set_master_volume(AUDIO_VOLUME);
	audioPlayer.add_track(&toneTrack, 1.0f);
	xTaskCreatePinnedToCore(AudioTask, "akaAudio", 4096, nullptr, 5, nullptr, 1);

	if (appName && *appName)
	{
		//The folder the game's files sit in on the card, named the way the Gamebuino META build
		//names its save folder (PlatformGamebuino.cpp): the first word of the game's name and
		//_embedded, so "Znax v1.0" saves in /Znax_embedded, the znax_embedded folder the launcher
		//has it in (the card does not mind the case). The repository's name, because other games
		//called Sokoban and Waternet have folders of their own (Jicehel's Sokoban lives in
		//SOKOBAN); the first word only, because appName carries the version and a save written
		//by one version is meant to be read by the next
		size_t n = 0;
		while (appName[n] && (appName[n] != ' ') && (n < 16))
			n++;
		if (n)
		{
			snprintf(saveFolder, sizeof(saveFolder), MOUNT_POINT "/%.*s_embedded", (int)n, appName);
			snprintf(savePath, sizeof(savePath), "%s/save.bin", saveFolder);
		}
	}
}

extern "C" void app_main(void)
{
	Game_Setup();
	for (;;)
	{
		Game_Loop();
		//the game's own pace, the frame went out in Platform_PresentFrame
		vTaskDelay(1);
	}
}

#endif
