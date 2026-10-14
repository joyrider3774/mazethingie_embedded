# PlayStation Vita build of Mazethingie: the game's own sources with the PLATFORM_VITA platform
# (source/mazethingie_embedded/PlatformVita.*) and VitaSDK (C:/psvita_dev by default).
#
# With VITASDK set and cmake on the path, from this folder:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
#   cmake --build build
# or just run build.bat, which sets that up. It gives build/Mazethingie.vpk, which is installed with VitaShell
# on a Vita with homebrew enabled, or opened in the Vita3K emulator. The high scores are saved in
# ux0:data/Mazethingie/Mazethingie.sav.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{VITASDK} AND NOT VITASDK)
    set(ENV{VITASDK} "C:/psvita_dev")
endif()
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake")
endif()

project(mazethingie_vita C CXX ASM)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)
# what LiveArea shows under the bubble, and the nine character id the Vita knows the game by
set(VITA_APP_NAME "Mazethingie")
set(VITA_TITLEID "JOYR00009")
set(VITA_VERSION "01.00")

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformVita.h: 0 (straight to the frame), 1, 8 or 16
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


# 1 = the frame is blown up to 512x512, 0 = 1:1 in the middle, see SCALESCREEN in PlatformVita.h
set(SCALESCREEN 1 CACHE STRING "SCALESCREEN setting: 1 or 0")

# how loud tones play, 0 (silent) to 100, see SOUNDVOLUME in PlatformVita.h
set(SOUNDVOLUME 60 CACHE STRING "SOUNDVOLUME setting: 0 to 100")

# 1 = the debug header is always shown, 0 = Up + Down shows and hides it, see FORCEDEBUG in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

add_executable(${GAME_NAME}.elf ${GAME_SOURCES})
target_compile_definitions(${GAME_NAME}.elf PRIVATE PLATFORM_VITA VITA_APP_NAME="${VITA_APP_NAME}"
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} SCALESCREEN=${SCALESCREEN} SOUNDVOLUME=${SOUNDVOLUME}
    FORCEDEBUG=${FORCEDEBUG})
target_include_directories(${GAME_NAME}.elf PRIVATE "${GAME_DIR}")
target_compile_options(${GAME_NAME}.elf PRIVATE -O2 -fno-strict-aliasing
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
# the Vita's own modules: the display, the buttons, the sound port and the kernel calls
target_link_libraries(${GAME_NAME}.elf SceDisplay_stub SceCtrl_stub SceAudio_stub SceLibKernel_stub
    SceSysmodule_stub ScePower_stub SceIofilemgr_stub)

# the signed program the Vita starts, and the VPK that installs it
vita_create_self(${GAME_NAME}.self ${GAME_NAME}.elf UNSAFE)
vita_create_vpk(${GAME_NAME}.vpk ${VITA_TITLEID} ${GAME_NAME}.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME})
