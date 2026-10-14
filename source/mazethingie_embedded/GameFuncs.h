#ifndef game_h
#define game_h

#include <stdbool.h>
//for PLATFORM_FAST_CODE, which marks the calls that run once for every pixel
#include "Platform.h"
void tftPrint(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size);
uint8_t MaxLineLen(const char* Text);

#endif
