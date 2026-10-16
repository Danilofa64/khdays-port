#!/usr/bin/env python3
"""The game's save file (saves/khdays.sav: the backup chip's 64 KB), read the way the game signs it:
with one file, each slot copy signed and whether its digest still matches; with two, whether they are the
same save (exit 0) but for the digests and the play-time word (docs/BUILD.md, "The tools").
    python port/tools/save_file.py A.sav [B.sav]
"""
import hashlib
import struct
import sys
from pathlib import Path

MAGIC = 0xC8F592A6
COPY = 0x2018
SIGNED = 0x1CAC
VARYING_WORD = 0x1B0  # differs between two saves of the same play (module docstring)


def copies(data: bytes):
    """(base, signed, digest ok) for each of the six slot copies."""
    for slot in range(3):
        for half in range(2):
            base = slot * 2 * COPY + 0x20 + half * COPY
            if base + 0x18 + SIGNED > len(data):
                continue
            signed = struct.unpack_from("<I", data, base)[0] == MAGIC
            ok = signed and data[base + 4:base + 0x18] == hashlib.sha1(data[base + 0x18:base + 0x18 + SIGNED]).digest()
            yield base, signed, ok


def broken_copies(data: bytes) -> list[int]:
    """The copies the game signed whose digest no longer matches."""
    return [base for base, signed, ok in copies(data) if signed and not ok]


def same_save(a: bytes, b: bytes) -> list[int]:
    """The offsets where `a` and `b` differ, leaving out what two saves of the
    same play differ in (each copy both sign: its digest and the varying
    word)."""
    ignored: set[int] = set()
    signed_a = {base for base, signed, ok in copies(a) if ok}
    signed_b = {base for base, signed, ok in copies(b) if ok}
    for base in signed_a & signed_b:
        ignored.update(range(base + 4, base + 0x18))
        ignored.update(range(base + VARYING_WORD, base + VARYING_WORD + 4))
    size = max(len(a), len(b))
    return [i for i in range(size)
            if i not in ignored and (a[i] if i < len(a) else None) != (b[i] if i < len(b) else None)]


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        return 2
    a = Path(sys.argv[1]).read_bytes()
    if len(sys.argv) == 2:
        for base, signed, ok in copies(a):
            print(f"copy at 0x{base:05x}: " + ("signed, digest " + ("ok" if ok else "BROKEN") if signed else "empty"))
        return 1 if broken_copies(a) else 0
    b = Path(sys.argv[2]).read_bytes()
    differ = same_save(a, b)
    if differ:
        print(f"{len(differ)} bytes differ, the first at 0x{differ[0]:05x}")
        return 1
    print("the same save (digests and the varying word aside)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
