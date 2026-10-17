#!/usr/bin/env python3
"""The HD videos and their subtitles from the 358/2 Days theater (MARE, its videos in --movies: found
from MARE by default), from your own copy (docs/HD_SCREENS.md, "Making them"). Without --videos or
--subtitles it does both; a video already there is kept unless --force.
  python tools/hd/theater_movies.py MARE [--movies DIR] [--videos] [--subtitles] [--force] [--lang ...]
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import theater_title as tt  # noqa: E402
from newgame_dialog import ctd, plain  # noqa: E402

# The theater's folders and names for the port's languages: the folder, the name in its paths,
# the exia2's language.
LANGUAGES = {
    "es": ("sp", "sp", "LANGUAGE_SP"),
    "en": ("uk", "en", "LANGUAGE_EN"),
    "fr": ("fr", "fr", "LANGUAGE_FR"),
    "de": ("gr", "de", "LANGUAGE_GE"),
    "it": ("it", "it", "LANGUAGE_IT"),
}
PATTERN = bytes.fromhex("3f1ec193264258b8aa9d5375bf629a97")  # XORed over the videos' first 0x100 bytes
SCRAMBLED = 0x100
# The tables of Days's words: groups 0x200-0x2a8 (its event scenes, whose lines some videos say
# too) and 0x391-0x3a8 (its videos'). Re:coded's (cthd500, 550, 600: groups 0x2fa-0x32f) are not
# read: two pass 64 KB, which their entries' 16-bit offsets do not reach.
WORDS = ("cthd000.ctd", "cthd050.ctd", "cthd100.ctd", "cthd300.ctd", "cthd800.ctd")
OPENING = ("DOP.mp4", "802")
ENDING = ("DED.mp4", "843")
ENDING_EVENT = "844"  # the ending's subtitles: no DS movie 844, its sound DED_MONO
OUT = tt.ROOT / "data" / "cutscenes" / "cinematics"
# Measured on the theater's window captures (hd818's lines, scaled to 1920 x 1080): the font's cells
# drawn 1.445 times their size, a space 13.75 texels, the lines 51 apart, the last one's top at 956,
# the block centred on 964 (docs/HD_SCREENS.md).
SCALE, SPACE, PITCH, LAST_TOP, CENTRE = 1.445, 13.75, 51, 956, 964
PICTURE = (1920, 1080)
RATE = 30


# --- The videos ---------------------------------------------------------------------------------


def videos(movies: Path, force: bool) -> int:
    made = 0
    names = [(p.name, p.stem[2:]) for p in sorted(movies.glob("hd8*.mp4"))] + [OPENING, ENDING]
    OUT.mkdir(parents=True, exist_ok=True)
    for name, movie in names:
        source, target = movies / name, OUT / f"hd{movie}.mp4"
        if not source.is_file():
            print(f"{source}: not found")
            continue
        if target.exists() and not force:
            print(f"{target.name}: kept (--force replaces it)")
            continue
        data = bytearray(source.read_bytes())
        for i in range(min(SCRAMBLED, len(data))):
            data[i] ^= PATTERN[i % 16]
        if data[4:8] != b"ftyp":
            print(f"{source}: not one of the theater's videos as known (no MP4 header once unscrambled)")
            continue
        target.write_bytes(data)
        made += 1
        print(f"{target.name} from {name}")
    return made


# --- The subtitles ------------------------------------------------------------------------------


class EventFont:
    """The theater's event font: eventfont.tm2 (2048 x 2048, cells of 32 x 32: eventfont.inf)
    and eventfont.cod (8 bytes a glyph: its UTF-16 code, its cell's x and y, a byte, its width)."""

    def __init__(self, folder: Path):
        cod = (folder / "eventfont.cod").read_bytes()
        self.glyphs = {}
        for i in range(len(cod) // 8):
            code, x, y, _, w = struct.unpack_from("<HHHBB", cod, 8 * i)
            self.glyphs[chr(code)] = (x, y, w)
        self.picture = tt.tim2_picture((folder / "eventfont.tm2").read_bytes(), 0)
        self.ink = tt.ink_widths(self.picture, self.glyphs)  # each glyph with its whole outline
        self.missing = set()

    def line(self, text: str) -> np.ndarray:
        """The line, white and outlined, as RGBA at the picture's size."""
        width = sum(SPACE if c == " " else self.glyphs[c][2] if c in self.glyphs else 0 for c in text)
        reach = max([self.ink[c] - self.glyphs[c][2] for c in text if c in self.glyphs] + [0])
        cells = np.zeros((32, int(np.ceil(width)) + 1 + reach, 4), dtype=np.uint8)
        x = 0.0
        for c in text:
            if c == " ":
                x += SPACE
                continue
            if c not in self.glyphs:
                self.missing.add(c)
                continue
            gx, gy, w = self.glyphs[c]
            at = int(round(x))
            region = cells[:, at : at + self.ink[c]]
            glyph = self.picture[gy : gy + 32, gx : gx + self.ink[c]]
            np.copyto(region, glyph[:, : region.shape[1]], where=glyph[:, : region.shape[1], 3:4] > region[..., 3:4])
            x += w
        # the PS2's colour: the texels' grey made white (0x80 = 1.0 of a vertex colour of 2.0)
        cells[..., :3] = np.minimum(255, cells[..., :3].astype(np.int32) * 2).astype(np.uint8)
        im = Image.fromarray(cells, "RGBA")
        size = (max(1, int(round(im.size[0] * SCALE))), int(round(32 * SCALE)))
        return np.asarray(im.resize(size, Image.LANCZOS))


def schedule(event: Path, language: str):
    """The language's lines: (first frame, end frame, TextID)."""
    xml = event.read_bytes().decode("utf-8", errors="replace")
    for category in re.finditer(
        r'<SCHEDULE_CATEGORY type = "SCHEDULE_CATEGORY_TEXT_MOVIE">(.*?)</SCHEDULE_CATEGORY>', xml, re.S
    ):
        if f'Language="{language}"' in category.group(1):  # (the file's header names a language too)
            return [
                (int(s), int(e), int(t, 16))
                for s, e, t in re.findall(
                    r'FrameStart="(\d+)" FrameEnd="(\d+)" TextID="([0-9a-fA-F]+)"', category.group(1)
                )
            ]
    return []


def subtitles(mare: Path, lang: str) -> int:
    folder, name, language = LANGUAGES[lang]
    root = mare / folder
    words = {}
    for table in WORDS:
        words.update(ctd(root / "message" / name / "event" / "hd" / "bin" / table))
    font = EventFont(root / "font")
    out = OUT / "subtitles" / lang
    out.mkdir(parents=True, exist_ok=True)
    made = 0
    for event in sorted((root / "event" / name / "hd").glob("hd8*/hd8*.exia2")):
        movie = event.stem[2:]
        if movie == ENDING_EVENT:
            movie = ENDING[1]
        lines = schedule(event, language)
        if not lines:
            continue
        atlas, placed = tt.Atlas(), []
        for n, (first, end, text_id) in enumerate(lines):
            text = words.get((text_id >> 16, text_id & 0xFFFF))
            if text is None:
                print(f"{lang} hd{movie}: no words for {text_id:x}")
                continue
            rows = [font.line(row) for row in (plain(r).rstrip() for r in text.replace("\r", "").split("\n")) if row]
            if not rows:
                continue
            block = max(r.shape[1] for r in rows)
            for i, image in enumerate(rows):
                atlas.add((n, i), image)
                placed.append((first, end, (n, i), round(CENTRE - block / 2), LAST_TOP - (len(rows) - 1 - i) * PITCH))
        if not placed:
            print(f"{lang} hd{movie}: no lines")
            continue
        pixels = atlas.pack()
        Image.fromarray(pixels, "RGBA").save(out / f"hd{movie}.png")
        txt = [
            f"# hd{movie}.mp4's subtitles as the HD collection's 358/2 Days theater draws them, made by",
            "# tools/hd/theater_movies.py from the player's own copy: each line's frames (the video's,",
            "# `rate` a second; it shows from the first to before the end), its place in the picture and",
            "# its texels in the atlas.",
            f"picture {PICTURE[0]} {PICTURE[1]}",
            f"rate {RATE}",
            f"atlas hd{movie}.png",
        ]
        for first, end, region, x, y in placed:
            u, v = atlas.at[region]
            h, w = atlas.pixels[region].shape[:2]
            txt.append(f"line {first} {end} {x} {y} {u} {v} {w} {h}")
        (out / f"hd{movie}.txt").write_text("\n".join(txt) + "\n", encoding="utf-8", newline="\n")
        made += 1
    if font.missing:
        print(f"{lang}: the event font has no {''.join(sorted(font.missing))!r} (left out)")
    print(f"{lang}: {made} videos' subtitles in {out}")
    return made


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mare", type=Path, help="the theater extracted: <collection>/Image/Mare.hed_out/original")
    ap.add_argument(
        "--movies",
        type=Path,
        help="its videos: <collection>/STEAM/Mare/MOVIE/Days/dt (Steam's), <collection>/EPIC/Mare/MOVIE/Days/en "
        "(the Epic Games Store's)",
    )
    ap.add_argument("--videos", action="store_true")
    ap.add_argument("--subtitles", action="store_true")
    ap.add_argument("--force", action="store_true", help="replace the videos already there")
    ap.add_argument("--lang", nargs="*", default=list(LANGUAGES), choices=list(LANGUAGES))
    args = ap.parse_args()
    both = not args.videos and not args.subtitles
    if args.videos or both:
        collection = args.mare.resolve().parents[2]
        movies = args.movies or next(
            (m for m in (collection / "STEAM" / "Mare" / "MOVIE" / "Days" / "dt",
                         collection / "EPIC" / "Mare" / "MOVIE" / "Days" / "en") if m.is_dir()),
            collection / "STEAM" / "Mare" / "MOVIE" / "Days" / "dt",
        )
        print(videos(movies, args.force), "videos made")
    if args.subtitles or both:
        for lang in args.lang:
            subtitles(args.mare, lang)
    return 0


if __name__ == "__main__":
    sys.exit(main())
