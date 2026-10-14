# Nintendo DS build of Mazethingie: the game's own sources with the PLATFORM_NDS platform
# (source/mazethingie_embedded/PlatformNDS.*), devkitARM, libnds and calico (toolchain.cmake,
# C:/devkitarm by default).
#
# With MSYS2's cmake and ninja, from this folder:
#   cmake -S . -B build -G Ninja
#   cmake --build build
# gives build/Mazethingie.nds, for an emulator or a flash card. Its save is a file on the card the game
# was started from.

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(mazethingie_nds C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)
# what the DS menu shows for the game
set(NDS_TITLE "Mazethingie")
set(NDS_SUBTITLE1 "joyrider3774")
set(NDS_SUBTITLE2 "joyrider3774.itch.io")

if(NOT EXISTS "${DEVKITPRO}/libnds/include/nds.h")
    message(FATAL_ERROR "libnds not found in ${DEVKITPRO}/libnds, pass -DDEVKITPRO=<folder>")
endif()
if(NOT EXISTS "${DEVKITPRO}/calico/share/ds9.specs")
    message(FATAL_ERROR "calico not found in ${DEVKITPRO}/calico, pass -DDEVKITPRO=<folder>")
endif()

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformNDS.h: 0 (straight to the bitmap), 1, 8 or 16
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

# 1 = scaled to 192x192, 0 = 1:1 in the middle, see SCALESCREEN in PlatformNDS.h
set(SCALESCREEN 1 CACHE STRING "SCALESCREEN setting: 1 or 0")

# 1 = the debug header is always shown, 0 = the debug combination shows and hides it, see FORCEDEBUG
# in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\\.cpp$")

# the ARM9 of the DS, an ARM946E-S
set(ARCH_FLAGS -march=armv5te -mtune=arm946e-s -mthumb -mthumb-interwork)

add_executable(${GAME_NAME}.elf ${GAME_SOURCES})
target_compile_definitions(${GAME_NAME}.elf PRIVATE PLATFORM_NDS __NDS__ ARM9
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} SCALESCREEN=${SCALESCREEN}
    FORCEDEBUG=${FORCEDEBUG})
target_include_directories(${GAME_NAME}.elf PRIVATE "${GAME_DIR}"
    "${DEVKITPRO}/libnds/include" "${DEVKITPRO}/calico/include")
target_compile_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} -O3 -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
# ds9.specs: calico's linker script and start up code for the ARM9
target_link_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} "-specs=${DEVKITPRO}/calico/share/ds9.specs"
    -Wl,--gc-sections "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${GAME_NAME}.map")
target_link_directories(${GAME_NAME}.elf PRIVATE "${DEVKITPRO}/libnds/lib" "${DEVKITPRO}/calico/lib")
# fat before nds9: the save file goes through libfat, which asks libnds for the card
target_link_libraries(${GAME_NAME}.elf PRIVATE fat nds9 calico_ds9)

# the card image: the program, the ARM7 calico supplies and the icon of the DS menu
add_custom_command(TARGET ${GAME_NAME}.elf POST_BUILD
    COMMAND ${NDSTOOL} -c ${GAME_NAME}.nds -9 ${GAME_NAME}.elf
            -7 "${DEVKITPRO}/calico/bin/ds7_maine.elf"
            -b "${DEVKITPRO}/calico/share/nds-icon.bmp" "${NDS_TITLE}$<SEMICOLON>${NDS_SUBTITLE1}$<SEMICOLON>${NDS_SUBTITLE2}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
