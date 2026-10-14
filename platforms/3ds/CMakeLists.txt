# Nintendo 3DS build of Mazethingie: the game's own sources with the PLATFORM_3DS platform
# (source/mazethingie_embedded/Platform3DS.*), devkitARM and libctru (toolchain.cmake, C:/devkitarm by
# default).
#
# With MSYS2's cmake and ninja, from this folder:
#   cmake -S . -B build -G Ninja
#   cmake --build build
# gives build/Mazethingie.3dsx, for an emulator or the Homebrew Launcher. Its save is a file in
# sdmc:/3ds/Mazethingie/ on the SD card.

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(mazethingie_3ds C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)
# what the Homebrew Launcher shows for the game
set(APP_TITLE "Mazethingie")
set(APP_DESCRIPTION "A maze game")
set(APP_AUTHOR "joyrider3774")

if(NOT EXISTS "${DEVKITPRO}/libctru/include/3ds.h")
    message(FATAL_ERROR "libctru not found in ${DEVKITPRO}/libctru, pass -DDEVKITPRO=<folder>")
endif()

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in Platform3DS.h: 1, 8 or 16, the screen is never drawn to
# straight
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

# 1 = scaled to 240x240, 0 = 1:1 in the middle, see SCALESCREEN in Platform3DS.h
set(SCALESCREEN 1 CACHE STRING "SCALESCREEN setting: 1 or 0")

# 1 = the debug header is always shown, 0 = the debug combination shows and hides it, see FORCEDEBUG
# in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

# the ARM11 of the 3DS, an ARM11 MPCore
set(ARCH_FLAGS -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations)

add_executable(${GAME_NAME}.elf ${GAME_SOURCES})
target_compile_definitions(${GAME_NAME}.elf PRIVATE PLATFORM_3DS __3DS__
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} SCALESCREEN=${SCALESCREEN}
    FORCEDEBUG=${FORCEDEBUG})
target_include_directories(${GAME_NAME}.elf PRIVATE "${GAME_DIR}" "${DEVKITPRO}/libctru/include")
target_compile_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} -O3 -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
# 3dsx.specs: the start up code and linker script of a Homebrew Launcher program
target_link_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} -specs=3dsx.specs -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${GAME_NAME}.map")
target_link_directories(${GAME_NAME}.elf PRIVATE "${DEVKITPRO}/libctru/lib")
target_link_libraries(${GAME_NAME}.elf PRIVATE ctru m)

# the icon of the Homebrew Launcher and the program it runs
add_custom_command(TARGET ${GAME_NAME}.elf POST_BUILD
    COMMAND ${SMDHTOOL} --create "${APP_TITLE}" "${APP_DESCRIPTION}" "${APP_AUTHOR}"
            "${DEVKITPRO}/libctru/default_icon.png" ${GAME_NAME}.smdh
    COMMAND ${THREEDSXTOOL} ${GAME_NAME}.elf ${GAME_NAME}.3dsx --smdh=${GAME_NAME}.smdh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
