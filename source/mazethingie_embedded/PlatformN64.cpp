//Platform.h for the Nintendo 64, built with libdragon (see n64/CMakeLists.txt):
//  display  320x240, the game's 128x128 frame is kept as a 16 bit surface and the RDP draws it
//           scaled to 240x240 in the middle
//  buttons  d-pad, A, B, L and R, read in between the game's frames so none are missed
//  sound    a square wave written into the buffers the sound hardware plays from
//  saves    the cartridge's 4 kbit EEPROM, when the cartridge has one
//  time     the processor's own counter, which runs at half of its 93.75 MHz
//
//main() here starts the game and runs Game_Loop over and over, the game holds its frames to its own
//frame rate itself.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_N64

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <libdragon.h>
#include "PlatformGamebuinoFont.h"

#define SCREEN_N64_WIDTH 320
#define SCREEN_N64_HEIGHT 240

static PlatformN64Display display;
PlatformDisplay& platformDisplay = display;

#if SCREENBUFFER
//the whole frame is drawn in here and handed to the RDP when the frame is done
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

//the frame as the RDP wants it: 16 bit pixels, five bits a channel and the last bit set
static surface_t frameSurface;
static uint16_t* framePixels = NULL;
//where the frame sits on screen and how much it is blown up
static float frameX = 0.0f, frameY = 0.0f, frameScale = 1.0f;

//RGB565 to the 5551 pixel the RDP draws, the last bit is the one that says the pixel is there
static inline uint16_t ToRGBA5551(uint16_t color)
{
	return (uint16_t)((color & 0xF800) | (((color >> 6) & 0x1F) << 6) | ((color & 0x1F) << 1) | 1);
}

static void ToneUpdate(void);
static void ButtonsUpdate(void);

// ===========================================================================
// Program start
// ===========================================================================

int main(void)
{
	Game_Setup();
	while (1)
	{
		//The sound hardware is kept fed and the controller read here: Game_Loop returns without
		//doing anything until it is time for the next frame, so this runs many times between two of
		//them, and a press shorter than a frame is not lost
		ToneUpdate();
		ButtonsUpdate();
		Game_Loop();
	}
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
void PlatformN64Display::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformN64Display::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

void PlatformN64Display::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	(void)data;
	(void)length;
	(void)swap;
}

void PlatformN64Display::writeColor(uint16_t color, uint32_t length)
{
	(void)color;
	(void)length;
}
#else
//Without a screen buffer the game draws straight into the frame the RDP is handed, which is kept in
//the RDP's own colours: a pixel is converted once, where it is drawn, instead of the whole frame
//being converted again every time it is shown. The area the pixels go into and where the next one
//goes, in game coordinates
static int32_t windowLeft = 0, windowRight = -1, cursorX = 0, cursorY = 0;

void PlatformN64Display::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if ((w <= 0) || (h <= 0))
		return;
	windowLeft = x;
	windowRight = x + w - 1;
	cursorX = x;
	cursorY = y;
}

//the next pixel of the window, what falls outside the game's screen is left out
static inline void WritePixel(uint16_t pixel)
{
	if ((cursorX >= 0) && (cursorX < WINDOW_WIDTH) && (cursorY >= 0) && (cursorY < WINDOW_HEIGHT))
		framePixels[cursorY * WINDOW_WIDTH + cursorX] = pixel;
	if (++cursorX > windowRight)
	{
		cursorX = windowLeft;
		cursorY++;
	}
}

void PlatformN64Display::writePixels(const uint16_t* data, int32_t length, bool swap)
{
	for (int32_t i = 0; i < length; i++)
	{
		const uint16_t value = swap ? data[i] : (uint16_t)((data[i] >> 8) | (data[i] << 8));
		WritePixel(ToRGBA5551(value));
	}
}

void PlatformN64Display::writeColor(uint16_t color, uint32_t length)
{
	const uint16_t pixel = ToRGBA5551(color);
	while (length--)
		WritePixel(pixel);
}

void PlatformN64Display::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if (!framePixels || !ClipRect(x, y, w, h))
		return;
	const uint16_t pixel = ToRGBA5551(color);
	for (int32_t row = y; row < y + h; row++)
	{
		uint16_t* d = &framePixels[row * WINDOW_WIDTH + x];
		for (int32_t column = 0; column < w; column++)
			d[column] = pixel;
	}
}
#endif

void PlatformN64GFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformN64GFX::drawChar(uint16_t c, int32_t x, int32_t y)
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
bool PlatformN64Buffer::createSprite(int32_t w, int32_t h)
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

void PlatformN64Buffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
	return ToRGBA5551((uint16_t)((row[x * 2] << 8) | row[x * 2 + 1]));
#elif SCREENBUFFER == 8
	return bufferPalette[row[x]];
#else
	//most significant bit first as SetBufferBit writes
	return (row[x >> 3] & (0x80 >> (x & 7))) ? bufferSetColor : bufferClearColor;
#endif
}
#endif

//Shows the frame the game drew: the RDP draws it onto the screen the video hardware shows next,
//scaled to where the frame belongs
void Platform_PresentFrame(void)
{
#if SCREENBUFFER
	//the buffer holds the frame in the colours a sprite keeps, which the RDP does not take
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	if (!src || !framePixels)
		return;
	for (uint16_t y = 0; y < WINDOW_HEIGHT; y++)
	{
		const uint8_t* row = &src[y * BUFFER_ROW_BYTES];
		uint16_t* d = &framePixels[y * WINDOW_WIDTH];
		for (uint16_t x = 0; x < WINDOW_WIDTH; x++)
			d[x] = BufferPixel(row, x);
	}
#else
	if (!framePixels)
		return;
#endif

	//the frame was written by the processor, so as far as the RDP is concerned it is still in its
	//cache and not in memory yet
	data_cache_hit_writeback(framePixels, (size_t)WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(uint16_t));

	//the screen that is shown next, waited for only if the one before it is still being shown
	surface_t* screen = display_get();
	//black around the game's frame
	rdpq_attach_clear(screen, NULL);
	//a pixel of the frame as it is, filling as many screen pixels as the frame is blown up to
	rdpq_set_mode_standard();
	rdpq_mode_filter(FILTER_POINT);
	rdpq_blitparms_t parms;
	memset(&parms, 0, sizeof(parms));
	parms.scale_x = frameScale;
	parms.scale_y = frameScale;
	rdpq_tex_blit(&frameSurface, frameX, frameY, &parms);
	rdpq_detach_show();
}

static void DisplayInit(void)
{
	//two screens, drawn in one while the other is shown
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
	rdpq_init();

	//the frame the game draws in, which the RDP reads as a texture
	frameSurface = surface_alloc(FMT_RGBA16, WINDOW_WIDTH, WINDOW_HEIGHT);
	framePixels = (uint16_t*)frameSurface.buffer;
	if (framePixels)
		memset(framePixels, 0, (size_t)WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(uint16_t));

#if SCALESCREEN
	//as high as the screen
	frameScale = (float)SCREEN_N64_HEIGHT / (float)WINDOW_HEIGHT;
#else
	frameScale = 1.0f;
#endif
	frameX = (float)((SCREEN_N64_WIDTH - (int)(WINDOW_WIDTH * frameScale)) / 2);
	frameY = (float)((SCREEN_N64_HEIGHT - (int)(WINDOW_HEIGHT * frameScale)) / 2);
}

// ===========================================================================
// Buttons
//
// The game looks at the buttons once a frame, and a frame is a 15th of a second in some of these
// games: a press that starts and ends in between two of them would never be seen. The controller is
// read here every time round main's loop instead, and every change is kept in order so that the
// game is handed one of them a frame and misses none. They are kept in order rather than folded
// together because up and down quickly one after the other has to stay two presses, which one state
// holding both would lose.
// ===========================================================================

//eight changes is a fifth of a second of pressing as fast as anybody can, more than the game can
//take in anyway
#define BUTTONS_QUEUE 8
static uint8_t buttonQueue[BUTTONS_QUEUE];
static uint8_t buttonFirst = 0, buttonCount = 0;
//what is held right now, which is also the last change that went into the queue
static uint8_t buttonsNow = 0;

static void ButtonsInit(void)
{
	joypad_init();
	buttonFirst = 0;
	buttonCount = 0;
	buttonsNow = 0;
}

//the BUTTON_ bits of the controller as it is at this moment
static uint8_t ButtonsRead(void)
{
	const joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
	uint8_t buttons = 0;
	if (held.d_left)
		buttons |= BUTTON_LEFT;
	if (held.d_up)
		buttons |= BUTTON_UP;
	if (held.d_down)
		buttons |= BUTTON_DOWN;
	if (held.d_right)
		buttons |= BUTTON_RIGHT;
	if (held.a)
		buttons |= BUTTON_A;
	if (held.b)
		buttons |= BUTTON_B;
	if (held.l)
		buttons |= BUTTON_L;
	if (held.r)
		buttons |= BUTTON_R;
	return buttons;
}

//reads the controller and puts what changed at the back of the queue
static void ButtonsUpdate(void)
{
	//libdragon reads the controllers by itself on every blank, this takes the last of those reads
	joypad_poll();
	const uint8_t buttons = ButtonsRead();
	if (buttons == buttonsNow)
		return;
	buttonsNow = buttons;
	if (buttonCount == BUTTONS_QUEUE)
	{
		//pressed faster than the game takes them, the oldest change goes
		buttonFirst = (uint8_t)((buttonFirst + 1) % BUTTONS_QUEUE);
		buttonCount--;
	}
	buttonQueue[(buttonFirst + buttonCount) % BUTTONS_QUEUE] = buttons;
	buttonCount++;
}

uint8_t Platform_GetButtons(void)
{
	ButtonsUpdate();
	//the changes that are waiting first, one a frame, and what is held once they are all through
	if (buttonCount == 0)
		return buttonsNow;
	const uint8_t buttons = buttonQueue[buttonFirst];
	buttonFirst = (uint8_t)((buttonFirst + 1) % BUTTONS_QUEUE);
	buttonCount--;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	//the processor counts on by itself, get_ticks_us keeps count of that counter wrapping around
	return (uint32_t)get_ticks_us();
}

//The square wave is built into the buffers the sound hardware plays from, which main() hands over
//in between the game's frames.
//
//A buffer is a 25th of a second, and the game asks for tones far shorter than that: the step it
//walks takes 16 ms and a note of the coin sound 33. Playing whatever tone the game happens to want
//at the moment a buffer is filled loses every tone that starts and ends between two of them, so the
//tones are lined up in a queue instead and every one of them is played for as long as it asked for,
//a little later than it was asked for
#define TONE_RATE 22050
//Two buffers is the least libdragon takes, and the least sound already on its way out: a tone is
//only heard once what is queued ahead of it has played. A frame that took longer than a buffer
//would leave the hardware without one, which the N64 has the speed to spare for
#define TONE_BUFFERS 2
//tones waiting to be played, more than the games ever have going at once
#define TONE_QUEUE 16

typedef struct
{
	//how much of a cycle one sample is, as a fraction of 65536. 0 is silence
	uint32_t step;
	//how many samples of it are still to be played
	uint32_t samples;
	//true while this is the tone the game asked for last: it plays on until the next one
	bool open;
} Tone;

static bool soundReady = false;
static Tone toneQueue[TONE_QUEUE];
static uint8_t toneFirst = 0, toneCount = 0;
//where in its cycle the tone being played is
static uint32_t tonePhase = 0;
//when the tone that plays on was asked for and how much of it has been played, so that it ends up
//as long as it was the tone the game wanted
static uint32_t openMicros = 0, openSamples = 0;

//count samples of the square wave into the buffer, both channels the same
static void ToneRender(short* buffer, size_t count, uint32_t step)
{
	if (step == 0)
	{
		memset(buffer, 0, count * 2 * sizeof(short));
		return;
	}
	const short high = (short)(0x7FFF * SOUNDVOLUME / 100);
	const short low = (short)-high;
	uint32_t phase = tonePhase;
	for (size_t i = 0; i < count; i++)
	{
		//the top bit of the fraction says which half of the cycle the sample is in
		const short value = (phase & 0x8000u) ? low : high;
		buffer[i * 2] = value;
		buffer[i * 2 + 1] = value;
		phase = (phase + step) & 0xFFFFu;
	}
	tonePhase = phase;
}

//one buffer out of the queue, silence for whatever the queue does not fill
static void ToneFill(short* buffer, size_t numsamples)
{
	while (numsamples)
	{
		if (toneCount == 0)
		{
			memset(buffer, 0, numsamples * 2 * sizeof(short));
			return;
		}
		Tone* tone = &toneQueue[toneFirst];
		size_t count = numsamples;
		//the tone the game asked for last fills the rest, it has no end of its own yet
		if (!tone->open && (tone->samples < count))
			count = (size_t)tone->samples;
		ToneRender(buffer, count, tone->step);
		buffer += count * 2;
		numsamples -= count;
		if (tone->open)
			openSamples += (uint32_t)count;
		else
		{
			tone->samples -= (uint32_t)count;
			if (tone->samples == 0)
			{
				//on to the next one, which starts at the beginning of its own cycle
				toneFirst = (uint8_t)((toneFirst + 1) % TONE_QUEUE);
				toneCount--;
				tonePhase = 0;
			}
		}
	}
}

//fills every buffer the sound hardware has room for, which is what keeps it playing
static void ToneUpdate(void)
{
	if (!soundReady)
		return;
	while (audio_can_write())
	{
		short* buffer = audio_write_begin();
		if (!buffer)
			return;
		ToneFill(buffer, (size_t)audio_get_buffer_length());
		audio_write_end();
	}
}

static void TonePush(uint32_t step, uint32_t samples, bool open)
{
	if (toneCount == TONE_QUEUE)
	{
		//the games never fill this, the oldest tone goes if one ever does
		toneFirst = (uint8_t)((toneFirst + 1) % TONE_QUEUE);
		toneCount--;
		tonePhase = 0;
	}
	if (toneCount)
	{
		Tone* last = &toneQueue[(toneFirst + toneCount - 1) % TONE_QUEUE];
		if (last->open)
		{
			//The tone that played on ends here: it is as long as it was the one the game wanted,
			//and never shorter than what has gone out of it already. The queue is filled a buffer
			//at a time, so without this a note would come out a buffer too long or too short
			const uint32_t played = (uint32_t)(((uint64_t)(Platform_Micros() - openMicros) * TONE_RATE) / 1000000u);
			last->samples = (played > openSamples) ? (played - openSamples) : 0;
			last->open = false;
		}
	}
	Tone* tone = &toneQueue[(toneFirst + toneCount) % TONE_QUEUE];
	tone->step = step;
	tone->samples = samples;
	tone->open = open;
	toneCount++;
	if (open)
	{
		openMicros = Platform_Micros();
		openSamples = 0;
	}
}

static void SoundInit(void)
{
	audio_init(TONE_RATE, TONE_BUFFERS);
	toneFirst = 0;
	toneCount = 0;
	tonePhase = 0;
	soundReady = true;
}

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	if (!soundReady)
		return;
	//a frequency of 0 is a rest, which is queued like any other tone so what follows it stays in
	//its place
	uint32_t step = 0;
	if (freq)
	{
		//how much of a cycle one sample is
		step = ((uint32_t)freq * 65536u + TONE_RATE / 2) / TONE_RATE;
		if (step == 0)
			step = 1;
	}
	//0 plays until the next tone, the others are as many samples as the milliseconds they ask for
	uint32_t samples = 0;
	if (duration)
	{
		samples = (uint32_t)duration * TONE_RATE / 1000u;
		if (samples == 0)
			samples = 1;
	}
	TonePush(step, samples, duration == 0);
}

void Platform_StopTone(void)
{
	toneFirst = 0;
	toneCount = 0;
	tonePhase = 0;
}

uint32_t Platform_FreeHeap(void)
{
	//what the heap has not handed out of the memory the console found when it started
	struct mallinfo info = mallinfo();
	const uint32_t total = (uint32_t)get_memory_size();
	const uint32_t used = (uint32_t)info.uordblks;
	return (total > used) ? (total - used) : 0;
}

uint32_t Platform_FreeStack(void)
{
	//what is between the stack as it stands now and the end of the heap below it
	const uintptr_t brk = (uintptr_t)sbrk(0);
	const uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
	return (frame > brk) ? (uint32_t)(frame - brk) : 0;
}

uint32_t Platform_RandomSeed(void)
{
	//how long the console took to reach the game, which is never quite the same twice
	return (uint32_t)TICKS_READ();
}

void Platform_Log(const char* format, ...)
{
	//goes to the debugger of an emulator or a flash cart, the screen is the game's
	char message[128];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	debugf("%s", message);
}

// ===========================================================================
// Saved data
//
// The cartridge's EEPROM, which holds 512 bytes in blocks of 8: the block the game saves fits in it
// twice over. Without one (an emulator that is not told the cartridge has one) the block only lives
// for as long as the game runs.
// ===========================================================================

static uint8_t storage[PLATFORM_STORAGE_SIZE];
static bool storageSaved = false;

//the EEPROM is written a block at a time, so the whole block is kept in memory and only the parts
//of it that change go out
#define STORAGE_BLOCKS (PLATFORM_STORAGE_SIZE / EEPROM_BLOCK_SIZE)

static void StorageInit(void)
{
	memset(storage, 0xFF, sizeof(storage));
	storageSaved = (eeprom_present() != EEPROM_NONE) && (eeprom_total_blocks() >= STORAGE_BLOCKS);
	if (storageSaved)
		eeprom_read_bytes(storage, 0, sizeof(storage));
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
	if (!storageSaved)
		return;
	//only the blocks the write reached into, each one of them costs about 15 ms
	const uint16_t first = (uint16_t)(offset / EEPROM_BLOCK_SIZE);
	const uint16_t last = (uint16_t)((offset + length - 1) / EEPROM_BLOCK_SIZE);
	for (uint16_t block = first; block <= last; block++)
		eeprom_write((uint8_t)block, storage + block * EEPROM_BLOCK_SIZE);
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	(void)appName;
	StorageInit();
	DisplayInit();
	ButtonsInit();
	SoundInit();
#if SCREENBUFFER
	screenBuffer.setColorDepth(SCREENBUFFER);
	screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT);
#endif
#if SCREENBUFFER == 8
	//RGB332 to RGB565 the way LovyanGFX converts it, so the colours match the other devices
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
		bufferPalette[i] = ToRGBA5551((uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3)));
	}
#endif
}

#endif
