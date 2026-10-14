#ifndef PLATFORM_DEVICE_H
#define PLATFORM_DEVICE_H

//Picks the device to build for and includes its header. defines.h includes this first, so
//the device's settings (SCREENBUFFER) are known wherever the game's defines use them.
//What the device header supplies is listed under "The device" in Platform.h.

//The Windows build (CMakeLists.txt) defines PLATFORM_SDL, the browser build (web/CMakeLists.txt)
//PLATFORM_WEB, the MS-DOS build (dos/CMakeLists.txt) PLATFORM_DOS, the Playdate build (playdate/CMakeLists.txt)
//PLATFORM_PLAYDATE, the libretro core (libretro/CMakeLists.txt) PLATFORM_LIBRETRO and the Game Boy Advance
//build (gba/CMakeLists.txt) PLATFORM_GBA and the Nintendo DS build (nds/CMakeLists.txt) PLATFORM_NDS and the
//Nintendo 3DS build (3ds/CMakeLists.txt) PLATFORM_3DS and the PlayStation build
//(psx/CMakeLists.txt) PLATFORM_PSX and the Nintendo 64 build (n64/CMakeLists.txt) PLATFORM_N64
//and the Gamebuino AKA build (aka/CMakeLists.txt) PLATFORM_AKA
//and the PlayStation Portable build (psp/CMakeLists.txt)
//PLATFORM_PSP and the PlayStation Vita build (vita/CMakeLists.txt) PLATFORM_VITA, an Arduino build is picked from the defines the board package
//of the board selected in the IDE sets. A build can still define one of the PLATFORM_ names itself,
//then nothing is detected
#if !defined(PLATFORM_SDL) && !defined(PLATFORM_WEB) && !defined(PLATFORM_DOS) && !defined(PLATFORM_PLAYDATE) && !defined(PLATFORM_LIBRETRO) && !defined(PLATFORM_GBA) && !defined(PLATFORM_NDS) && !defined(PLATFORM_3DS) && !defined(PLATFORM_PSX) && !defined(PLATFORM_N64) && !defined(PLATFORM_PSP) && !defined(PLATFORM_VITA) && !defined(PLATFORM_GAMEBUINO) && !defined(PLATFORM_ESPBOY) && !defined(PLATFORM_PYBADGE) && !defined(PLATFORM_PICOSYSTEM) && !defined(PLATFORM_EXPLORER) && !defined(PLATFORM_TUFTY) && !defined(PLATFORM_THUMBY) && !defined(PLATFORM_AKA) && !defined(PLATFORM_CHGAME)
  #if defined(ADAFRUIT_PYBADGE_M4_EXPRESS) || defined(ADAFRUIT_PYGAMER_M4_EXPRESS)
    //Adafruit's board package, the PyGamer builds the same code with its joystick as the d-pad
    #define PLATFORM_PYBADGE 1
  #elif defined(ESP8266)
    //the ESPboy is an ESP8266 module, any ESP8266 board from the esp8266 package builds for it
    #define PLATFORM_ESPBOY 1
  #elif defined(__SAMD21G18A__) && defined(ARDUINO_SAMD_ZERO)
    //The Gamebuino META's board package gives it the Arduino Zero's chip and board name, so an
    //Arduino Zero (and the Adafruit M0 boards that also call themselves SAMD_ZERO) builds as a
    //META too. The MKR boards have a board name of their own and do not
    #define PLATFORM_GAMEBUINO 1
  #elif defined(ARDUINO_ARCH_CH32)
    //the CHGame's board package (github.com/bateske/CHGame) has the one board, and it is
    //the only package here that builds for a CH32
    #define PLATFORM_CHGAME 1
  #elif defined(ARDUINO_PIMORONI_EXPLORER)
    //the arduino-pico core's Pimoroni Explorer board, checked before any other board of that core
    #define PLATFORM_EXPLORER 1
  #elif defined(ARDUINO_GENERIC_RP2350)
    //arduino-pico has a board of its own for neither the Tufty 2350 nor the Thumby Color, both build
    //as its Generic RP2350 board and its Chip Variant tells them apart: the Tufty is an RP2350B
    //(PSRAM CS GPIO 8, 8MB PSRAM, 16MB flash), the Thumby Color an RP2350A (16MB flash, no PSRAM).
    //The core passes that choice as __PICO_RP2350A, the board's own PICO_RP2350A is not set yet here
    #if defined(__PICO_RP2350A) && __PICO_RP2350A
      #define PLATFORM_THUMBY 1
    #else
      #define PLATFORM_TUFTY 1
    #endif
  #elif defined(ARDUINO_ARCH_RP2040)
    //The arduino-pico core has no PicoSystem board of its own, the PicoSystem builds as its Generic
    //RP2040 board (flash size 16 MB). Any other board of that core builds for it
    #define PLATFORM_PICOSYSTEM 1
  #else
    #error "unsupported board: pick an ESP8266 board for the ESPboy (LOLIN(WEMOS) D1 mini), the Gamebuino META, the Adafruit PyBadge or PyGamer M4 Express, Generic RP2040 for the PicoSystem, Pimoroni Explorer, or Generic RP2350 for the Tufty 2350 (chip variant RP2350B) and the Thumby Color (chip variant RP2350A), or CHGame"
  #endif
#endif

#if defined(PLATFORM_ESPBOY)
#include "PlatformESPboy.h"
#elif defined(PLATFORM_GAMEBUINO)
#include "PlatformGamebuino.h"
#elif defined(PLATFORM_PYBADGE)
#include "PlatformPyBadge.h"
#elif defined(PLATFORM_PICOSYSTEM)
#include "PlatformPicoSystem.h"
#elif defined(PLATFORM_EXPLORER)
#include "PlatformExplorer.h"
#elif defined(PLATFORM_TUFTY)
#include "PlatformTufty.h"
#elif defined(PLATFORM_THUMBY)
#include "PlatformThumby.h"
#elif defined(PLATFORM_CHGAME)
#include "PlatformCHGame.h"
#elif defined(PLATFORM_AKA)
#include "PlatformAka.h"
#elif defined(PLATFORM_SDL)
#include "PlatformSDL.h"
#elif defined(PLATFORM_WEB)
#include "PlatformWeb.h"
#elif defined(PLATFORM_DOS)
#include "PlatformDOS.h"
#elif defined(PLATFORM_PLAYDATE)
#include "PlatformPlaydate.h"
#elif defined(PLATFORM_LIBRETRO)
#include "PlatformLibretro.h"
#elif defined(PLATFORM_GBA)
#include "PlatformGBA.h"
#elif defined(PLATFORM_NDS)
#include "PlatformNDS.h"
#elif defined(PLATFORM_3DS)
#include "Platform3DS.h"
#elif defined(PLATFORM_PSX)
#include "PlatformPSX.h"
#elif defined(PLATFORM_N64)
#include "PlatformN64.h"
#elif defined(PLATFORM_PSP)
#include "PlatformPSP.h"
#elif defined(PLATFORM_VITA)
#include "PlatformVita.h"
#endif

#endif
