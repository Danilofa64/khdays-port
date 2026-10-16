from __future__ import annotations

import hashlib
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).resolve().parents[1] / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


verify_rom = load("verify_rom")
extract_data = load("extract_data")


class WholeDumpTests(unittest.TestCase):
    """A trimmed dump hashes as the whole dump it was cut from; any other file as it is."""

    def test_trimmed_dump(self) -> None:
        used = 0x400
        rom = bytearray(used)
        rom[0x0C:0x10] = b"TSTP"
        rom[0x14] = 0  # a 128 KiB cartridge
        rom[0x80:0x84] = used.to_bytes(4, "little")
        whole = bytes(rom) + b"\xff" * (0x20000 - used)
        with tempfile.TemporaryDirectory() as temporary:
            trimmed_path = Path(temporary) / "trimmed.nds"
            whole_path = Path(temporary) / "whole.nds"
            trimmed_path.write_bytes(rom)
            whole_path.write_bytes(whole)
            expected = hashlib.sha256(whole).hexdigest()
            for tool in (verify_rom, extract_data):
                self.assertEqual(tool.whole_dump_sha256(trimmed_path), expected)
                self.assertEqual(tool.whole_dump_sha256(whole_path), expected)
            self.assertEqual(verify_rom.read_rom_info(trimmed_path).sha256, expected)

    def test_other_files_as_they_are(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "short.nds"
            data = bytes(0x300)  # no used size in its header: not a trimmed dump
            path.write_bytes(data)
            for tool in (verify_rom, extract_data):
                self.assertEqual(tool.whole_dump_sha256(path), hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    unittest.main()
