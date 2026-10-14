# Gamebuino AKA build of Mazethingie: the game's own sources with the PLATFORM_AKA platform
# (source/rubido_embedded/PlatformAka.*), ESP-IDF 5.5 and the Gamebuino AKA library.
#
# The AKA is an ESP32-S3, so this is an ESP-IDF project and not a plain CMake one: it is built
# with idf.py, not with cmake directly. From this folder, with ESP-IDF exported:
#   idf.py build
# gives build/Mazethingie.bin, to put on the card the AKA's loader reads.
#
# The AKA library is not part of this repository. Point AKA_LIB_DIR at a checkout of
# https://github.com/jmp42/Gamebuino_AKA_lib (its components/gamebuino is what is used), either
# with -DAKA_LIB_DIR=<folder> or with the AKA_LIB_DIR environment variable.
#
# Do not run "idf.py set-target": it writes a fresh sdkconfig from the defaults and loses the
# PSRAM settings this device needs, and the link then fails with "dram0_0_seg overflowed".
# sdkconfig.defaults here holds them, a plain "idf.py build" picks it up.

cmake_minimum_required(VERSION 3.16)

set(GAME_NAME Mazethingie)

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where the Gamebuino AKA library is checked out
if(NOT AKA_LIB_DIR AND DEFINED ENV{AKA_LIB_DIR})
    set(AKA_LIB_DIR "$ENV{AKA_LIB_DIR}")
endif()
if(NOT AKA_LIB_DIR)
    set(AKA_LIB_DIR "C:/github/Gamebuino_AKA_lib")
endif()
set(AKA_LIB_DIR "${AKA_LIB_DIR}" CACHE PATH "a checkout of Gamebuino_AKA_lib")

if(NOT EXISTS "${AKA_LIB_DIR}/components/gamebuino/include_lib/gamebuino.h")
    message(FATAL_ERROR
        "the Gamebuino AKA library was not found in ${AKA_LIB_DIR}.\n"
        "Clone https://github.com/jmp42/Gamebuino_AKA_lib and pass -DAKA_LIB_DIR=<folder>, "
        "or set the AKA_LIB_DIR environment variable.")
endif()

# the library is used where it lies, so a checkout of it is never copied or modified
set(EXTRA_COMPONENT_DIRS "${AKA_LIB_DIR}/components")

# where drawing goes, see SCREENBUFFER in PlatformAka.h: 0 (straight to the display), 1, 8 or 16.
# Only a buffered frame can be scaled up to 240x240
set(SCREENBUFFER 16 CACHE STRING "SCREENBUFFER setting: 0, 1, 8 or 16")
set_property(CACHE SCREENBUFFER PROPERTY STRINGS 0 1 8 16)
if(NOT SCREENBUFFER MATCHES "^(0|1|8|16)$")
    message(FATAL_ERROR "SCREENBUFFER has to be 0, 1, 8 or 16, not ${SCREENBUFFER}")
endif()
# 1 = the colours of an image are spread over the ones the buffer can hold instead of every colour
# becoming the nearest one there is, 0 = they are not. An 8 bpp and a 1 bpp buffer have something
# to gain from it, see DITHERING in defines.h
set(DITHERING 0 CACHE STRING "DITHERING setting: 0 or 1")
if(NOT DITHERING MATCHES "^(0|1)$")
    message(FATAL_ERROR "DITHERING has to be 0 or 1, not ${DITHERING}")
endif()


# 1 scales the frame to 240x240 in the middle of the display, 0 shows it 1:1
set(SCALESCREEN 1 CACHE STRING "1 to scale the frame to 240x240, 0 for 1:1")
# 1 shows the debug header
set(FORCEDEBUG 0 CACHE STRING "1 to show the debug header")
# the backlight, 0 to 100 percent
set(BACKLIGHT 80 CACHE STRING "backlight brightness in percent, 0 to 100")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${GAME_NAME})

# The library's gb_graphics.h includes "gb_ll_LCD.h", but the file it means is
# include_ll/gb_ll_lcd.h. Windows and macOS do not notice the difference; Linux does, and the
# build stops at "gb_ll_LCD.h: No such file or directory" - which is what the CI runner hits.
# Rather than patch a checkout of somebody else's library, a header of the name it asks for is
# generated here and put on every component's include path, holding nothing but an include of
# the real one by absolute path. Absolute matters: a plain #include "gb_ll_lcd.h" would find
# this very file again where the filesystem ignores case.
set(AKA_CASE_COMPAT "${CMAKE_BINARY_DIR}/aka_case_compat")
file(WRITE "${AKA_CASE_COMPAT}/gb_ll_LCD.h"
     "#pragma once\n#include \"${AKA_LIB_DIR}/components/gamebuino/include_ll/gb_ll_lcd.h\"\n")
idf_build_set_property(COMPILE_OPTIONS "-I${AKA_CASE_COMPAT}" APPEND)

# The library's fifo helpers in gb_ll_audio.c are "inline" without "static": a C99 inline
# definition emits no symbol of its own, so it only links where the compiler inlined every call.
# At -Os it does not inline them into the IRAM audio callback and the link fails with
# "undefined reference to fifo_used". The library is used exactly as it is shipped, so the
# component is built at -O2, where they are inlined, instead of being patched here.
idf_component_get_property(aka_gamebuino_lib gamebuino COMPONENT_LIB)
target_compile_options(${aka_gamebuino_lib} PRIVATE -O2)
