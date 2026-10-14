# Game Boy Advance build of Mazethingie: the game's own sources with the PLATFORM_GBA platform
# (source/mazethingie_embedded/PlatformGBA.*), devkitARM and libgba (toolchain.cmake, C:/devkitarm by default).
#
# With MSYS2's cmake and ninja, from this folder:
#   cmake -S . -B build -G Ninja
#   cmake --build build
# gives build/Mazethingie.gba, for an emulator or a flash cart. Its save is the cartridge's SRAM.

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(mazethingie_gba C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)
# the cartridge header: a title of at most 12 characters and a 4 character game code
set(GBA_TITLE "Mazethingie")
set(GBA_CODE "MAZE")

if(NOT EXISTS "${DEVKITPRO}/libgba/include/gba.h")
    message(FATAL_ERROR "libgba not found in ${DEVKITPRO}/libgba, pass -DDEVKITPRO=<folder>")
endif()

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformGBA.h: 0 (straight to the display), 1, 8 or 16
set(SCREENBUFFER 0 CACHE STRING "SCREENBUFFER setting: 0, 1, 8 or 16")
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

# 1 = scaled to 160x160, 0 = 1:1 in the middle, see SCALESCREEN in PlatformGBA.h
set(SCALESCREEN 1 CACHE STRING "SCALESCREEN setting: 1 or 0")

# 1 = the screen buffer is in the GBA's fast memory, 0 = in its work RAM. A game whose globals leave
# no room says "region `iwram' overflowed" when this is 1, see BUFFERINIWRAM in PlatformGBA.h
set(BUFFERINIWRAM 1 CACHE STRING "BUFFERINIWRAM setting: 1 or 0")

# 1 = the debug header is always shown, 0 = Up + Down shows and hides it, see FORCEDEBUG in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

set(ARCH_FLAGS -mthumb -mthumb-interwork -mcpu=arm7tdmi -mtune=arm7tdmi)

add_executable(${GAME_NAME}.elf ${GAME_SOURCES})
target_compile_definitions(${GAME_NAME}.elf PRIVATE PLATFORM_GBA SCREENBUFFER=${SCREENBUFFER} DITHERING=${DITHERING} SCALESCREEN=${SCALESCREEN}
    BUFFERINIWRAM=${BUFFERINIWRAM} FORCEDEBUG=${FORCEDEBUG})
target_include_directories(${GAME_NAME}.elf PRIVATE "${GAME_DIR}" "${DEVKITPRO}/libgba/include")
target_compile_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} -O3 -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
# gba.specs: the cartridge's crt0 and linker script, the heap in EWRAM
target_link_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} -specs=gba.specs -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${GAME_NAME}.map")
target_link_directories(${GAME_NAME}.elf PRIVATE "${DEVKITPRO}/libgba/lib")
target_link_libraries(${GAME_NAME}.elf PRIVATE gba)

# the ROM: the program as a binary with a valid cartridge header
add_custom_command(TARGET ${GAME_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${GAME_NAME}.elf ${GAME_NAME}.gba
    COMMAND ${GBAFIX} ${GAME_NAME}.gba -t${GBA_TITLE} -c${GBA_CODE} -mJD -r0
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
