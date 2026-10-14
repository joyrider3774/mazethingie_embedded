#include <string.h>
#include "Commonvars.h"
#include "GameFuncs.h"
#include "Player.h"
#include "Maze.h"
#include "Game.h"
#include "SaveData.h"
#include "Platform.h"

void Game()
{
	if ((currButtons & BUTTON_B) && !(prevButtons & BUTTON_B))
	{
		GameState = GSTitleScreen;
		TitleRedraw = 1;
	}

	if( GameWon )
		if ((currButtons & BUTTON_A) && !(prevButtons & BUTTON_A))
		{
			GameState = GSTitleScreen;
			TitleRedraw = 1;
		}

	if (!GameWon)
	{
		if (currButtons & BUTTON_A)
		{
			
			int regen = 0;
			if (currButtons & BUTTON_LEFT)
			{
				if (!(prevButtons & BUTTON_LEFT))
					frameLeftStart = framecount;
				if ((framecount - frameLeftStart) % MenuSpeedTickVal == 0)
					if (MazeWidth > 10)
					{
						MazeWidth--;
						regen = 1;
					}
			}

			if (currButtons & BUTTON_RIGHT)
			{
				if (!(prevButtons & BUTTON_RIGHT))
					frameRightStart = framecount;
				if ((framecount - frameRightStart) % MenuSpeedTickVal == 0)
					if (MazeWidth < MaxMazeWidth)
					{
						MazeWidth++;
						regen = 1;
					}
			}

			if (currButtons & BUTTON_DOWN)
			{
				if (!(prevButtons & BUTTON_DOWN))
					frameDownStart = framecount;
				if ((framecount - frameDownStart) % MenuSpeedTickVal == 0)
					if (MazeHeight < MaxMazeHeight)
					{
						MazeHeight++;
						regen = 1;
					}
			}

			if (currButtons & BUTTON_UP)
			{
				if (!(prevButtons & BUTTON_UP))
					frameUpStart = framecount;
				if ((framecount - frameUpStart) % MenuSpeedTickVal == 0)
					if (MazeHeight > 10)
					{
						MazeHeight--;
						regen = 1;
					}
			}

			if (regen)
			{
				saveSaveState();
				GameWon = 0;
				GFX.fillRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, ColorBlack);
				GenerateMaze();
				DrawMaze();
				DrawPlayer();
			}
		}
		else
		{
			
			if (currButtons & BUTTON_RIGHT)
			{
				if (!(prevButtons &BUTTON_RIGHT))
					frameRightStart = framecount;
				if ((framecount - frameRightStart) % PlayerSpeedTickVal == 0)
					if (!(Maze[(PlayerPosY * MazeWidth) + PlayerPosX] & 2))
					{
						ErasePlayer();
						PlayerPosX++;
						DrawPlayer();
					}
			}

			if (currButtons & BUTTON_LEFT)
			{
				if (!(prevButtons & BUTTON_LEFT))
					frameLeftStart = framecount;
				if ((framecount - frameLeftStart) % PlayerSpeedTickVal == 0)
					if (!(Maze[(PlayerPosY * MazeWidth) + PlayerPosX] & 8))
					{
						ErasePlayer();
						PlayerPosX--;
						DrawPlayer();
					}
			}

			if (currButtons & BUTTON_UP)
			{
				if(!(prevButtons & BUTTON_UP))
					frameUpStart = framecount;
			
				if ((framecount - frameUpStart) % PlayerSpeedTickVal == 0)
				{
					if ((PlayerPosX == 0) && (PlayerPosY == 0))
					{
						GameWon = 1;
						const char* msg = "Congratulations !!!\nYou solved the maze,\nlet's try another";
						//a character is 6 pixels wide with its last column empty, a line 9 high with
						//8 of them drawn, so this is the size of what actually shows
						int lines = 1;
						for (const char* c = msg; *c; c++)
							if (*c == '\n')
								lines++;
						int tw = MaxLineLen(msg) * 6 - 1;
						int th = (lines - 1) * 9 + 8;
						int tx = (WINDOW_WIDTH - tw) >> 1;
						int ty = (WINDOW_HEIGHT - th) >> 1;
						//a border 2 pixels clear of the text, and a black edge around it so it stands
						//apart from the maze, all of it kept on the screen
						int bx = tx - 3, by = ty - 3, bw = tw + 6, bh = th + 6;
						int fx = (bx > 0) ? bx - 1 : 0;
						int fy = (by > 0) ? by - 1 : 0;
						int fw = ((bx + bw < WINDOW_WIDTH) ? bx + bw + 1 : WINDOW_WIDTH) - fx;
						int fh = ((by + bh < WINDOW_HEIGHT) ? by + bh + 1 : WINDOW_HEIGHT) - fy;
						GFX.fillRect(fx, fy, fw, fh, ColorBlack);
						GFX.drawRect(bx, by, bw, bh, ColorWhite);
						tftPrint(tx, ty, msg, ColorWhite, ColorWhite, 1);
					}
					else
						if (!(Maze[(PlayerPosY * MazeWidth) + PlayerPosX] & 1))
						{
							ErasePlayer();
							PlayerPosY--;
							DrawPlayer();
						}
				}
			}

			if (currButtons & BUTTON_DOWN)
			{
				if (!(prevButtons & BUTTON_DOWN))
					frameDownStart = framecount;
				if ((framecount-frameDownStart) % PlayerSpeedTickVal == 0)
					if ((PlayerPosX != MazeWidth - 1) || (PlayerPosY != MazeHeight - 1))
						if (!(Maze[(PlayerPosY * MazeWidth) + PlayerPosX] & 4))
						{
							ErasePlayer();
							PlayerPosY++;
							DrawPlayer();
						}
			}
		}
	}
}