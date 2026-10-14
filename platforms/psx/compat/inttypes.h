#ifndef PSX_COMPAT_INTTYPES_H
#define PSX_COMPAT_INTTYPES_H

//PSn00bSDK's C library has no inttypes.h, and the game's logging uses its PRI macros. The console
//is 32 bit with 32 bit longs, so these are what the compiler's own inttypes.h would define here.

#include <stdint.h>

#define PRId8    "d"
#define PRIi8    "i"
#define PRIu8    "u"
#define PRIx8    "x"
#define PRId16   "d"
#define PRIi16   "i"
#define PRIu16   "u"
#define PRIx16   "x"
#define PRId32   "ld"
#define PRIi32   "li"
#define PRIu32   "lu"
#define PRIx32   "lx"
#define PRIX32   "lX"
#define PRId64   "lld"
#define PRIi64   "lli"
#define PRIu64   "llu"
#define PRIx64   "llx"

#endif
