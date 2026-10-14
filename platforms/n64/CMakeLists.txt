# Nintendo 64 build of Mazethingie: the game's own sources with the PLATFORM_N64 platform
# (source/mazethingie_embedded/PlatformN64.*) and libdragon (toolchain.cmake, C:/n64_dev by default).
#
# With MSYS2's cmake and ninja, from this folder:
#   cmake -S . -B build -G Ninja
#   cmake --build build
# gives build/Mazethingie.z64, for an emulator or a flash cart. Its save goes into the cartridge's
# EEPROM, which the ROM is marked as having.

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(mazethingie_n64 C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(GAME_NAME Mazethingie)
# what a flash cart's menu shows for the game, 20 characters at most
set(N64_ROM_TITLE "Mazethingie")

set(N64_INCLUDE_DIR "${N64_INST}/mips64-elf/include")
set(N64_LIB_DIR "${N64_INST}/mips64-elf/lib")
if(NOT EXISTS "${N64_INCLUDE_DIR}/libdragon.h")
    message(FATAL_ERROR "libdragon not found in ${N64_INST}, pass -DN64_INST=<folder>")
endif()

# the game's folder, next to this one unless a build copies this folder somewhere else
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../source/mazethingie_embedded" CACHE PATH "the game's source folder")

# where drawing goes, see SCREENBUFFER in PlatformN64.h: 0 (straight into the frame), 1, 8 or 16
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


# 1 = scaled to 240x240, 0 = 1:1 in the middle, see SCALESCREEN in PlatformN64.h
set(SCALESCREEN 1 CACHE STRING "SCALESCREEN setting: 1 or 0")

# 1 = the debug header is always shown, 0 = the debug combination shows and hides it, see FORCEDEBUG
# in defines.h
set(FORCEDEBUG 0 CACHE STRING "FORCEDEBUG setting: 0 or 1")

# the game's files, not the ESPboy platform or its library (lib/ is not globbed)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${GAME_DIR}/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX "PlatformESPboy\.cpp$")

# the VR4300 of the N64, the flags libdragon's n64.mk builds with
set(ARCH_FLAGS -march=vr4300 -mtune=vr4300 -mabi=o64)

add_executable(${GAME_NAME}.elf ${GAME_SOURCES})
target_compile_definitions(${GAME_NAME}.elf PRIVATE PLATFORM_N64 N64
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING} SCALESCREEN=${SCALESCREEN}
    FORCEDEBUG=${FORCEDEBUG})
target_include_directories(${GAME_NAME}.elf PRIVATE "${GAME_DIR}" "${N64_INCLUDE_DIR}")
# -falign-functions=32 is what libdragon's backtrace() expects of every function
target_compile_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} -O2 -falign-functions=32
    -ffunction-sections -fdata-sections -g
    -ffast-math -ftrapping-math -fno-associative-math
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
# n64.ld is libdragon's linker script, and its start up code runs the global constructors itself
target_link_options(${GAME_NAME}.elf PRIVATE ${ARCH_FLAGS} -g
    "-L${N64_LIB_DIR}" -Wl,-Tn64.ld -Wl,--gc-sections -Wl,--wrap,__do_global_ctors
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${GAME_NAME}.map")
target_link_directories(${GAME_NAME}.elf PRIVATE "${N64_LIB_DIR}")
target_link_libraries(${GAME_NAME}.elf PRIVATE dragon m dragonsys c)

# The ROM, the way libdragon's n64.mk builds one: the symbols of the program for its backtraces, the
# program itself stripped and packed, and the two of them in a ROM with libdragon's own boot code
# the tools look for the toolchain in N64_INST themselves, so they are run with it set
set(N64_ENV ${CMAKE_COMMAND} -E env "N64_INST=${N64_INST}")
add_custom_command(TARGET ${GAME_NAME}.elf POST_BUILD
    COMMAND ${N64_ENV} ${N64_SYM} ${GAME_NAME}.elf ${GAME_NAME}.elf.sym
    COMMAND ${CMAKE_COMMAND} -E copy ${GAME_NAME}.elf ${GAME_NAME}.elf.stripped
    COMMAND ${CMAKE_STRIP} -s ${GAME_NAME}.elf.stripped
    COMMAND ${N64_ENV} ${N64_ELFCOMPRESS} -o . -c 1 ${GAME_NAME}.elf.stripped
    COMMAND ${N64_ENV} ${N64_TOOL} --title ${N64_ROM_TITLE} --toc --output ${GAME_NAME}.z64
            --align 256 ${GAME_NAME}.elf.stripped --align 8 ${GAME_NAME}.elf.sym --align 8
    # so an emulator or a flash cart knows the cartridge has the EEPROM the game saves in
    COMMAND ${N64_ENV} ${N64_ED64ROMCONFIG} --savetype eeprom4k --regionfree ${GAME_NAME}.z64
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
