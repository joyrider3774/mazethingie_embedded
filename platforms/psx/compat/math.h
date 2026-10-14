#ifndef PSX_COMPAT_MATH_H
#define PSX_COMPAT_MATH_H

//PSn00bSDK's C library has no math.h. The game's own code includes it but the functions it would
//bring are only used by the platforms with a backlight to set (the Thumby Color and the Tufty),
//which are not part of a PlayStation build. What the console does need of it is here, and anything
//else the game starts using will say so at the link.

//the few constants a game may reach for
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

static inline float fabsf_compat(float value) { return (value < 0.0f) ? -value : value; }
#ifndef fabsf
#define fabsf fabsf_compat
#endif

#endif
