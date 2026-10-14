# Builds Windows binaries on Linux with mingw-w64, for the CI workflow: the Windows exe, the libretro
# core's dll and the Playdate simulator's pdex.dll all come out of a Linux runner this way.
# tools/build_releases.py passes this with --cross-windows.
#
# Ubuntu has the compilers in the mingw-w64 package. Pass the SDL2 mingw development folder as
# CMAKE_PREFIX_PATH (build_releases.py does that from --sdl2-mingw) so find_package(SDL2) finds it.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
# the compilers are the host's, everything else is looked for in the Windows sysroot and in whatever
# CMAKE_PREFIX_PATH adds (SDL2)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
