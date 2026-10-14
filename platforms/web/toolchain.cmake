# Emscripten for the browser build. EMSDK is the folder the SDK is installed in (C:/github/emsdk by
# default, or the EMSDK environment variable), the same one emsdk_env sets.

if(DEFINED ENV{EMSDK} AND NOT EMSDK)
    file(TO_CMAKE_PATH "$ENV{EMSDK}" EMSDK)
endif()
if(NOT EMSDK)
    set(EMSDK "C:/github/emsdk")
endif()
set(EMSDK "${EMSDK}" CACHE PATH "folder the Emscripten SDK is installed in")

set(EMSCRIPTEN_ROOT "${EMSDK}/upstream/emscripten")
if(NOT EXISTS "${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
    message(FATAL_ERROR "Emscripten not found in ${EMSDK}, pass -DEMSDK=<folder>")
endif()

# Emscripten brings its own toolchain file, this one only finds it
include("${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
