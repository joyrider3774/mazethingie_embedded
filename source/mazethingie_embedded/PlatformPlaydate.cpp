//Platform.h for the Panic Playdate, built with the Playdate C SDK (see playdate/CMakeLists.txt) for
//the device and for the simulator:
//  display  400x240, 1 bit per pixel, a set bit is white. The game's 128x128 screen buffer is turned
//           into those pixels at the end of every frame, scaled up to 240x240 in the middle
//  buttons  d-pad, A and B. The side buttons are entries of the system menu, see PLAYDATE_MENU_L
//  sound    a square wave synth
//  saves    a file named after the game in its data folder, <Game>.sav
//
//The Playdate calls eventHandler, which starts the game and has Game_Loop called once per display
//refresh, at the game's frame rate.

#include "Platform.h"
//every platform's source sits in the sketch folder, only the one being built compiles
#ifdef PLATFORM_PLAYDATE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

//the SDK's headers are C without a C++ guard of their own
extern "C" {
#include <pd_api.h>
}

#include "PlatformGamebuinoFont.h"

#define DISPLAY_WIDTH LCD_COLUMNS
#define DISPLAY_HEIGHT LCD_ROWS
//the game's screen sits in the middle, unless SCALESCREEN scales it to SCALED_SIZE
#define DISPLAY_OFFSET_X ((DISPLAY_WIDTH - WINDOW_WIDTH) / 2)
#define DISPLAY_OFFSET_Y ((DISPLAY_HEIGHT - WINDOW_HEIGHT) / 2)
#define SCALED_SIZE DISPLAY_HEIGHT
#define SCALED_OFFSET_X ((DISPLAY_WIDTH - SCALED_SIZE) / 2)

static PlaydateAPI* pd = nullptr;

static PlatformPlaydateDisplay display;
PlatformDisplay& platformDisplay = display;

//the whole frame is drawn in here and turned into the Playdate's pixels at the end of Game_Loop
PlatformBuffer screenBuffer;
#if SCREENBUFFER == 1
//whether the set bits are the brighter colour, from Platform_SetBufferColors
static bool setIsWhite = true;
#endif

// ===========================================================================
// Program start
//
// The device runs the program without the C runtime's start up, so the constructors of the global
// objects (the display and the screen buffer) are called here first. The simulator's dll has them
// called when it is loaded
// ===========================================================================

#if TARGET_PLAYDATE
extern "C" {
extern void (*__preinit_array_start[])(void);
extern void (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
//C++ support the device's -nostartfiles build does not bring along: nothing is ever destroyed at exit
//and no pure virtual function is ever called
void* __dso_handle = nullptr;
int __aeabi_atexit(void* object, void (*destructor)(void*), void* dso) { (void)object; (void)destructor; (void)dso; return 0; }
void __cxa_pure_virtual(void) { while (true) {} }
}

static void RunConstructors(void)
{
	for (void (**f)(void) = __preinit_array_start; f < __preinit_array_end; f++)
		(*f)();
	for (void (**f)(void) = __init_array_start; f < __init_array_end; f++)
		(*f)();
}
#endif

//a frame was drawn since the display was last updated
static bool framePresented = false;
static int UpdateDisplay(void);

static int Update(void* userdata)
{
	(void)userdata;
	Game_Loop();
	return UpdateDisplay();
}

extern "C" {
#ifdef _WIN32
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg)
{
	(void)arg;
	if (event == kEventInit)
	{
		pd = playdate;
#if TARGET_PLAYDATE
		RunConstructors();
#endif
		Game_Setup();
		pd->system->setUpdateCallback(Update, nullptr);
	}
	return 0;
}
}

//operator new and delete the Playdate way, the virtual destructors refer to delete
void* operator new(size_t size) { return malloc(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t size) noexcept { (void)size; free(p); }

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
void PlatformPlaydateDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)color;
}

void PlatformPlaydateGFX::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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
size_t PlatformPlaydateGFX::drawChar(uint16_t c, int32_t x, int32_t y)
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

bool PlatformPlaydateBuffer::createSprite(int32_t w, int32_t h)
{
	if (pixels)
		free(pixels);
	const size_t bytes = (depth == 1) ? (size_t)((w + 7) / 8) * h : (size_t)w * h * ((depth == 16) ? 2 : 1);
	pixels = (uint8_t*)malloc(bytes);
	if (pixels)
		memset(pixels, 0, bytes);
	return pixels != nullptr;
}

void PlatformPlaydateBuffer::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
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

//The display column and row every game column and row starts at, one more for where the last one
//ends. 1:1 in the middle of the display, or with SCALESCREEN nearest neighbour over 240x240 in its
//middle: 240 / 128 is not a whole number, so a game pixel is 1 or 2 display pixels wide
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

#if SCREENBUFFER != 1
//4x4 ordered dither thresholds, 0-255
static const uint8_t ditherThreshold[4][4] = {
	{   8, 136,  40, 168 },
	{ 200,  72, 232, 104 },
	{  56, 184,  24, 152 },
	{ 248, 120, 216,  88 }
};
//brightness 0-255 of every RGB332 value, or of the RGB565 pixel, the way SetBufferBit weighs it
  #if SCREENBUFFER == 8
static uint8_t bufferBrightness[256];
  #endif
static inline uint8_t Brightness565(uint16_t color)
{
	return (uint8_t)(((((color >> 11) & 0x1F) << 3) * 77 + (((color >> 5) & 0x3F) << 2) * 150 + ((color & 0x1F) << 3) * 29) >> 8);
}
#endif

void Platform_SetBufferColors(uint16_t setColor, uint16_t clearColor)
{
#if SCREENBUFFER == 1
	//set bits are white when the set colour is the brighter one
	const uint16_t setLum = (uint16_t)(((setColor >> 11) & 0x1F) * 2 + ((setColor >> 5) & 0x3F) + (setColor & 0x1F));
	const uint16_t clearLum = (uint16_t)(((clearColor >> 11) & 0x1F) * 2 + ((clearColor >> 5) & 0x3F) + (clearColor & 0x1F));
	setIsWhite = setLum >= clearLum;
#else
	(void)setColor;
	(void)clearColor;
#endif
}

void Platform_PresentFrame(void)
{
	framePresented = true;
}

//Turns the frame into the display's pixels, only the display rows that changed are written and
//marked. Nonzero when the display has to be updated
static int UpdateDisplay(void)
{
	if (!framePresented)
		return 0;
	framePresented = false;
	const uint8_t* src = (const uint8_t*)SCREENBUFFER_PIXELS();
	uint8_t* frame = pd->graphics->getFrame();
	if (!src || !frame)
		return 0;
	uint8_t line[LCD_ROWSIZE];
	int firstChanged = -1, lastChanged = -1;
	for (uint16_t sy = 0; sy < WINDOW_HEIGHT; sy++)
	{
		for (uint16_t dy = displayRow[sy]; dy < displayRow[sy + 1]; dy++)
		{
			//black around the game's screen
			memset(line, 0, sizeof(line));
			for (uint16_t sx = 0; sx < WINDOW_WIDTH; sx++)
			{
#if SCREENBUFFER == 1
				const bool bit = ((src[(sy * WINDOW_WIDTH + sx) >> 3] & (0x80 >> ((sy * WINDOW_WIDTH + sx) & 7))) != 0);
				const bool white = (bit == setIsWhite);
#elif SCREENBUFFER == 8
				const uint8_t lum = bufferBrightness[src[sy * WINDOW_WIDTH + sx]];
#else
				const uint8_t lum = Brightness565((uint16_t)((src[(sy * WINDOW_WIDTH + sx) * 2] << 8) | src[(sy * WINDOW_WIDTH + sx) * 2 + 1]));
#endif
				for (uint16_t dx = displayColumn[sx]; dx < displayColumn[sx + 1]; dx++)
				{
#if SCREENBUFFER != 1
					const bool white = lum > ditherThreshold[dy & 3][dx & 3];
#endif
					if (white)
						line[dx >> 3] |= (uint8_t)(0x80 >> (dx & 7));
				}
			}
			uint8_t* row = &frame[dy * LCD_ROWSIZE];
			if (memcmp(row, line, LCD_ROWSIZE) != 0)
			{
				memcpy(row, line, LCD_ROWSIZE);
				if (firstChanged < 0)
					firstChanged = dy;
				lastChanged = dy;
			}
		}
	}
	if (firstChanged < 0)
		return 0;
	pd->graphics->markUpdatedRows(firstChanged, lastChanged);
	return 1;
}

// ===========================================================================
// Buttons
// ===========================================================================

//a system menu entry chosen since the buttons were last read
static bool menuPressedL = false, menuPressedR = false;

#ifdef PLAYDATE_MENU_L
static void MenuL(void* userdata)
{
	(void)userdata;
	menuPressedL = true;
}
#endif
#ifdef PLAYDATE_MENU_R
static void MenuR(void* userdata)
{
	(void)userdata;
	menuPressedR = true;
}
#endif

uint8_t Platform_GetButtons(void)
{
	PDButtons current, pushed, released;
	pd->system->getButtonState(&current, &pushed, &released);
	//a press that came and went between two frames still counts for this one
	const uint32_t held = (uint32_t)current | (uint32_t)pushed;
	uint8_t buttons = 0;
	if (held & kButtonLeft)
		buttons |= BUTTON_LEFT;
	if (held & kButtonUp)
		buttons |= BUTTON_UP;
	if (held & kButtonDown)
		buttons |= BUTTON_DOWN;
	if (held & kButtonRight)
		buttons |= BUTTON_RIGHT;
	if (held & kButtonA)
		buttons |= BUTTON_A;
	if (held & kButtonB)
		buttons |= BUTTON_B;
	//a menu entry is a press of one frame
	if (menuPressedL)
		buttons |= BUTTON_L;
	if (menuPressedR)
		buttons |= BUTTON_R;
	menuPressedL = menuPressedR = false;
	return buttons;
}

// ===========================================================================
// Time, sound and memory
// ===========================================================================

uint32_t Platform_Micros(void)
{
	//the Playdate counts milliseconds
	return (uint32_t)pd->system->getCurrentTimeMilliseconds() * 1000u;
}

static PDSynth* synth = nullptr;

void Platform_PlayTone(uint16_t freq, uint16_t duration)
{
	if (!synth)
		return;
	//a frequency of 0 is a rest
	if (!freq)
	{
		pd->sound->synth->noteOff(synth, 0);
		return;
	}
	//a length of -1 plays until the next tone or Platform_StopTone
	const float length = duration ? duration / 1000.0f : -1.0f;
	pd->sound->synth->playNote(synth, (float)freq, SOUNDVOLUME / 100.0f, length, 0);
}

//the Playdate does not tell how much heap or stack is free
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
	unsigned int milliseconds = 0;
	const unsigned int seconds = pd->system->getSecondsSinceEpoch(&milliseconds);
	return (uint32_t)(seconds * 1000u + milliseconds);
}

void Platform_Log(const char* format, ...)
{
	char text[128];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	pd->system->logToConsole("%s", text);
}

// ===========================================================================
// Saved data
//
// The whole storage block is kept in RAM and in the file <Game>.sav in the game's data folder,
// named after the first word of the game's name. Bytes the file does not have yet read as 0xFF, the
// way erased flash does on the ESPboy, so the game sees a never saved store.
// ===========================================================================

static uint8_t storage[PLATFORM_STORAGE_SIZE];
static char storagePath[32] = "game.sav";

static void StorageInit(const char* appName)
{
	memset(storage, 0xFF, sizeof(storage));
	size_t n = 0;
	while (appName[n] && (appName[n] != ' ') && (n < 16))
		n++;
	if (n)
		snprintf(storagePath, sizeof(storagePath), "%.*s.sav", (int)n, appName);
	SDFile* file = pd->file->open(storagePath, kFileReadData);
	if (file)
	{
		pd->file->read(file, storage, sizeof(storage));
		pd->file->close(file);
	}
}

void Platform_StorageRead(uint16_t offset, uint8_t* data, uint16_t length)
{
	memcpy(data, storage + offset, length);
}

void Platform_StorageWrite(uint16_t offset, const uint8_t* data, uint16_t length)
{
	//storing what is already there does not touch the file
	if (memcmp(storage + offset, data, length) == 0)
		return;
	memcpy(storage + offset, data, length);
	SDFile* file = pd->file->open(storagePath, kFileWrite);
	if (!file)
	{
		Platform_Log("could not write %s\n", storagePath);
		return;
	}
	pd->file->write(file, storage, sizeof(storage));
	pd->file->close(file);
}

// ===========================================================================
// Start
// ===========================================================================

void Platform_Init(const char* appName)
{
	//once per refresh at the game's frame rate, see FPSLOCK in PlatformPlaydate.h
	pd->display->setRefreshRate((float)FRAMERATE);
	pd->graphics->clear(kColorBlack);

	synth = pd->sound->synth->newSynth();
	if (synth)
		pd->sound->synth->setWaveform(synth, kWaveformSquare);

#ifdef PLAYDATE_MENU_L
	pd->system->addMenuItem(PLAYDATE_MENU_L, MenuL, nullptr);
#endif
#ifdef PLAYDATE_MENU_R
	pd->system->addMenuItem(PLAYDATE_MENU_R, MenuR, nullptr);
#endif

	StorageInit(appName);

	screenBuffer.setColorDepth(SCREENBUFFER);
	if (!screenBuffer.createSprite(WINDOW_WIDTH, WINDOW_HEIGHT))
		Platform_Log("screen buffer could not be allocated\n");
#if SCREENBUFFER == 8
	//RGB332 to RGB565 the way LovyanGFX converts it, then its brightness
	for (uint16_t i = 0; i < 256; i++)
	{
		const uint8_t r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
		const uint16_t color = (uint16_t)((((r3 * 9) >> 1) << 11) | ((g3 * 9) << 5) | ((b2 * 0x55) >> 3));
		bufferBrightness[i] = Brightness565(color);
	}
#endif
	MapDisplay();
}

#endif
