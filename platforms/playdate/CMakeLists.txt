# Playdate build of Mazethingie: the game's own sources with the PLATFORM_PLAYDATE platform
# (source/mazethingie_embedded/PlatformPlaydate.*) and Panic's Playdate C SDK. Needs PLAYDATE_SDK_PATH set
# to the SDK folder.
#
# From this folder, the device (the ARM gcc of the SDK's instructions on the PATH):
#   cmake -S . -B build_device -G Ninja -DCMAKE_TOOLCHAIN_FILE=%PLAYDATE_SDK_PATH%/C_API/buildsupport/arm.cmake
#   cmake --build build_device
# and the simulator (MSYS2's mingw64 gcc):
#   cmake -S . -B build_simulator -G Ninja
#   cmake --build build_simulator
# Each build puts its pdex.elf or pdex.dll into Source/ and runs pdc on it, so the pdx made last holds
# both and runs on the device and in the simulator. tools/build_releases.py does both in a copy of
# this folder.

cmake_minimum_required(VERSION 3.19)

set(ENVSDK $ENV{PLAYDATE_SDK_PATH})
if(NOT "${ENVSDK}" STREQUAL "")
    file(TO_CMAKE_PATH ${ENVSDK} SDK)
endif()
if(NOT EXISTS "${SDK}/C_API/pd_api.h")
    message(FATAL_ERROR "Playdate SDK not found, set PLAYDATE_SDK_PATH to its folder")
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(PLAYDATE_GAME_NAME Mazethingie)
set(PLAYDATE_GAME_DEVICE Mazethingie_DEVICE)

project(${PLAYDATE_GAME_NAME} C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformPlaydate.h: 1, or 8 or 16 dithered
set(SCREENBUFFER 1 CACHE STRING "SCREENBUFFER setting: 1, 8 or 16")
set_property(CACHE SCREENBUFFER PROPERTY STRINGS 1 8 16)
if(NOT SCREENBUFFER MATCHES "^(1|8|16)$")
    message(FATAL_ERROR "SCREENBUFFER has to be 1, 8 or 16, not ${SCREENBUFFER}")
endif()

# 1 = the colours of an image are spread over the ones the buffer can hold instead of every colour
# becoming the nearest one there is, 0 = they are not. An 8 bpp and a 1 bpp buffer have something
# to gain from it, see DITHERING in defines.h
set(DITHERING 0 CACHE STRING "DITHERING setting: 0 or 1")
if(NOT DITHERING MATCHES "^(0|1)$")
    message(FATAL_ERROR "DITHERING has to be 0 or 1, not ${DITHERING}")
endif()

# 1 = the debug header is always shown, 0 = Up + Down shows and hides it, see FORCEDEBUG in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

if(TOOLCHAIN STREQUAL "armgcc")
    add_executable(${PLAYDATE_GAME_DEVICE} ${GAME_SOURCES})
    set(GAME_TARGET ${PLAYDATE_GAME_DEVICE})
    # no run time type information and no guards around static locals, the device has neither
    target_compile_options(${GAME_TARGET} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-threadsafe-statics>)
    # the C++ runtime refers to newlib's system calls (_write, _exit, ...), the Playdate has none of
    # them: nosys gives them stubs that do nothing
    target_link_options(${GAME_TARGET} PRIVATE --specs=nosys.specs)
    # pdc builds the image it relocates by pasting the elf's LOAD segments together, which drops the gap
    # the linker leaves between the end of .text and the 8 byte aligned start of .data. that gap becomes
    # a segment boundary only when .text ends just under a 4k page, and then the last relocation of
    # .data sits past the end of pdc's image: "ERROR: relocation offset outside of binary image". -n
    # keeps .text and .data in one LOAD segment, so there is never a boundary to lose them at
    target_link_options(${GAME_TARGET} PRIVATE -Wl,-n)
else()
    add_library(${PLAYDATE_GAME_NAME} SHARED ${GAME_SOURCES})
    set(GAME_TARGET ${PLAYDATE_GAME_NAME})
    if(MINGW)
        # libgcc and libstdc++ linked in, the simulator loads the dll without MSYS2's dlls
        target_link_options(${GAME_TARGET} PRIVATE -static)
    endif()
endif()

target_compile_definitions(${GAME_TARGET} PRIVATE PLATFORM_PLAYDATE SCREENBUFFER=${SCREENBUFFER} DITHERING=${DITHERING} FORCEDEBUG=${FORCEDEBUG})
target_include_directories(${GAME_TARGET} PRIVATE "${GAME_DIR}")

include(${SDK}/C_API/buildsupport/playdate_game.cmake)
