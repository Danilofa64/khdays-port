"""tools/hd/theater_movies.py without the collection: the videos the scene map names, made from a
scrambled source and from a plain one, a video already there kept, the opening's and the ending's
names, and the map's stems read past its comments and `off` entries."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import theater_movies as tm  # noqa: E402

PLAIN = b"\x00\x00\x00\x20ftypmp42" + bytes(range(256)) * 2  # an MP4's first bytes, then anything


def scrambled(data: bytes) -> bytes:
    out = bytearray(data)
    for i in range(min(tm.SCRAMBLED, len(out))):
        out[i] ^= tm.PATTERN[i % 16]
    return bytes(out)


class Movies(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.movies = root / "dt"
        self.movies.mkdir()
        self.saved = tm.OUT, tm.SCENE_MAP
        tm.OUT = root / "cinematics"
        tm.SCENE_MAP = root / "hd_scenes.txt"
        tm.SCENE_MAP.write_text(
            "# the port's\nEV_S#196 hd002 ; pictures\nEV_S#3 off ; none\nmnl#0 hd000 # a comment\n"
            "EV_S#169 hd131a hd132\n",
            encoding="utf-8",
        )

    def tearDown(self):
        tm.OUT, tm.SCENE_MAP = self.saved
        self.temp.cleanup()

    def test_scene_stems(self):
        self.assertEqual(tm.scene_stems(), ["hd002", "hd000", "hd131a", "hd132"])

    def test_videos(self):
        (self.movies / "hd002.mp4").write_bytes(scrambled(PLAIN))  # as the collection ships them
        (self.movies / "hd000.mp4").write_bytes(PLAIN)  # one shipped plain (the player's hd129)
        (self.movies / "hd818.mp4").write_bytes(scrambled(PLAIN))
        (self.movies / "DOP.mp4").write_bytes(scrambled(PLAIN))
        (self.movies / "hd131a.mp4").write_bytes(b"not a video at all, scrambled or not")
        made = tm.videos(self.movies, force=False)
        self.assertEqual(made, 4)  # hd002, hd000, hd818 and the opening; hd131a refused, hd132 absent
        for name in ("hd002.mp4", "hd000.mp4", "hd818.mp4", "hd802.mp4"):
            self.assertEqual((tm.OUT / name).read_bytes(), PLAIN, name)
        self.assertFalse((tm.OUT / "hd131a.mp4").exists())
        # kept unless forced
        (tm.OUT / "hd002.mp4").write_bytes(b"the player's own")
        self.assertEqual(tm.videos(self.movies, force=False), 0)
        self.assertEqual((tm.OUT / "hd002.mp4").read_bytes(), b"the player's own")
        self.assertEqual(tm.videos(self.movies, force=True), 4)
        self.assertEqual((tm.OUT / "hd002.mp4").read_bytes(), PLAIN)


if __name__ == "__main__":
    unittest.main()
