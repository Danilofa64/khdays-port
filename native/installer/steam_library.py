"""Where Steam keeps the KINGDOM HEARTS collections on this computer: Steam's own folder, its
libraries (`steamapps/libraryfolders.vdf`) and each app's install folder (`appmanifest_<id>.acf`).
Read only."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Any

APP_1525 = 2552430  # KINGDOM HEARTS -HD 1.5+2.5 ReMIX-
APP_28 = 2552440    # KINGDOM HEARTS HD 2.8 Final Chapter Prologue

_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([{}])')


def parse_vdf(text: str) -> dict[str, Any]:
    """Valve's text KeyValues: quoted keys followed by a quoted value or a braced block."""
    tokens = [(m.group(1), m.group(2)) for m in _TOKEN.finditer(text)]
    pos = 0

    def block() -> dict[str, Any]:
        nonlocal pos
        out: dict[str, Any] = {}
        while pos < len(tokens):
            key, brace = tokens[pos]
            if brace == "}":
                pos += 1
                return out
            pos += 1
            if pos >= len(tokens):
                break
            value, open_brace = tokens[pos]
            pos += 1
            if open_brace == "{":
                out[key] = block()
            else:
                out[key] = value.replace("\\\\", "\\")
        return out

    return block()


def steam_folder() -> Path | None:
    """Steam's own folder: the registry's SteamPath on Windows."""
    if sys.platform != "win32":
        return None
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            value, _ = winreg.QueryValueEx(key, "SteamPath")
    except OSError:
        return None
    folder = Path(value)
    return folder if folder.is_dir() else None


def libraries(steam: Path) -> list[Path]:
    """Every Steam library: the libraries file's paths, Steam's own folder first."""
    found = [steam]
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    try:
        data = parse_vdf(vdf.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        return found
    for entry in data.get("libraryfolders", {}).values():
        if isinstance(entry, dict) and entry.get("path"):
            path = Path(entry["path"])
            if path.is_dir() and all(path.resolve() != f.resolve() for f in found):
                found.append(path)
    return found


def app_folder(app_id: int, steam: Path | None = None) -> Path | None:
    """The install folder of Steam app `app_id`, from its manifest in whichever library has it."""
    steam = steam or steam_folder()
    if steam is None:
        return None
    for library in libraries(steam):
        manifest = library / "steamapps" / f"appmanifest_{app_id}.acf"
        try:
            data = parse_vdf(manifest.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        installdir = data.get("AppState", {}).get("installdir")
        if installdir:
            folder = library / "steamapps" / "common" / installdir
            if folder.is_dir():
                return folder
    return None
