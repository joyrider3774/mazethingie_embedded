#include <stdio.h>
#include <string.h>
#include "Commonvars.h"
#include "SaveData.h"
#include "Maze.h"
#include "Player.h"
#include "Titlescreen.h"
#include "GameFuncs.h"

//the whole of the title screen
static void DrawTitleScreen()
{
	int MenuXPos = 15;
	int MenuYPos = 30;
	GFX.fillRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, ColorBlack);
	tftPrint(30,5, "MAZE THINGIE", ColorWhite, ColorWhite, 1);
	if (Selection == 1)
	{
		tftPrint(MenuXPos, MenuYPos, "Play", ColorWhite,ColorWhite,1);
	}
	else
	{
		
		tftPrint(MenuXPos, MenuYPos, "Play", ColorGrey, ColorGrey, 1);
	}

	char Text[30];
	snprintf(Text, sizeof(Text), "Maze Width: %d", MazeWidth);
	if (Selection == 2)
	{
		tftPrint(MenuXPos, MenuYPos + 12, Text, ColorWhite,ColorWhite,1);
	}
	else
	{
		tftPrint(MenuXPos, MenuYPos + 12, Text, ColorGrey, ColorGrey, 1);
	}

	snprintf(Text, sizeof(Text), "Maze Height: %d", MazeHeight);
	if (Selection == 3)
	{
		tftPrint(MenuXPos, MenuYPos + 24, Text, ColorWhite,ColorWhite,1);
	}
	else
	{
		tftPrint(MenuXPos, MenuYPos + 24, Text, ColorGrey, ColorGrey, 1);
	}
	
	//two lines of 9 pixels, the second one ending on the bottom row of the screen
	tftPrint(30, WINDOW_HEIGHT - 18, " Created by\nWillems Davy", ColorWhite,ColorWhite,1);
#if SCREENBUFFER == 1
	//grey is white in a 1 bpp buffer, so the selection is marked instead
	tftPrint(MenuXPos - 8, MenuYPos + (Selection - 1) * 12, ">", ColorWhite, ColorWhite, 1);
#endif
}

void TitleScreen()
{
	if (currButtons & BUTTON_UP)
	{
		if (!(prevButtons & BUTTON_UP))
			frameUpStart = framecount;
		if ((framecount-frameUpStart) % MenuSpeedTickVal == 0)
			if (Selection > 1)
				Selection--;
	}

	if (currButtons & BUTTON_DOWN)
	{
		if (!(prevButtons & BUTTON_DOWN))
			frameDownStart = framecount;
		if ((framecount - frameDownStart) % MenuSpeedTickVal == 0)
			if (Selection < 3)
				Selection++;
	}

	if ((currButtons & BUTTON_A) && !(prevButtons & BUTTON_A))
	{
		saveSaveState();
		GenerateMaze();
		GFX.fillRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, ColorBlack);
		GameWon = 0;
		DrawMaze();
		DrawPlayer();
		GameState = GSGame;
	}

	if (Selection == 2)
	{
		if(currButtons & BUTTON_RIGHT)
		{
			if (!(prevButtons & BUTTON_RIGHT))
				frameRightStart = framecount;
			if ((framecount - frameRightStart) % MenuSpeedTickVal == 0)
				if (MazeWidth < MaxMazeWidth)
				{
					MazeWidth++;
					saveSaveState();
				}
		}

		if (currButtons & BUTTON_LEFT)
		{
			if (!(prevButtons & BUTTON_LEFT))
				frameLeftStart = framecount;
			if ((framecount - frameLeftStart) % MenuSpeedTickVal == 0)
				if (MazeWidth > 10)
				{
					MazeWidth--;
					saveSaveState();
				}
		}

	}

	if (Selection == 3)
	{
		if (currButtons & BUTTON_RIGHT)
		{
			if (!(prevButtons & BUTTON_RIGHT))
				frameRightStart = framecount;
			if ((framecount - frameRightStart) % MenuSpeedTickVal == 0)
				if (MazeHeight < MaxMazeHeight)
				{
					MazeHeight++;
					saveSaveState();
				}
		}

		if (currButtons & BUTTON_LEFT)
		{
			if (!(prevButtons & BUTTON_LEFT))
				frameLeftStart = framecount;
			if ((framecount - frameLeftStart) % MenuSpeedTickVal == 0)
				if (MazeHeight > 10)
				{
					MazeHeight--;
					saveSaveState();
				}
		}

	}

	if (GameState != GSTitleScreen)
		return;

	//Only drawn again when something on it changed: cleared and drawn every frame, it flickers on
	//a device that draws straight to the display. The debug header is drawn over the top of it, so
	//once that is turned off the screen is drawn again to take it away
	static int drawnSelection = -1, drawnWidth = -1, drawnHeight = -1;
	static uint8_t drawnDebug = 0;
	if (TitleRedraw || (Selection != drawnSelection) || (MazeWidth != drawnWidth) ||
		(MazeHeight != drawnHeight) || (drawnDebug && !debugMode))
	{
		DrawTitleScreen();
		TitleRedraw = 0;
		drawnSelection = Selection;
		drawnWidth = MazeWidth;
		drawnHeight = MazeHeight;
	}
	drawnDebug = debugMode;
}
