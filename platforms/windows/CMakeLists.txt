# Windows build of Mazethingie: the game's own sources with the PLATFORM_SDL platform
# (source/mazethingie_embedded/PlatformSDL.*), LovyanGFX's SDL panel for the window and SDL2.
#
# With MSYS2 (mingw64 packages cmake, ninja or make, gcc and SDL2), from this folder:
#   cmake -S . -B build-windows -G Ninja            (add -DSCREENBUFFER=1 to change)
#   cmake --build build-windows
#   build-windows/mazethingie.exe
# The exe is linked statically, it runs on its own without any dll from MSYS2. It is a Windows
# program without a console window, and what it does not use is left out of it.
# The save file (Mazethingie.sav) is written to the folder the game is started from.

cmake_minimum_required(VERSION 3.16)
project(mazethingie C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# LovyanGFX's lgfx_qrcode.c defines its own bool, which C23 (the newer gcc default) forbids
set(CMAKE_C_STANDARD 11)

# the same LovyanGFX the Arduino build uses
set(LOVYANGFX_DIR "C:/arduino/portable/sketchbook/libraries/LovyanGFX-master" CACHE PATH "LovyanGFX library folder")
if(NOT EXISTS "${LOVYANGFX_DIR}/src/LovyanGFX.hpp")
    message(FATAL_ERROR "LovyanGFX not found in ${LOVYANGFX_DIR}, pass -DLOVYANGFX_DIR=<folder>")
endif()

# where drawing goes, see SCREENBUFFER in Platform.h: 0, 1, 8 or 16. A PC has the memory
# for a full colour buffer, so that is the default here. Change it with -DSCREENBUFFER=<n>
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


# 1 = frames are held to the game's frame rate, 0 = they run as fast as they go, see FPSLOCK
# in Defines.h. Change it with -DFPSLOCK=<n>
set(FPSLOCK 1 CACHE STRING "FPSLOCK setting: 1 or 0")
set_property(CACHE FPSLOCK PROPERTY STRINGS 1 0)
if(NOT FPSLOCK MATCHES "^(0|1)$")
    message(FATAL_ERROR "FPSLOCK has to be 0 or 1, not ${FPSLOCK}")
endif()

# 1 = the debug header is always shown, 0 = Up + Down shows and hides it, see FORCEDEBUG in
# Defines.h. Change it with -DFORCEDEBUG=<n>
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# how much bigger than the game's own size the window opens, the window can be resized
# after, see WINDOW_SCALE in PlatformSDL.cpp
set(WINDOW_SCALE 4 CACHE STRING "WINDOW_SCALE setting: how many times the game's size the window opens")
set_property(CACHE FORCEDEBUG PROPERTY STRINGS 0 1)
if(NOT FORCEDEBUG MATCHES "^(0|1)$")
    message(FATAL_ERROR "FORCEDEBUG has to be 0 or 1, not ${FORCEDEBUG}")
endif()

set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

# the parts of LovyanGFX an SDL build needs, as in its examples_for_PC/CMake_SDL
file(GLOB LGFX_SOURCES CONFIGURE_DEPENDS
    "${LOVYANGFX_DIR}/src/lgfx/Fonts/efont/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/Fonts/IPA/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/utility/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/v1/*.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/misc/*.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_Device.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_FrameBufferBase.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp")

# the static SDL2 library, so the exe needs no SDL2.dll next to it
find_package(SDL2 REQUIRED COMPONENTS SDL2-static)

# WIN32: a Windows (GUI) program, started it opens no console window next to the game's own
add_executable(mazethingie WIN32 ${GAME_SOURCES} ${LGFX_SOURCES})
# The switches above are named one by one because they are validated; this carries
# anything else a build asks for, so a switch with no option of its own can still be
# set. See --define in tools/build_releases.py
set(EXTRA_DEFINES "" CACHE STRING "extra compile definitions, NAME=VALUE, semicolon separated")

target_compile_definitions(mazethingie PRIVATE PLATFORM_SDL LGFX_SDL SCREENBUFFER=${SCREENBUFFER} DITHERING=${DITHERING} FPSLOCK=${FPSLOCK} FORCEDEBUG=${FORCEDEBUG} WINDOW_SCALE=${WINDOW_SCALE} ${EXTRA_DEFINES})
target_include_directories(mazethingie PRIVATE "${GAME_DIR}" "${LOVYANGFX_DIR}/src")
target_link_libraries(mazethingie PRIVATE SDL2::SDL2-static)

if(MINGW)
    # a fully static exe: libgcc, libstdc++ and winpthread are linked in as well, it only
    # needs the dlls every Windows has
    target_link_options(mazethingie PRIVATE -static)
    # LovyanGFX's Chinese, Japanese and Korean fonts are over 40 MB and the game uses none of them:
    # every function and piece of data gets a section of its own, and the linker leaves out the
    # sections nothing uses. Except in a Debug build the symbols are left out as well
    target_compile_options(mazethingie PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(mazethingie PRIVATE -Wl,--gc-sections $<$<NOT:$<CONFIG:Debug>>:-s>)
endif()
