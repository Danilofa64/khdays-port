#!/usr/bin/env python3
"""Extract a DeSmuME savestate's display state for the native renderer's test (savestate_test.c): the
ARM9 I/O registers, palettes, OAM, VRAM and both framebuffers (the .dst format: docs/BUILD.md).
  python port/tools/savestate_gpu.py STATE.dst OUT.bin
"""

from __future__ import annotations

import struct
import sys
import zlib


def chunks(raw: bytes):
    off = 0
    while off + 8 <= len(raw):
        cid, size = struct.unpack_from("<II", raw, off)
        yield cid, raw[off + 8 : off + 8 + size]
        off += 8 + size


def fields(data: bytes) -> dict[str, bytes]:
    out = {}
    p = 0
    while p + 12 <= len(data):
        tag = data[p : p + 4]
        size, count = struct.unpack_from("<II", data, p + 4)
        if not all(32 <= c < 127 for c in tag):
            break
        out[tag.decode()] = data[p + 12 : p + 12 + size * count]
        p += 12 + size * count
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    blob = open(sys.argv[1], "rb").read()
    if not blob.startswith(b"DeSmuME SState"):
        raise SystemExit("not a DeSmuME savestate")
    raw = zlib.decompress(blob[0x20:])
    parts = dict(chunks(raw))
    memory = fields(parts[4])
    display = parts[7]
    version = struct.unpack_from("<I", display, 0)[0]
    if version != 2:
        raise SystemExit(f"unknown display chunk version {version}")
    framebuffers = display[4 : 4 + 2 * 256 * 192 * 2]
    out = memory["9REG"] + memory["VMEM"] + memory["OAMS"] + memory["LCDM"] + framebuffers
    assert len(memory["9REG"]) == 0x2000 and len(memory["LCDM"]) == 0xA4000
    open(sys.argv[2], "wb").write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
