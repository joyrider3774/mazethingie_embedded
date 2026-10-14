#include <stdlib.h>
#include <string.h>
#include "Defines.h"
#include "Platform.h"
#include "Commonvars.h"

void DrawMaze()
{

    int X, Y, I;
    //clear_screen (color_red);
    for (Y = 0; Y < MazeHeight; Y++)
        for (X = 0; X < MazeWidth; X++)
        {
            I = (Y * MazeWidth) + X;
            //north wall
            if (Maze[I] & 1)
            {
                GFX.fillRect(xoffset + (X + 1) * BoxWidth, yoffset + (Y + 1) * BoxHeight,BoxWidth+1,1,ColorWhite);
            }

            //east wall
            if (Maze[I] & 2)
            {
                GFX.fillRect(xoffset + (X + 2) * BoxWidth, yoffset + (Y + 1) * BoxHeight, 1, BoxHeight+1,ColorWhite);
            }

            //south wall
            if (Maze[I] & 4)
            {
                GFX.fillRect(xoffset + (X + 1) * BoxWidth, yoffset + (Y + 2) * BoxHeight, BoxWidth+1, 1, ColorWhite);
            }

            //west wall
            if (Maze[I] & 8)
            {
                GFX.fillRect(xoffset + (X + 1) * BoxWidth, yoffset + (Y + 1) * BoxHeight, 1, BoxHeight+1, ColorWhite);
            }
        }
}

//A room's walls are its low 4 bits. While the maze is dug, bits 4 and 5 say which neighbour it was
//entered from, which is the way back out of a dead end. That keeps a stack of rooms to return to,
//2 bytes for every room of the largest maze, out of the RAM: the CHGame needs it for its 8 bpp
//screen buffer. The rooms passed on the way back had nothing left to visit, which is why the old
//stack only kept the ones that had a choice, so the same seed digs the same maze
#define PARENT_SHIFT 4
#define PARENT_WEST  0
#define PARENT_EAST  1
#define PARENT_NORTH 2
#define PARENT_SOUTH 3

void GenerateMaze()
{
    srand(framecount + MazeWidth + MazeHeight);
    uint16_t neighbours[4];
    uint16_t currentPoint = 0;
    uint16_t visitedRooms = 1;
    uint16_t tmp2;
    uint16_t selectedNeighbour;
    uint16_t rnd;

    //intial all walls value in every room we will remove bits of this value to remove walls
    memset(Maze, 0xfu, (size_t)(MaxMazeSize));

    while (visitedRooms != MazeHeight * MazeWidth)
    {
        uint16_t neighboursFound = 0;
        uint16_t lookUpX = currentPoint % MazeWidth;
        uint16_t lookUpY = currentPoint / MazeWidth;
        uint16_t tmp = currentPoint + 1;
        //tile has neighbour to the right which we did not handle yet
        if ((lookUpX + 1 < MazeWidth) && (Maze[tmp] == 0xfu))
            neighbours[neighboursFound++] = tmp;

        tmp = currentPoint - 1;
        //tile has neighbour to the left which we did not handle yet
        if ((lookUpX > 0) && (Maze[tmp] == 0xfu))
            neighbours[neighboursFound++] = tmp;

        tmp = currentPoint - MazeWidth;
        //tile has neighbour the north which we did not handle yet
        if ((lookUpY > 0) && (Maze[tmp] == 0xfu))
            neighbours[neighboursFound++] = tmp;

        tmp = currentPoint + MazeWidth;
        //tile has neighbour the south which we did not handle yet
        if ((lookUpY + 1 < MazeHeight) && (Maze[tmp] == 0xfu))
            neighbours[neighboursFound++] = tmp;

        switch (neighboursFound)
        {
        case 0:
            //a dead end, back to the room this one was entered from
            switch ((Maze[currentPoint] >> PARENT_SHIFT) & 3)
            {
            case PARENT_WEST: currentPoint -= 1; break;
            case PARENT_EAST: currentPoint += 1; break;
            case PARENT_NORTH: currentPoint -= MazeWidth; break;
            default: currentPoint += MazeWidth; break;
            }
            continue;
            break;
        default:
            rnd = (uint16_t)(rand() % neighboursFound);
            break;
        }
        selectedNeighbour = neighbours[rnd];
        tmp = (selectedNeighbour % MazeWidth);
        //tile has neighbour to the east
        if (tmp > lookUpX)
        {
            //remove west wall neighbour
            Maze[selectedNeighbour] &= ~(8);
            Maze[selectedNeighbour] |= PARENT_WEST << PARENT_SHIFT;
            //remove east wall tile
            Maze[currentPoint] &= ~(2);
        }
        else // tile has neighbour to the west
        {
            if (tmp < lookUpX)
            {
                //remove east wall neighbour
                Maze[selectedNeighbour] &= ~(2);
                Maze[selectedNeighbour] |= PARENT_EAST << PARENT_SHIFT;
                //remove west wall tile
                Maze[currentPoint] &= ~(8);
            }
            else // tile has neighbour to the north
            {
                tmp2 = selectedNeighbour / MazeWidth;
                if (tmp2 < lookUpY)
                {
                    //remove south wall neighbour
                    Maze[selectedNeighbour] &= ~(4);
                    Maze[selectedNeighbour] |= PARENT_SOUTH << PARENT_SHIFT;
                    //remove north wall tile
                    Maze[currentPoint] &= ~(1);
                }
                else // tile has neighbour to the south
                {
                    if (tmp2 > lookUpY)
                    {
                        //remove north wall neighbour
                        Maze[selectedNeighbour] &= ~(1);
                        Maze[selectedNeighbour] |= PARENT_NORTH << PARENT_SHIFT;
                        //remove south wall tile
                        Maze[currentPoint] &= ~(4);
                    }
                }
            }
        }

        //set tile to the neighbour   
        currentPoint = selectedNeighbour;
        visitedRooms++;
    }
    //the way back is not needed any more, only the walls are left
    for (uint16_t i = 0; i < MazeHeight * MazeWidth; i++)
        Maze[i] &= 0xfu;
    Maze[0] &= ~(1);
    Maze[(MazeWidth) * (MazeHeight)-1] &= ~(4);
    PlayerPosX = MazeWidth - 1;
    PlayerPosY = MazeHeight - 1;
    xoffset = (((WINDOW_WIDTH / BoxWidth) - (MazeWidth + 2)) * BoxWidth) >> 1;
    yoffset = (((WINDOW_HEIGHT / BoxHeight) - (MazeHeight + 2)) * BoxHeight) >> 1;
}
