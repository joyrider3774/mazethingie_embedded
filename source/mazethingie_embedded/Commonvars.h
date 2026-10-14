#ifndef COMMONVARS_H
#define COMMONVARS_H

#include <stdint.h>
#include "Defines.h"

extern int GameState;
extern int MazeWidth, MazeHeight, PlayerPosX, PlayerPosY, Selection, xoffset, yoffset, framecount, GameWon;
extern uint8_t Maze[MaxMazeSize];
extern uint8_t debugMode;
//set to have the title screen drawn again in full the next time it runs
extern uint8_t TitleRedraw;
extern uint8_t currButtons, prevButtons;
extern int frameUpStart, frameDownStart, frameLeftStart, frameRightStart;
extern uint16_t ColorWhite;
extern uint16_t ColorBlack;
extern uint16_t ColorGrey;
extern uint16_t ColorRed;

#endif
