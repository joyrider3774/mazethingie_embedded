# PlayStation build of Mazethingie: the game's own sources with the PLATFORM_PSX platform
# (source/mazethingie_embedded/PlatformPSX.*) and PSn00bSDK (C:/psn00bsdk by default, or the
# PSN00BSDK_PREFIX environment variable).
#
# With MSYS2's cmake and ninja, from this folder:
#   cmake -S . -B build -G Ninja -DPSN00BSDK_PREFIX=C:/psn00bsdk
#   cmake --build build
# gives build/Mazethingie.exe, a PS-EXE for an emulator or a console that runs unsigned code, and
# build/Mazethingie.bin + .cue, a disc image.

cmake_minimum_required(VERSION 3.21)

# PSn00bSDK brings its own toolchain file, which needs its prefix before the project is set up
if(NOT PSN00BSDK_PREFIX)
    if(DEFINED ENV{PSN00BSDK_PREFIX})
        file(TO_CMAKE_PATH "$ENV{PSN00BSDK_PREFIX}" PSN00BSDK_PREFIX)
    else()
        set(PSN00BSDK_PREFIX "C:/psn00bsdk")
    endif()
endif()
set(PSN00BSDK_PREFIX "${PSN00BSDK_PREFIX}" CACHE PATH "folder PSn00bSDK is installed in")
set(PSN00BSDK_LIBS "${PSN00BSDK_PREFIX}/lib/libpsn00b" CACHE PATH "PSn00bSDK's libraries")
set(PSN00BSDK_TC "${PSN00BSDK_PREFIX}" CACHE PATH "the GCC toolchain PSn00bSDK builds with")
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${PSN00BSDK_LIBS}/cmake/sdk.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(mazethingie_psx C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformPSX.h: 1, 8 or 16, the screen is never drawn to
# straight
set(SCREENBUFFER 16 CACHE STRING "SCREENBUFFER setting: 1, 8 or 16")

# 1 = the colours of an image are spread over the ones the buffer can hold instead of every colour
# becoming the nearest one there is, 0 = they are not. An 8 bpp and a 1 bpp buffer have something
# to gain from it, see DITHERING in defines.h
set(DITHERING 0 CACHE STRING "DITHERING setting: 0 or 1")
if(NOT DITHERING MATCHES "^(0|1)$")
    message(FATAL_ERROR "DITHERING has to be 0 or 1, not ${DITHERING}")
endif()

# 1 = scaled to 240x240, 0 = 1:1 in the middle, see SCALESCREEN in PlatformPSX.h
set(SCALESCREEN 1 CACHE STRING "SCALESCREEN setting: 1 or 0")

# 1 = the debug header is always shown, 0 = the debug combination shows and hides it, see FORCEDEBUG
# in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\.cpp$")

psn00bsdk_add_executable(${GAME_NAME} GPREL ${GAME_SOURCES})
target_compile_definitions(${GAME_NAME} PRIVATE PLATFORM_PSX
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} SCALESCREEN=${SCALESCREEN}
    FORCEDEBUG=${FORCEDEBUG})
# compat/: what PSn00bSDK's C library does not have but the game's own code includes
target_include_directories(${GAME_NAME} PRIVATE "${GAME_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/compat")
