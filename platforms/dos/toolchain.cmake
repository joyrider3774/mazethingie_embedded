# DJGPP for MS-DOS. DOSDEV is the folder the toolchain is unpacked in (C:/dos_dev by default, or the
# DOSDEV environment variable), holding djgpp/ and csdpmi/.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i586)

if(DEFINED ENV{DOSDEV} AND NOT DOSDEV)
    file(TO_CMAKE_PATH "$ENV{DOSDEV}" DOSDEV)
endif()
if(NOT DOSDEV)
    set(DOSDEV "C:/dos_dev")
endif()
set(DOSDEV "${DOSDEV}" CACHE PATH "folder with the DJGPP toolchain and CWSDPMI")

if(CMAKE_HOST_WIN32)
    set(EXE_SUFFIX ".exe")
else()
    set(EXE_SUFFIX "")
endif()

set(DJGPP_PREFIX "${DOSDEV}/djgpp/bin/i586-pc-msdosdjgpp-")
set(CMAKE_C_COMPILER "${DJGPP_PREFIX}gcc${EXE_SUFFIX}")
set(CMAKE_CXX_COMPILER "${DJGPP_PREFIX}g++${EXE_SUFFIX}")
set(CMAKE_ASM_COMPILER "${DJGPP_PREFIX}gcc${EXE_SUFFIX}")

# CWSDPMI is the DPMI host a DJGPP program needs to run. CWSDSTUB is the same thing as a stub that
# goes in front of the program, which makes it one file that runs on a plain DOS with nothing else
set(CWSDSTUB "${DOSDEV}/csdpmi/bin/CWSDSTUB.EXE" CACHE FILEPATH "the DPMI host that is built in")

# the compiler check links a whole program, which DJGPP does happily
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
