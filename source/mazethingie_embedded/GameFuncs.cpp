#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "GameFuncs.h"

//multi line text straight to the screen. tft.drawChar already matches what the
//framebuffer version did per character, a 6x8 cell with the background only
//painted when it differs from the text colour, so only the line breaks are
//handled here. Advances match the old code, 6 pixels per char and 9 per line
void tftPrint(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size)
{
	int16_t cursorX = x;
	int16_t cursorY = y;
	if (!str)
		return;
#if LOVYANGFX
	//LovyanGFX's drawChar that takes the colours hands them to the font the other way
	//round, set them as the text colour instead. Its default font is the same 6x8 GLCD
	//font and a background equal to the text colour is left out here as well
	GFX.setTextColor(color, bg);
	GFX.setTextSize(size);
#endif
#if SCREENBUFFER == 0
	//Straight to the display every character would be a write transaction of its own, and
	//the chip select sits on the I/O expander: that is I2C traffic per character. One
	//transaction for the whole text instead. Into a buffer nothing is sent, so nothing to do
	SCREEN.startWrite();
#endif
	while (*str)
	{
		if (*str == '\n')
		{
			cursorY += 9 * size;
			cursorX = x;
			str++;
			continue;
		}
#if LOVYANGFX
		GFX.drawChar((uint8_t)*str, cursorX, cursorY);
#else
		GFX.drawChar(cursorX, cursorY, *str, color, bg, size);
#endif
		cursorX += 6 * size;
		str++;
	}
#if SCREENBUFFER == 0
	SCREEN.endWrite();
#endif
}

//length of the longest line in Text, the count restarts on every newline so a
//multi line message reports the width it actually needs and not its total length
uint8_t MaxLineLen(const char* Text)
{
	uint8_t Result = 0;
	uint8_t Len = 0;
	if (!Text)
		return 0;
	while (*Text)
	{
		if (*Text == '\n')
			Len = 0;
		else
		{
			Len++;
			if (Len > Result)
				Result = Len;
		}
		Text++;
	}
	return Result;
}
