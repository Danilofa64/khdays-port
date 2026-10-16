"""Add the installed port to Steam as a non-Steam game, and take it out again, while Steam is closed:
shortcuts.vdf changed only when its parse writes back the same bytes, the Proton mapping in
config.vdf edited as text, nothing else of Steam's touched (installer/README.md, "How it works")."""
from __future__ import annotations

import shutil
import sys
import zlib
from pathlib import Path
from typing import Any

# One field of a binary-VDF map: its type byte, its name, and its value -- bytes for a string
# (type 1), an int for an int32 (type 2), or a list of fields for a nested map (type 0).
Field = tuple[int, bytes, Any]

MAP, STRING, INT = 0x00, 0x01, 0x02
END = 0x08

APP_NAME = "KINGDOM HEARTS 358/2 Days"
EXE_NAME = "khdays-native.exe"


# ---- Where Steam keeps a user's shortcuts, and whether it is running ---------------------------

def user_configs(steam: Path) -> list[Path]:
    """Every local Steam user's `config` folder (userdata/<account>/config). The port's entry goes
    in each, since the installer does not know which account the player uses."""
    userdata = steam / "userdata"
    if not userdata.is_dir():
        return []
    return [account / "config" for account in sorted(userdata.iterdir())
            if account.is_dir() and account.name.isdigit() and account.name != "0"]


def under_wine() -> bool:
    """Whether this runs under Wine or Proton (a Steam Deck, Linux): the installer is then in a Wine
    prefix, whose Steam folder and registry are the prefix's own, not the computer's Steam -- an
    entry written there would reach nothing, so the Steam steps are left out."""
    if sys.platform != "win32":
        return False
    try:
        import ctypes
        return hasattr(ctypes.windll.ntdll, "wine_get_version")
    except OSError:
        return False


def steam_running() -> bool:
    """Whether Steam is running: its registry `ActiveProcess/pid` names a live process. Steam
    rewrites shortcuts.vdf and config.vdf when it closes, so a change made while it runs would be
    lost -- the installer closes Steam first. When it cannot be told, False (the installer closes
    Steam anyway before writing)."""
    if sys.platform != "win32":
        return False
    try:
        import ctypes
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam\ActiveProcess") as key:
            pid, _ = winreg.QueryValueEx(key, "pid")
    except OSError:
        return False
    if not pid:
        return False
    handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, int(pid))  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = ctypes.c_ulong()
    alive = ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code)) and code.value == 259  # STILL_ACTIVE
    ctypes.windll.kernel32.CloseHandle(handle)
    return bool(alive)


# ---- The binary KeyValues of shortcuts.vdf -----------------------------------------------------

def _parse(data: bytes, i: int) -> tuple[list[Field], int]:
    """The fields of one map from `data` at `i`, up to its `END`; the index after the `END`."""
    fields: list[Field] = []
    while True:
        kind = data[i]
        i += 1
        if kind == END:
            return fields, i
        end = data.index(0, i)
        name = data[i:end]
        i = end + 1
        if kind == MAP:
            value, i = _parse(data, i)
        elif kind == STRING:
            end = data.index(0, i)
            value = data[i:end]
            i = end + 1
        elif kind == INT:
            value = int.from_bytes(data[i:i + 4], "little")
            i += 4
        else:
            raise ValueError(f"unknown field type {kind} at {i - 1}")
        fields.append((kind, name, value))


def _serialise(fields: list[Field]) -> bytes:
    out = bytearray()
    for kind, name, value in fields:
        out.append(kind)
        out += name + b"\x00"
        if kind == MAP:
            out += _serialise(value) + bytes([END])
        elif kind == STRING:
            out += value + b"\x00"
        elif kind == INT:
            out += (int(value) & 0xFFFFFFFF).to_bytes(4, "little")
        else:
            raise ValueError(f"unknown field type {kind}")
    return out


HEADER = b"\x00shortcuts\x00"


def read_shortcuts(path: Path) -> tuple[list[Field], bytes]:
    """The entries of a `shortcuts.vdf` (each a nested map named "0", "1", ...), and the bytes
    after the root map's close (an empty file is made from nothing). Raises if the file does not
    serialise back to itself byte for byte -- then it is left untouched."""
    try:
        data = path.read_bytes()
    except FileNotFoundError:
        return [], b""
    if not data.startswith(HEADER):
        raise ValueError("not a shortcuts.vdf")
    entries, i = _parse(data, len(HEADER))
    tail = data[i:]
    if write_shortcuts_bytes(entries, tail) != data:
        raise ValueError("this shortcuts.vdf does not round-trip; left untouched")
    return entries, tail


def write_shortcuts_bytes(entries: list[Field], tail: bytes) -> bytes:
    return HEADER + _serialise(entries) + bytes([END]) + tail


def _int_field(fields: list[Field], name: bytes) -> int | None:
    for kind, n, value in fields:
        if n == name and kind == INT:
            return value
    return None


def _str_field(fields: list[Field], name: bytes) -> bytes | None:
    for kind, n, value in fields:
        if n == name and kind == STRING:
            return value
    return None


def shortcut_appid(exe_quoted: str, app_name: str) -> int:
    """A stable 32-bit id for the port's entry (high bit set, as Steam's own shortcut ids are);
    Steam keeps an entry's `appid`, so this same number keys the Proton mapping and the removal."""
    return zlib.crc32((exe_quoted + app_name).encode("utf-8")) | 0x80000000


def port_entry(index: int, exe_path: Path, app_name: str = APP_NAME) -> tuple[Field, int]:
    """The port's shortcut entry named `index`, and its appid. `exe_path` is the installed
    executable; Steam stores the command quoted."""
    exe = str(exe_path)
    quoted = f'"{exe}"'
    start_dir = str(exe_path.parent)
    appid = shortcut_appid(quoted, app_name)
    fields: list[Field] = [
        (INT, b"appid", appid),
        (STRING, b"appname", app_name.encode("utf-8")),
        (STRING, b"Exe", quoted.encode("utf-8")),
        (STRING, b"StartDir", (start_dir + "\\").encode("utf-8")),
        (STRING, b"icon", exe.encode("utf-8")),
        (STRING, b"ShortcutPath", b""),
        (STRING, b"LaunchOptions", b""),
        (INT, b"IsHidden", 0),
        (INT, b"AllowDesktopConfig", 1),
        (INT, b"AllowOverlay", 1),
        (INT, b"OpenVR", 0),
        (INT, b"Devkit", 0),
        (STRING, b"DevkitGameID", b""),
        (INT, b"DevkitOverrideAppID", 0),
        (INT, b"LastPlayTime", 0),
        (STRING, b"FlatpakAppID", b""),
        (MAP, b"tags", []),
    ]
    return (MAP, str(index).encode("ascii"), fields), appid


def port_present(entries: list[Field], exe_path: Path) -> bool:
    """Whether the port's entry (by its installed executable) is already among `entries`."""
    return any(_is_port_entry(e, exe_path) for e in entries)


def _is_port_entry(entry: Field, exe_path: Path) -> bool:
    """Whether a shortcut entry is the port's: its Exe names this installed executable (the match
    for removal, so an entry Steam renumbered or renamed is still found)."""
    _, _, fields = entry
    exe = _str_field(fields, b"Exe")
    if exe is None:
        return False
    want = str(exe_path).casefold()
    return exe.decode("utf-8", "replace").strip('"').casefold() == want


def set_port_shortcut(entries: list[Field], exe_path: Path, app_name: str = APP_NAME) -> int:
    """Add or replace the port's entry in `entries` (in place); returns its appid. Entries are
    renamed to a gap-free "0", "1", ... as Steam expects."""
    kept = [e for e in entries if not _is_port_entry(e, exe_path)]
    entry, appid = port_entry(len(kept), exe_path, app_name)
    kept.append(entry)
    _renumber(kept)
    entries[:] = kept
    return appid


def remove_port_shortcut(entries: list[Field], exe_path: Path) -> int:
    """Remove the port's entries from `entries` (in place); returns how many were removed."""
    kept = [e for e in entries if not _is_port_entry(e, exe_path)]
    removed = len(entries) - len(kept)
    _renumber(kept)
    entries[:] = kept
    return removed


def _renumber(entries: list[Field]) -> None:
    for i, (kind, _, value) in enumerate(entries):
        entries[i] = (kind, str(i).encode("ascii"), value)


def write_shortcuts(path: Path, entries: list[Field], tail: bytes) -> None:
    """Write `entries` to `path`, keeping a one-time backup of the first file replaced and writing
    atomically (a `.partial`, then a rename)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        backup = path.with_name(path.name + ".khdays-backup")
        if not backup.exists():
            shutil.copy2(path, backup)
    work = path.with_name(path.name + ".partial")
    work.write_bytes(write_shortcuts_bytes(entries, tail))
    work.replace(path)


# ---- The Proton mapping in config.vdf (a Windows game on Linux / a Steam Deck) ------------------
# The port's entry is inserted into or removed from CompatToolMapping's block as text, so every other
# byte stays as Steam wrote it; only when the block exists (the player has run a game under Proton).


def _quoted(text: str, key: str, start: int = 0) -> int:
    """The index of the token `"key"` at or after `start`, or -1."""
    return text.find(f'"{key}"', start)


def _block(text: str, i: int) -> tuple[int, int]:
    """Given the index of a key, the indices of its block's `{` and matching `}` (quotes aside)."""
    open_brace = text.index("{", i)
    depth = 0
    j = open_brace
    in_string = False
    while j < len(text):
        c = text[j]
        if c == '"' and text[j - 1] != "\\":
            in_string = not in_string
        elif not in_string:
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return open_brace, j
        j += 1
    raise ValueError("unbalanced braces")


def _mapping_block(text: str) -> tuple[int, int]:
    i = _quoted(text, "CompatToolMapping")
    if i < 0:
        raise ValueError("no CompatToolMapping in config.vdf")
    return _block(text, i)


def read_compat_tool(text: str, app_id: int) -> str | None:
    """The Proton tool mapped to `app_id` in config.vdf (to reuse the collection's), or None."""
    try:
        open_brace, close = _mapping_block(text)
    except ValueError:
        return None
    key = _quoted(text, str(app_id), open_brace)
    if key < 0 or key > close:
        return None
    _, entry_close = _block(text, key)
    name = _quoted(text, "name", key)
    if name < 0 or name > entry_close:
        return None
    value_start = text.index('"', name + len('"name"')) + 1  # the opening quote of the value
    value_end = text.index('"', value_start)
    return text[value_start:value_end]


def set_compat_tool(text: str, app_id: int, name: str) -> str:
    """config.vdf with `app_id` mapped to Proton `name` (added, or its name replaced). Raises if
    there is no CompatToolMapping block to add it to."""
    open_brace, close = _mapping_block(text)
    key = _quoted(text, str(app_id), open_brace)
    if 0 <= key < close:  # already mapped: take it out, then add it fresh (same shape either way)
        text = remove_compat_tool(text, app_id)
        open_brace, close = _mapping_block(text)
    indent = _entry_indent(text, open_brace)
    block = _entry_text(app_id, name, indent, indent + "\t")
    after_open = text.index("\n", open_brace) + 1  # insert as the block's first entry
    return text[:after_open] + block + text[after_open:]


def remove_compat_tool(text: str, app_id: int) -> str:
    """config.vdf without `app_id`'s mapping (unchanged if it has none)."""
    try:
        open_brace, close = _mapping_block(text)
    except ValueError:
        return text
    key = _quoted(text, str(app_id), open_brace)
    if key < 0 or key > close:
        return text
    _, entry_close = _block(text, key)
    line_start = text.rfind("\n", 0, key) + 1
    line_end = text.index("\n", entry_close) + 1 if "\n" in text[entry_close:] else len(text)
    return text[:line_start] + text[line_end:]


def _entry_indent(text: str, open_brace: int) -> str:
    """The indentation an entry inside the mapping block should take: one tab past the block's own
    key line."""
    line_start = text.rfind("\n", 0, open_brace) + 1
    key_line = text[line_start:open_brace]
    return key_line[:len(key_line) - len(key_line.lstrip("\t"))] + "\t"


def _entry_text(app_id: int, name: str, indent: str, inner: str) -> str:
    return (f'{indent}"{app_id}"\n{indent}{{\n'
            f'{inner}"name"\t\t"{name}"\n'
            f'{inner}"config"\t\t""\n'
            f'{inner}"priority"\t\t"250"\n'
            f'{indent}}}\n')
