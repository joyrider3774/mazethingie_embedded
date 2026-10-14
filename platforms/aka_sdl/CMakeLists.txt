# Gamebuino AKA build of Mazethingie, on a PC: the same sources platforms/aka builds for the device
# (source/mazethingie_embedded/PlatformAka.*, PLATFORM_AKA), with the AKA library's SDL backend
# underneath instead of ESP-IDF.
#
# It is the device's own gb_graphics, gb_core and sound mixer running on the game's own frames;
# only the panel, the codec, the key expander, the ADC and the SD card are SDL. So this is the
# way to try a change to PlatformAka.cpp without flashing, and it is not a substitute for
# flashing: a PC says nothing about the AKA's speed, its PSRAM or its link.
#
# With MSYS2 (mingw64 packages cmake, ninja or make, gcc and SDL2), from this folder:
#   cmake -S . -B build_aka_sdl -G Ninja            (add -DSCREENBUFFER=1 for another mode)
#   cmake --build build_aka_sdl
#   build_aka_sdl/mazethingie_aka.exe
# The exe is linked statically, it runs on its own without any dll from MSYS2. It keeps its
# console: the backend prints where the card folder is, and how many frames and how much
# starved sound a run ended with.
#
# Two checkouts are needed, neither of them part of this repository:
#   AKA_LIB_DIR  https://github.com/jmp42/Gamebuino_AKA_lib   the device's own library
#   AKA_SDL_DIR  the SDL backend for it, which compiles that library's gb_lib as it stands
# Both may come from the environment, and both have a default that matches where they usually
# sit. The save file goes to <exe folder>/sdcard/Mazethingie/, which is what MOUNT_POINT becomes.
#
# The controls: arrow keys or W A S D, Z X C V for A B C D, Q and E for L1 and R1, Escape for
# RUN, which powers the device down and so ends the run.

cmake_minimum_required(VERSION 3.16)
project(mazethingie_aka C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# the AKA library's SDL backend, which brings the library itself in with it
if(NOT AKA_SDL_DIR AND DEFINED ENV{AKA_SDL_DIR})
    set(AKA_SDL_DIR "$ENV{AKA_SDL_DIR}")
endif()
if(NOT AKA_SDL_DIR)
    set(AKA_SDL_DIR "C:/github/Gamebuino_AKA_lib_sdl")
endif()
set(AKA_SDL_DIR "${AKA_SDL_DIR}" CACHE PATH "a checkout of the AKA library's SDL backend")

if(NOT EXISTS "${AKA_SDL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "the AKA library's SDL backend was not found in ${AKA_SDL_DIR}.\n"
        "Pass -DAKA_SDL_DIR=<folder>, or set the AKA_SDL_DIR environment variable. It in turn "
        "needs a checkout of https://github.com/jmp42/Gamebuino_AKA_lib in AKA_LIB_DIR.")
endif()

# where drawing goes, see SCREENBUFFER in PlatformAka.h: 0 (straight to gb_graphics), 1, 8 or
# 16. Only a buffered frame can be scaled up to 240x240, and a PC has the memory for a full
# colour one, so this matches the device build's default
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


# 1 scales the frame to 240x240 in the middle of the panel, 0 shows it 1:1
set(SCALESCREEN 1 CACHE STRING "1 to scale the frame to 240x240, 0 for 1:1")
set_property(CACHE SCALESCREEN PROPERTY STRINGS 0 1)
if(NOT SCALESCREEN MATCHES "^(0|1)$")
    message(FATAL_ERROR "SCALESCREEN has to be 0 or 1, not ${SCALESCREEN}")
endif()

# 1 = the debug header is always shown, 0 = Up + Down shows and hides it
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")
set_property(CACHE FORCEDEBUG PROPERTY STRINGS 0 1)
if(NOT FORCEDEBUG MATCHES "^(0|1)$")
    message(FATAL_ERROR "FORCEDEBUG has to be 0 or 1, not ${FORCEDEBUG}")
endif()

# the backlight, 0 to 100 percent. The backend dims the window by it, as the device dims the panel
set(BACKLIGHT 80 CACHE STRING "backlight brightness in percent, 0 to 100")

# the backend links SDL2 and the compiler runtime statically by default, so the exe runs on
# its own, as the Windows build's does. -DGB_SDL_STATIC_SDL2=OFF goes back to the shared ones
add_subdirectory("${AKA_SDL_DIR}" gamebuino_sdl)

set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded")

# the same set platforms/aka builds: the game's own folder, minus the platform sources of other
# devices. Each of those compiles to nothing without its own PLATFORM_ define, but the ones
# that include a library this build has no headers for cannot be handed to the compiler
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "Platform(ESPboy|Gamebuino|PyBadge|PicoSystem|Explorer|Tufty|Thumby|SDL|Web|DOS|Playdate|Libretro|GBA|NDS|3DS|PSX|N64|PSP|Vita)\\.cpp$")

# no WIN32 here: the backend's console output is half the point of running the game this way
add_executable(mazethingie_aka ${GAME_SOURCES})
target_compile_definitions(mazethingie_aka PRIVATE
    PLATFORM_AKA
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING}
    SCALESCREEN=${SCALESCREEN}
    FORCEDEBUG=${FORCEDEBUG}
    BACKLIGHT=${BACKLIGHT})
target_include_directories(mazethingie_aka PRIVATE "${GAME_DIR}")
target_link_libraries(mazethingie_aka PRIVATE gamebuino)

# the game is not written to a desktop compiler's warnings, and the device build turns the same
# ones off
target_compile_options(mazethingie_aka PRIVATE
    -Wno-narrowing -Wno-unused-variable -Wno-unused-function -Wno-sign-compare
    -Wno-char-subscripts -Wno-missing-field-initializers)

if(MINGW)
    # -static comes from the backend; what is left is dropping the parts nothing uses
    target_compile_options(mazethingie_aka PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(mazethingie_aka PRIVATE -Wl,--gc-sections $<$<NOT:$<CONFIG:Debug>>:-s>)
endif()
