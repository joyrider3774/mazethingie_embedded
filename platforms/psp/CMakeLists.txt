# PlayStation Portable build of Mazethingie: the game's own sources with the PLATFORM_PSP platform
# (source/mazethingie_embedded/PlatformPSP.*) and the pspdev toolchain (C:/psp_dev by default).
#
# pspdev only ships toolchains for Linux and macOS, so on Windows this is built from WSL. From this
# folder:
#   wsl bash build.sh
# or, with PSPDEV set and its bin folder on the path:
#   psp-cmake -S . -B build
#   cmake --build build
# gives build/EBOOT.PBP. Copy it into ms0:/PSP/GAME/Mazethingie/ on the memory stick, or open it in PPSSPP.
# The high scores are saved next to it in Mazethingie.sav.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{PSPDEV})
    message(FATAL_ERROR "PSPDEV is not set: point it at the pspdev toolchain (C:/psp_dev) and use psp-cmake")
endif()
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/pspdev.cmake")
endif()
# No build type on purpose: the optimisation is set below, and pspdev's create_pbp_file strips the
# program in a Release build before psp-fixup-imports has read it, which then fails with
# "no nid section found"

project(mazethingie_psp C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)
# what the PSP's menu shows
set(PSP_TITLE "Mazethingie")

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformPSP.h: 0 (straight to the frame), 1, 8 or 16
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


# 1 = the frame is doubled to 256x256, 0 = 1:1 in the middle, see SCALESCREEN in PlatformPSP.h
set(SCALESCREEN 1 CACHE STRING "SCALESCREEN setting: 1 or 0")

# how loud tones play, 0 (silent) to 100, see SOUNDVOLUME in PlatformPSP.h
set(SOUNDVOLUME 60 CACHE STRING "SOUNDVOLUME setting: 0 to 100")

# 1 = the debug header is always shown, 0 = Up + Down shows and hides it, see FORCEDEBUG in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

add_executable(${GAME_NAME} ${GAME_SOURCES})
target_compile_definitions(${GAME_NAME} PRIVATE PLATFORM_PSP PSP_APP_NAME="${GAME_NAME}"
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} SCALESCREEN=${SCALESCREEN} SOUNDVOLUME=${SOUNDVOLUME}
    FORCEDEBUG=${FORCEDEBUG})
target_include_directories(${GAME_NAME} PRIVATE "${GAME_DIR}")
target_compile_options(${GAME_NAME} PRIVATE -O2 -G0
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
# No --gc-sections: it throws away the sections naming the PSP functions the program imports, and
# psp-fixup-imports then stops with "no nid section found". The PSP has 32 MB, the size is no worry
# pspaudiolib runs the sound in a thread of its own, the rest is the PSP's own modules
target_link_libraries(${GAME_NAME} PRIVATE pspaudiolib pspaudio pspgu pspge pspdisplay pspctrl pspdebug)

# the EBOOT.PBP the PSP's menu starts
create_pbp_file(
    TARGET ${GAME_NAME}
    TITLE "${PSP_TITLE}"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}"
)
