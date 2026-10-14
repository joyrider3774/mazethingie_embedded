# libretro core of Mazethingie: the game's own sources with the PLATFORM_LIBRETRO platform
# (source/mazethingie_embedded/PlatformLibretro.*), for RetroArch and the other libretro frontends. Needs
# libretro.h from libretro-common (https://github.com/libretro/libretro-common).
#
# With MSYS2 (mingw64 packages cmake, ninja and gcc), from this folder:
#   cmake -S . -B build -G Ninja
#   cmake --build build
# gives build/mazethingie_libretro.dll. Copy it into RetroArch's cores folder and mazethingie_libretro.info into its
# info folder, then start it with "Load Core" and "Start Core". On Linux the same gives
# mazethingie_libretro.so.

cmake_minimum_required(VERSION 3.16)
project(mazethingie_libretro C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(LIBRETRO_COMMON_DIR "C:/github/libretro-common" CACHE PATH "libretro-common folder")
if(NOT EXISTS "${LIBRETRO_COMMON_DIR}/include/libretro.h")
    message(FATAL_ERROR "libretro.h not found in ${LIBRETRO_COMMON_DIR}/include, pass -DLIBRETRO_COMMON_DIR=<folder>")
endif()

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformLibretro.h: 1, 8 or 16
set(SCREENBUFFER 16 CACHE STRING "SCREENBUFFER setting: 1, 8 or 16")
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

add_library(mazethingie_libretro SHARED ${GAME_SOURCES})
# the name frontends look for: mazethingie_libretro.dll / .so, without the lib gcc puts in front
set_target_properties(mazethingie_libretro PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(mazethingie_libretro PRIVATE PLATFORM_LIBRETRO LIBRETRO_GAME_NAME="Mazethingie" LIBRETRO_GAME_VERSION="1.0"
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} FORCEDEBUG=${FORCEDEBUG})
target_include_directories(mazethingie_libretro PRIVATE "${GAME_DIR}" "${LIBRETRO_COMMON_DIR}/include")
target_compile_options(mazethingie_libretro PRIVATE -ffunction-sections -fdata-sections)

if(MINGW)
    # libgcc, libstdc++ and winpthread linked in: the dll needs nothing from MSYS2 next to it
    target_link_options(mazethingie_libretro PRIVATE -static -Wl,--gc-sections $<$<NOT:$<CONFIG:Debug>>:-s>)
elseif(UNIX AND NOT APPLE)
    # only the retro_ functions are exported
    target_link_options(mazethingie_libretro PRIVATE -Wl,--gc-sections -Wl,--no-undefined)
endif()

# the core's info file next to the core
add_custom_command(TARGET mazethingie_libretro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/mazethingie_libretro.info" "$<TARGET_FILE_DIR:mazethingie_libretro>/mazethingie_libretro.info")
