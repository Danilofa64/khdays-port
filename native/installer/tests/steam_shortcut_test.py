"""The Steam non-Steam-game writer (steam_shortcut.py) on synthetic data: a shortcuts.vdf with
other games in it parses and serialises back byte for byte; adding the port keeps the other games,
is idempotent, and removing it restores the original bytes; a file it cannot reproduce is left
untouched; and the Proton mapping in a config.vdf is added and removed without disturbing the rest.
No real Steam is touched (the collection's own files are never in the repository)."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import steam_shortcut as ss  # noqa: E402

MAP, STRING, INT, END = ss.MAP, ss.STRING, ss.INT, ss.END


def other_game(index, name, exe):
    """A shortcut entry like Steam writes for another non-Steam game (a nested tags map and all)."""
    fields = [
        (INT, b"appid", 0xA1B2C3D4),
        (STRING, b"appname", name.encode()),
        (STRING, b"Exe", f'"{exe}"'.encode()),
        (STRING, b"StartDir", b'"C:\\Games\\"'),
        (STRING, b"icon", b""),
        (INT, b"IsHidden", 0),
        (MAP, b"tags", [(STRING, b"0", b"favourite")]),
    ]
    return (MAP, str(index).encode(), fields)


def shortcuts_bytes(entries, tail=b"\x08"):
    """A whole shortcuts.vdf; `tail` is the quirk seen in real files (an extra close byte)."""
    return ss.write_shortcuts_bytes(entries, tail)


class BinaryVdf(unittest.TestCase):
    def test_round_trip_with_other_games_and_tail(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "shortcuts.vdf"
            raw = shortcuts_bytes([other_game(0, "Celeste", "C:\\Games\\celeste.exe"),
                                   other_game(1, "Hades", "C:\\Games\\hades.exe")], tail=b"\x08")
            path.write_bytes(raw)
            entries, tail = ss.read_shortcuts(path)  # raises if it does not round-trip
            self.assertEqual(len(entries), 2)
            self.assertEqual(tail, b"\x08")

    def test_file_that_does_not_round_trip_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "shortcuts.vdf"
            path.write_bytes(ss.HEADER + b"\x07bad\x00\x08\x08")  # unknown field type 0x07
            with self.assertRaises(ValueError):
                ss.read_shortcuts(path)

    def test_missing_file_is_empty(self):
        entries, tail = ss.read_shortcuts(Path(tempfile.gettempdir()) / "khdays-no-such.vdf")
        self.assertEqual((entries, tail), ([], b""))


class AddRemove(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = Path(self.dir.name) / "shortcuts.vdf"
        self.original = shortcuts_bytes([other_game(0, "Celeste", "C:\\Games\\celeste.exe")], tail=b"\x08")
        self.path.write_bytes(self.original)
        self.exe = Path(r"C:\Program Files\KH 358-2 Days Port\khdays-native.exe")

    def tearDown(self):
        self.dir.cleanup()

    def test_add_keeps_other_games_and_is_idempotent(self):
        entries, tail = ss.read_shortcuts(self.path)
        appid1 = ss.set_port_shortcut(entries, self.exe)
        ss.write_shortcuts(self.path, entries, tail)
        self.assertTrue((self.path.with_name("shortcuts.vdf.khdays-backup")).is_file())

        entries, tail = ss.read_shortcuts(self.path)
        names = [ss._str_field(e[2], b"appname") for e in entries]
        self.assertIn(b"Celeste", names)
        self.assertIn(ss.APP_NAME.encode(), names)
        self.assertTrue(ss.port_present(entries, self.exe))

        appid2 = ss.set_port_shortcut(entries, self.exe)  # again
        self.assertEqual(appid1, appid2)
        self.assertEqual(sum(ss._is_port_entry(e, self.exe) for e in entries), 1)
        self.assertEqual([e[1] for e in entries], [b"0", b"1"])  # gap-free indices

    def test_remove_restores_original_bytes(self):
        entries, tail = ss.read_shortcuts(self.path)
        ss.set_port_shortcut(entries, self.exe)
        ss.write_shortcuts(self.path, entries, tail)
        entries, tail = ss.read_shortcuts(self.path)
        removed = ss.remove_port_shortcut(entries, self.exe)
        ss.write_shortcuts(self.path, entries, tail)
        self.assertEqual(removed, 1)
        self.assertEqual(self.path.read_bytes(), self.original)

    def test_appid_stable_and_high_bit(self):
        appid = ss.shortcut_appid('"C:\\x\\khdays-native.exe"', ss.APP_NAME)
        self.assertEqual(appid & 0x80000000, 0x80000000)
        self.assertEqual(appid, ss.shortcut_appid('"C:\\x\\khdays-native.exe"', ss.APP_NAME))


CONFIG = (
    '"InstallConfigStore"\n{\n\t"Software"\n\t{\n\t\t"Valve"\n\t\t{\n\t\t\t"Steam"\n\t\t\t{\n'
    '\t\t\t\t"CompatToolMapping"\n\t\t\t\t{\n'
    '\t\t\t\t\t"2552430"\n\t\t\t\t\t{\n\t\t\t\t\t\t"name"\t\t"proton_9"\n'
    '\t\t\t\t\t\t"config"\t\t""\n\t\t\t\t\t\t"priority"\t\t"250"\n\t\t\t\t\t}\n'
    '\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n')


class CompatMapping(unittest.TestCase):
    APP = 0xC2F7D56A

    def test_read_collection_tool(self):
        self.assertEqual(ss.read_compat_tool(CONFIG, 2552430), "proton_9")
        self.assertIsNone(ss.read_compat_tool(CONFIG, 999999))

    def test_add_then_remove_restores(self):
        added = ss.set_compat_tool(CONFIG, self.APP, "proton_9")
        self.assertEqual(ss.read_compat_tool(added, self.APP), "proton_9")
        self.assertEqual(ss.read_compat_tool(added, 2552430), "proton_9")  # collection untouched
        self.assertEqual(ss.remove_compat_tool(added, self.APP), CONFIG)

    def test_into_empty_block_and_back(self):
        empty = CONFIG.replace(
            '\t\t\t\t\t"2552430"\n\t\t\t\t\t{\n\t\t\t\t\t\t"name"\t\t"proton_9"\n'
            '\t\t\t\t\t\t"config"\t\t""\n\t\t\t\t\t\t"priority"\t\t"250"\n\t\t\t\t\t}\n', '')
        added = ss.set_compat_tool(empty, self.APP, "proton_9")
        self.assertEqual(ss.read_compat_tool(added, self.APP), "proton_9")
        self.assertEqual(ss.remove_compat_tool(added, self.APP), empty)

    def test_replace_keeps_one_and_removes_clean(self):
        added = ss.set_compat_tool(CONFIG, self.APP, "proton_9")
        replaced = ss.set_compat_tool(added, self.APP, "proton_experimental")
        self.assertEqual(ss.read_compat_tool(replaced, self.APP), "proton_experimental")
        self.assertEqual(replaced.count(str(self.APP)), 1)
        self.assertEqual(ss.remove_compat_tool(replaced, self.APP), CONFIG)

    def test_no_mapping_block(self):
        bare = '"InstallConfigStore"\n{\n\t"Software"\n\t{\n\t}\n}\n'
        self.assertIsNone(ss.read_compat_tool(bare, self.APP))
        self.assertEqual(ss.remove_compat_tool(bare, self.APP), bare)
        with self.assertRaises(ValueError):
            ss.set_compat_tool(bare, self.APP, "proton_9")


class Commands(unittest.TestCase):
    """add-to-steam / remove-from-steam through the helper, on a made-up Steam folder (two users,
    the collection already mapped to Proton as on a Steam Deck)."""

    def setUp(self):
        import argparse
        import steam_library
        import setup_steps
        self.dir = tempfile.TemporaryDirectory()
        root = Path(self.dir.name)
        self.steam = root / "steam"
        for account in ("0", "12345", "67890"):
            (self.steam / "userdata" / account / "config").mkdir(parents=True)
        (self.steam / "config").mkdir()
        (self.steam / "config" / "config.vdf").write_text(CONFIG, encoding="utf-8")
        self.install = root / "install"
        (self.install / "setup").mkdir(parents=True)
        (self.install / ss.EXE_NAME).write_bytes(b"MZ")
        self.saved = (steam_library.steam_folder, ss.steam_running)
        steam_library.steam_folder = lambda: self.steam
        ss.steam_running = lambda: False
        self.lib, self.steps, self.argparse = steam_library, setup_steps, argparse

    def tearDown(self):
        self.lib.steam_folder, ss.steam_running = self.saved
        self.dir.cleanup()

    def run_command(self, name):
        args = self.argparse.Namespace(install=self.install, language="en", result=None,
                                       proton=None, proton_from_collection=True)
        return self.steps.COMMANDS[name][0](args)

    def test_add_then_remove(self):
        added = self.run_command("add-to-steam")
        self.assertEqual(added["written"], 2)  # the users, not "0"
        self.assertEqual(added["proton"], "proton_9")  # the collection's own
        self.assertTrue(self.run_command("steam-status")["present"])
        (self.install / "setup" / "state.json").unlink()  # removal does not need the install's state
        self.assertEqual(self.run_command("remove-from-steam")["removed"], 2)
        self.assertFalse(self.run_command("steam-status")["present"])
        self.assertEqual((self.steam / "config" / "config.vdf").read_text(encoding="utf-8"), CONFIG)

    def test_refused_while_steam_runs(self):
        ss.steam_running = lambda: True
        with self.assertRaises(self.steps.Refusal) as refused:
            self.run_command("add-to-steam")
        self.assertEqual(refused.exception.key, "steam-running")

    def test_nothing_to_remove_needs_no_closed_steam(self):
        # uninstalling a port never added to Steam must not ask to close a running Steam
        ss.steam_running = lambda: True
        self.assertEqual(self.run_command("remove-from-steam")["removed"], 0)

    def test_remove_while_running_is_refused_when_present(self):
        self.run_command("add-to-steam")
        ss.steam_running = lambda: True
        with self.assertRaises(self.steps.Refusal) as refused:
            self.run_command("remove-from-steam")
        self.assertEqual(refused.exception.key, "steam-running")


class Accounts(unittest.TestCase):
    def test_user_configs_skips_zero_and_non_digit(self):
        with tempfile.TemporaryDirectory() as d:
            steam = Path(d)
            for name in ("0", "29455668", "123456", "anonymous"):
                (steam / "userdata" / name / "config").mkdir(parents=True)
            configs = ss.user_configs(steam)
            names = sorted(c.parent.name for c in configs)
            self.assertEqual(names, ["123456", "29455668"])


if __name__ == "__main__":
    unittest.main()
