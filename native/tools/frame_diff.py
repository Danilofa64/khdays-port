#!/usr/bin/env python3
"""Compare two directories of frame shots (KHDAYS_SHOTS, native/runtime/display/display.c).

    python native/tools/frame_diff.py A_DIR B_DIR [--rects N]

Each frame_NNNNN.bmp in both directories (256x384: the upper screen, then
the lower, 1x) is compared pixel for pixel. A frame that differs is named
with how many pixels differ and the rectangles around each group of
differing pixels that touch (screen, x, y, width, height in DS pixels), at
most N of them (8 by default). Exit 0 when every common frame is the same,
1 when some differ, 2 when there is nothing to compare.

What a layout moves should differ only inside the element's old and new
places; with every part off, nothing should differ from
the build before.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

W, H = 256, 384


def read_bmp(path: Path) -> bytes:
    """The picture's pixels top row first, 3 bytes each (B, G, R)."""
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if (width, abs(height), bits) != (W, H, 24):
        raise ValueError(f"{path}: {width}x{height}x{bits}, not a {W}x{H} 24-bit frame shot")
    row = W * 3
    rows = [data[offset + y * row: offset + (y + 1) * row] for y in range(H)]
    if height > 0:
        rows.reverse()  # stored bottom-up
    return b"".join(rows)


def groups(mask: list[bool]) -> list[tuple[int, int, int, int]]:
    """Bounding rectangles (x0, y0, x1, y1 inclusive) of the 8-connected groups."""
    seen = [False] * len(mask)
    out = []
    for start, on in enumerate(mask):
        if not on or seen[start]:
            continue
        seen[start] = True
        stack = [start]
        x0 = x1 = start % W
        y0 = y1 = start // W
        while stack:
            i = stack.pop()
            x, y = i % W, i // W
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H:
                        j = ny * W + nx
                        if mask[j] and not seen[j]:
                            seen[j] = True
                            stack.append(j)
        out.append((x0, y0, x1, y1))
    return out


def describe(rect: tuple[int, int, int, int]) -> str:
    x0, y0, x1, y1 = rect
    screen = "upper" if y0 < H // 2 else "lower"
    top = y0 if y0 < H // 2 else y0 - H // 2
    return f"{screen} {x0},{top} {x1 - x0 + 1}x{y1 - y0 + 1}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    ap.add_argument("--rects", type=int, default=8)
    args = ap.parse_args()
    names = sorted({p.name for p in args.a.glob("frame_*.bmp")} & {p.name for p in args.b.glob("frame_*.bmp")})
    if not names:
        print("no frame shots in both directories")
        return 2
    differing = 0
    for name in names:
        a, b = read_bmp(args.a / name), read_bmp(args.b / name)
        if a == b:
            continue
        mask = [a[i * 3:i * 3 + 3] != b[i * 3:i * 3 + 3] for i in range(W * H)]
        rects = groups(mask)
        differing += 1
        shown = ", ".join(describe(r) for r in rects[:args.rects])
        more = f" (+{len(rects) - args.rects} more)" if len(rects) > args.rects else ""
        print(f"{name}: {sum(mask)} pixels differ in {len(rects)} places: {shown}{more}")
    print(f"{len(names)} frames compared, {differing} differ")
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main())
