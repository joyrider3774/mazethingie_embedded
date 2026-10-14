# Browser build of Mazethingie: the game's own sources with the PLATFORM_WEB platform
# (source/mazethingie_embedded/PlatformWeb.*), built to WebAssembly with Emscripten and drawn into a
# canvas through the SDL2 that Emscripten brings itself (toolchain.cmake, C:/github/emsdk by
# default, or the EMSDK environment variable).
#
# From this folder:
#   cmake -S . -B build -G Ninja
#   cmake --build build
# gives build/index.html, index.js and index.wasm. Those three files zipped together, with
# index.html at the root of the zip, are what an itch.io HTML project takes.

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(mazethingie_web C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformWeb.h: 0 (straight into the frame), 1, 8 or 16.
# A browser has the memory for a full colour buffer, so that is the default
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


# 1 = frames are held to the game's frame rate, 0 = they run as fast as the browser hands them out,
# see FPSLOCK in defines.h
set(FPSLOCK 1 CACHE STRING "FPSLOCK setting: 1 or 0")

# 1 = the debug header is always shown, 0 = the debug combination shows and hides it, see FORCEDEBUG
# in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

# index.html, so the zip an itch.io project takes has the page it opens at its root
set(CMAKE_EXECUTABLE_SUFFIX ".html")
add_executable(index ${GAME_SOURCES})
target_compile_definitions(index PRIVATE PLATFORM_WEB
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} FPSLOCK=${FPSLOCK}
    FORCEDEBUG=${FORCEDEBUG})
target_include_directories(index PRIVATE "${GAME_DIR}")
# SDL2 is one of the libraries Emscripten brings, it has to be on both sides of the build
target_compile_options(index PRIVATE -sUSE_SDL=2 -O3)
target_link_options(index PRIVATE
    -sUSE_SDL=2
    -O3
    # a page only, not node, which keeps the javascript it writes down to what a browser needs
    -sENVIRONMENT=web
    # the game allocates its board as it goes, so the heap is left free to grow
    -sALLOW_MEMORY_GROWTH=1
    # nothing outside the game calls into it
    -sEXPORTED_RUNTIME_METHODS=[]
    # the page the game is wrapped in, see shell.html
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
set_target_properties(index PROPERTIES OUTPUT_NAME index)
