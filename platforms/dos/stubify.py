#!/usr/bin/env python3
"""Puts CWSDPMI in front of a DJGPP program, so the game is one file that runs on a plain DOS.

A DJGPP program is a small DOS stub followed by the 32 bit program itself, and the stub only knows
how to look for a DPMI host and complain when there is none. Swapping that stub for CWSDSTUB.EXE,
which is the host itself, makes the program carry what it needs. This is what exe2coff and a copy
/b do in the DJGPP instructions, without needing DOS to do it.

  python stubify.py <cwsdstub.exe> <program.exe>
"""
import struct
import sys


def coff_offset(data):
    """Where the 32 bit program starts, which is the end of the DOS stub in front of it"""
    if data[:2] not in (b"MZ", b"ZM"):
        raise SystemExit("not a DOS program: %r" % data[:2])
    # the last page of the stub is partly used, the header says how much of it
    last_page, pages = struct.unpack_from("<HH", data, 2)
    size = pages * 512
    if last_page:
        size = size - 512 + last_page
    return size


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    stub_path, exe_path = sys.argv[1], sys.argv[2]
    with open(exe_path, "rb") as f:
        exe = f.read()
    with open(stub_path, "rb") as f:
        stub = f.read()
    start = coff_offset(exe)
    if start >= len(exe):
        raise SystemExit("no program found after the stub in %s" % exe_path)
    # already stubbed with CWSDPMI, which happens when a build is run again over its own output
    if exe[:len(stub)] == stub:
        return
    with open(exe_path, "wb") as f:
        f.write(stub)
        f.write(exe[start:])


if __name__ == "__main__":
    main()
