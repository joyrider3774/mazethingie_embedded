# The mips64-elf toolchain libdragon brings for the Nintendo 64. N64_INST is the folder it and
# libdragon are installed in (C:/n64_dev by default, or the N64_INST environment variable).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mips64)

if(DEFINED ENV{N64_INST} AND NOT N64_INST)
    file(TO_CMAKE_PATH "$ENV{N64_INST}" N64_INST)
endif()
if(NOT N64_INST)
    set(N64_INST "C:/n64_dev")
endif()
set(N64_INST "${N64_INST}" CACHE PATH "folder with the mips64-elf toolchain and libdragon")

if(CMAKE_HOST_WIN32)
    set(EXE_SUFFIX ".exe")
else()
    set(EXE_SUFFIX "")
endif()

set(CMAKE_C_COMPILER "${N64_INST}/bin/mips64-elf-gcc${EXE_SUFFIX}")
set(CMAKE_CXX_COMPILER "${N64_INST}/bin/mips64-elf-g++${EXE_SUFFIX}")
set(CMAKE_ASM_COMPILER "${N64_INST}/bin/mips64-elf-gcc${EXE_SUFFIX}")
set(CMAKE_STRIP "${N64_INST}/bin/mips64-elf-strip${EXE_SUFFIX}" CACHE FILEPATH "strip")
# the tools that turn the linked program into a ROM
set(N64_SYM "${N64_INST}/bin/n64sym${EXE_SUFFIX}" CACHE FILEPATH "n64sym")
set(N64_ELFCOMPRESS "${N64_INST}/bin/n64elfcompress${EXE_SUFFIX}" CACHE FILEPATH "n64elfcompress")
set(N64_TOOL "${N64_INST}/bin/n64tool${EXE_SUFFIX}" CACHE FILEPATH "n64tool")
set(N64_ED64ROMCONFIG "${N64_INST}/bin/ed64romconfig${EXE_SUFFIX}" CACHE FILEPATH "ed64romconfig")

# the compiler check links a static library, a N64 program needs libdragon's linker script to link
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
