# devkitARM for the Game Boy Advance. DEVKITPRO is the folder holding devkitARM, libgba and tools
# (C:/devkitarm by default, or the DEVKITPRO environment variable).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{DEVKITPRO} AND NOT DEVKITPRO)
    file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" DEVKITPRO)
endif()
if(NOT DEVKITPRO)
    set(DEVKITPRO "C:/devkitarm")
endif()
set(DEVKITPRO "${DEVKITPRO}" CACHE PATH "folder with devkitARM, libgba and tools")

if(CMAKE_HOST_WIN32)
    set(EXE_SUFFIX ".exe")
else()
    set(EXE_SUFFIX "")
endif()

set(CMAKE_C_COMPILER "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-gcc${EXE_SUFFIX}")
set(CMAKE_CXX_COMPILER "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-g++${EXE_SUFFIX}")
set(CMAKE_ASM_COMPILER "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-gcc${EXE_SUFFIX}")
set(CMAKE_OBJCOPY "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-objcopy${EXE_SUFFIX}" CACHE FILEPATH "objcopy")
set(GBAFIX "${DEVKITPRO}/tools/bin/gbafix${EXE_SUFFIX}" CACHE FILEPATH "gbafix")

# the compiler check links a static library, a GBA program needs gba.specs to link
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
