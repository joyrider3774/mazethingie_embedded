#ifndef defines_h
#define defines_h

//the device comes first: the display library and SCREENBUFFER are device settings, see
//PlatformESPboy.h / PlatformSDL.h
#include "PlatformDevice.h"

#define FRAMERATE 30
//1 = every frame waits until 1/FRAMERATE of a second has passed, 0 = a frame starts as soon
//as the last one is done, to see how fast the game can go. Movement, animation, input
//repeat and music all count frames, so without the lock they run faster as well.
//A build can set it itself
#ifndef FPSLOCK
#define FPSLOCK 1
#endif
//1 = the debug header (frame rate, free heap and stack) is always shown, Up + Down does not
//hide it. 0 = it starts hidden and Up + Down shows and hides it. A build can set it itself
#ifndef FORCEDEBUG
#define FORCEDEBUG 0
#endif
//1 = the colours of an image are spread over the ones the buffer can hold, so that a shade it
//has no colour for is a pattern of the two it does instead of the nearer of them. 0 = every
//colour becomes the nearest one there is, which shows as bands across anything that shades.
//An 8 bpp buffer is RGB332 and drops 2 bits of red, 3 of green and 3 of blue, and a 1 bpp buffer
//keeps only black and white, so both have something to spread. A 16 bpp buffer holds every colour
//of the image as it is and is left alone. A build can set this itself, see DitherSpread in
//Platform.h
#ifndef DITHERING
#define DITHERING 0
#endif

#define WINDOW_WIDTH 128
#define WINDOW_HEIGHT 128
#define HALFWINDOWWIDTH 64
#define HALFWINDOWHEIGHT 64

#define GSGame 0
#define GSTitleScreen 1

#define BoxWidth 4
#define BoxHeight 4

#define MaxMazeWidth ((int)((WINDOW_WIDTH / BoxWidth) -2))
#define MaxMazeHeight ((int)((WINDOW_HEIGHT / BoxHeight) -2))
#define MaxMazeSize (MaxMazeWidth * MaxMazeHeight)

#define PlayerSpeedTickVal 5
#define MenuSpeedTickVal 4

#endif
