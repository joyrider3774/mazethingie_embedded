#include "Platform.h"
#include "Commonvars.h"
#include "Defines.h"

int GameState = GSTitleScreen;
int MazeWidth = MaxMazeWidth, MazeHeight = MaxMazeHeight, PlayerPosX = 32, PlayerPosY = 32, Selection = 1, xoffset = 0, yoffset = 0, framecount = 0, GameWon = 0;
uint8_t Maze[MaxMazeSize];
int frameUpStart = 0, frameDownStart = 0, frameLeftStart = 0, frameRightStart = 0;
//RGB565, written out rather than asked of the display: these are set before the display exists
uint16_t ColorWhite = 0xFFFF;
uint16_t ColorBlack = 0x0000;
#if SCREENBUFFER == 1
//A 1 bpp buffer shows anything darker than mid grey as clear, which would leave the menu items
//that are not selected and the player as black on black. White is all that shows there
uint16_t ColorGrey = 0xFFFF;
uint16_t ColorRed = 0xFFFF;
#else
uint16_t ColorGrey = 0x632C;   //100,100,100
uint16_t ColorRed = 0xF800;    //255,0,0
#endif
uint8_t debugMode = 0;
uint8_t TitleRedraw = 1;
uint8_t currButtons = 0, prevButtons = 0;