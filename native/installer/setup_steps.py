"""The installer's helper: each step the installer cannot do by itself, run with the Python the
installer carries.

  python -I setup_steps.py <command> [arguments] --result <file.json> [--language es|en|fr|de|it]

Every command writes its result to --result as one JSON object and exits 0 on success, 1 on a
refusal the player can fix (a wrong file, a missing folder), 2 on a failure (a bug, a full disk).
A result's `message` is in the language asked for. Progress goes to the standard output as lines
`progress <done> <total> <words>` for the installer's page.

What a step makes is written under a `.partial` name and renamed when it is whole, so an
interrupted install never leaves anything half made in use; `setup/state.json` records the steps
done, so a rerun skips them or redoes the one left unfinished.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import traceback
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Callable, Iterator

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # its own modules (run with -I, the script's folder is not on the path)
LANGUAGES = ("en", "es", "fr", "de", "it")

OK, REFUSED, FAILED = 0, 1, 2


class Refusal(Exception):
    """Something the player can put right: its message key and the values it names."""

    def __init__(self, key: str, **values: Any) -> None:
        super().__init__(key)
        self.key = key
        self.values = values


# The helper's own words, in the installer's five languages.
MESSAGES: dict[str, dict[str, str]] = {
    "rom-supported": {
        "en": "This is the ROM the port needs: {name}.",
        "es": "Es la ROM que necesita el port: {name}.",
        "fr": "C'est la ROM dont le portage a besoin : {name}.",
        "de": "Das ist das ROM, das die Portierung braucht: {name}.",
        "it": "È la ROM che serve al port: {name}.",
    },
    "rom-wrong": {
        "en": "This file is not the ROM the port needs. It needs your own dump of {names}.",
        "es": "Este archivo no es la ROM que necesita el port. Necesita tu propio volcado de {names}.",
        "fr": "Ce fichier n'est pas la ROM dont le portage a besoin. Il lui faut votre propre copie de {names}.",
        "de": "Diese Datei ist nicht das ROM, das die Portierung braucht. Sie braucht deine eigene Kopie von {names}.",
        "it": "Questo file non è la ROM che serve al port. Serve la tua copia di {names}.",
    },
    "rom-missing": {
        "en": "There is no file at {path}.",
        "es": "No hay ningún archivo en {path}.",
        "fr": "Il n'y a aucun fichier à {path}.",
        "de": "Unter {path} gibt es keine Datei.",
        "it": "Non c'è nessun file in {path}.",
    },
    "data-ready": {
        "en": "The game's data is ready.",
        "es": "Los datos del juego están listos.",
        "fr": "Les données du jeu sont prêtes.",
        "de": "Die Daten des Spiels sind bereit.",
        "it": "I dati del gioco sono pronti.",
    },
    "preparing-data": {
        "en": "Preparing the game's data from the ROM",
        "es": "Preparando los datos del juego a partir de la ROM",
        "fr": "Préparation des données du jeu à partir de la ROM",
        "de": "Die Daten des Spiels werden aus dem ROM vorbereitet",
        "it": "Preparazione dei dati del gioco dalla ROM",
    },
    "failed": {
        "en": "Something went wrong: {what}",
        "es": "Algo ha fallado: {what}",
        "fr": "Une erreur s'est produite : {what}",
        "de": "Etwas ist schiefgegangen: {what}",
        "it": "Qualcosa è andato storto: {what}",
    },
}


def words(key: str, language: str, **values: Any) -> str:
    texts = MESSAGES.get(key, {})
    text = texts.get(language) or texts.get("en") or key
    return text.format(**values)


def progress(done: int, total: int, what: str) -> None:
    print(f"progress {done} {total} {what}", flush=True)


# ---- Where the tools are -----------------------------------------------------------------------

def tool_folders() -> dict[str, Path]:
    """The repository's tools as the helper runs them: in the repository (native/installer/ beside
    tools/ and native/tools/) or in an install (setup/installer/ beside setup/tools/)."""
    repository = HERE.parent.parent
    if (repository / "tools" / "verify_rom").is_dir():
        return {"rom": repository / "tools", "hd": repository / "native" / "tools"}
    installed = HERE.parent / "tools"
    return {"rom": installed, "hd": installed / "hd"}


# ---- Whole or absent ---------------------------------------------------------------------------

def partial_name(final: Path) -> Path:
    return final.with_name(final.name + ".partial")


def remove(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


@contextmanager
def making(final: Path) -> Iterator[Path]:
    """The place to make `final` in: its `.partial` name, emptied first; renamed to `final` (an
    older one replaced) only when the block ends without an error, else left to be redone."""
    work = partial_name(final)
    remove(work)
    final.parent.mkdir(parents=True, exist_ok=True)
    yield work
    if final.exists():
        old = final.with_name(final.name + ".old")
        remove(old)
        os.replace(final, old)
        os.replace(work, final)
        remove(old)
    else:
        os.replace(work, final)


# ---- The install's state -----------------------------------------------------------------------

def state_path(install: Path) -> Path:
    return install / "setup" / "state.json"


def load_state(install: Path) -> dict[str, Any]:
    path = state_path(install)
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_state(install: Path, state: dict[str, Any]) -> None:
    path = state_path(install)
    path.parent.mkdir(parents=True, exist_ok=True)
    work = partial_name(path)
    work.write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(work, path)


def step_done(state: dict[str, Any], step: str, inputs: dict[str, Any]) -> bool:
    """Whether `step` is complete for these same inputs: a rerun skips it then, and redoes it when
    it is not complete or its inputs changed (another ROM, another collection folder)."""
    record = state.get("steps", {}).get(step)
    return bool(record) and record.get("complete") is True and record.get("inputs") == inputs


def mark_step(state: dict[str, Any], step: str, inputs: dict[str, Any], **more: Any) -> None:
    state.setdefault("steps", {})[step] = {"complete": True, "inputs": inputs, **more}


# ---- The commands ------------------------------------------------------------------------------

Command = Callable[[argparse.Namespace], dict[str, Any]]
COMMANDS: dict[str, tuple[Command, Callable[[argparse.ArgumentParser], None], str]] = {}


def command(name: str, help_text: str, arguments: Callable[[argparse.ArgumentParser], None]):
    def register(fn: Command) -> Command:
        COMMANDS[name] = (fn, arguments, help_text)
        return fn
    return register


def load_tool(folder: Path, name: str):
    """One of the repository's tools as a module (its functions, not its command line)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, folder / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def rom_check(rom: Path) -> dict[str, Any]:
    """The ROM's SHA-256 against the supported list (tools/verify_rom): its entry, or a refusal."""
    folder = tool_folders()["rom"] / "verify_rom"
    verify = load_tool(folder, "verify_rom")
    roms = verify.load_database(folder / "supported_roms.json")
    if not rom.is_file():
        raise Refusal("rom-missing", path=str(rom))
    try:
        info = verify.read_rom_info(rom)
    except (OSError, ValueError):
        info = None  # too small or unreadable: not the ROM either
    entry = verify.find_supported_rom(info, roms) if info is not None else None
    if entry is None:
        raise Refusal("rom-wrong", names=", ".join(r.get("name", "?") for r in roms))
    return {"name": entry.get("name", ""), "region": info.region, "sha256": info.sha256}


def _rom_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("rom", type=Path)


@command("verify-rom", "whether a file is the ROM the port supports (exit 1 when not)", _rom_arguments)
def verify_rom(args: argparse.Namespace) -> dict[str, Any]:
    found = rom_check(args.rom)
    return {"supported": True, **found, "message": words("rom-supported", args.language, name=found["name"])}


@command("find-rom", "a supported ROM already on this computer where KH Melon Mix keeps a player's own",
         lambda p: p.add_argument("folders", type=Path, nargs="*",
                                  help="KINGDOM HEARTS HD 1.5+2.5 ReMIX folders (default: Steam's)"))
def find_rom(args: argparse.Namespace) -> dict[str, Any]:
    folders = list(args.folders)
    if not folders:
        import steam_library
        found = steam_library.app_folder(steam_library.APP_1525)
        folders = [found] if found is not None else []
    for folder in folders:
        candidate = folder / "Image" / "melon" / "roms" / "days.nds"
        if candidate.is_file():
            try:
                checked = rom_check(candidate)
            except Refusal:
                continue
            return {"found": str(candidate.resolve()), **checked}
    return {"found": None}


def extracted_root(install: Path) -> Path:
    return install / "data" / "extracted"


@command("prepare-data", "the game's data prepared from the ROM (tools/extract_data)",
         lambda p: (p.add_argument("rom", type=Path), p.add_argument("install", type=Path)))
def prepare_data(args: argparse.Namespace) -> dict[str, Any]:
    install = args.install.resolve()
    checked = rom_check(args.rom)
    name = checked["sha256"][:16]  # the extraction's own folder name (extract_data.py)
    final = extracted_root(install) / name
    state = load_state(install)
    inputs = {"rom": checked["sha256"]}
    if step_done(state, "data", inputs) and (final / "nitrofs").is_dir():
        return {"data": str(final), "skipped": True, "message": words("data-ready", args.language)}
    progress(0, 1, words("preparing-data", args.language))
    folder = tool_folders()["rom"]
    extract = load_tool(folder / "extract_data", "extract_data")
    with making(final) as work:
        # the tool writes <root>/<name>: its root is the .partial folder, its output moved up after
        out = extract.extract_rom(rom_path=args.rom, output_root=work, database_path=folder / "verify_rom" /
                                  "supported_roms.json", allow_unknown=False, force=True)
        inner = work.with_name(work.name + ".inner")
        remove(inner)
        os.replace(out, inner)
        remove(work)
        os.replace(inner, work)
    size = sum(p.stat().st_size for p in final.rglob("*") if p.is_file())
    mark_step(state, "data", inputs, folder=str(final.relative_to(install)))
    save_state(install, state)
    progress(1, 1, words("data-ready", args.language))
    return {"data": str(final), "bytes": size, "message": words("data-ready", args.language)}


@command("remove-user-files", "the player's saves and settings moved to the Recycle Bin (on uninstall)",
         lambda p: p.add_argument("install", type=Path))
def remove_user_files(args: argparse.Namespace) -> dict[str, Any]:
    saves = args.install.resolve() / "saves"
    moved = []
    if saves.exists():
        to_recycle_bin(saves)
        moved.append(str(saves))
    return {"moved": moved}


def to_recycle_bin(path: Path) -> None:
    """The shell's undoable delete: `path` goes to the Recycle Bin, never deleted outright."""
    import ctypes
    from ctypes import wintypes

    class SHFILEOPSTRUCTW(ctypes.Structure):
        _fields_ = [("hwnd", wintypes.HWND), ("wFunc", wintypes.UINT), ("pFrom", wintypes.LPCWSTR),
                    ("pTo", wintypes.LPCWSTR), ("fFlags", ctypes.c_ushort), ("fAnyOperationsAborted", wintypes.BOOL),
                    ("hNameMappings", ctypes.c_void_p), ("lpszProgressTitle", wintypes.LPCWSTR)]

    FO_DELETE, FOF_ALLOWUNDO, FOF_NOCONFIRMATION, FOF_SILENT, FOF_NOERRORUI = 3, 0x40, 0x10, 0x4, 0x400
    op = SHFILEOPSTRUCTW(None, FO_DELETE, str(path) + "\0", None,
                         FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI, False, None, None)
    if ctypes.windll.shell32.SHFileOperationW(ctypes.byref(op)) != 0 or op.fAnyOperationsAborted:
        raise OSError(f"could not move {path} to the Recycle Bin")


def write_result(path: Path | None, result: dict[str, Any]) -> None:
    # ASCII only (other characters as \u escapes): what the installer's script reads back
    text = json.dumps(result, indent=2, ensure_ascii=True)
    if path is None:
        print(text)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    work = partial_name(path)
    work.write_text(text, encoding="utf-8")
    os.replace(work, path)


def main(argv: list[str] | None = None) -> int:
    # --result and --language before the command or after its arguments
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--result", type=Path, default=argparse.SUPPRESS,
                        help="the JSON file the result is written to")
    common.add_argument("--language", choices=LANGUAGES, default=argparse.SUPPRESS)
    # (the parent's actions are shared with the commands' parsers: their defaults stay unset)
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], parents=[common])
    sub = parser.add_subparsers(dest="command", required=True)
    for name, (_, arguments, help_text) in COMMANDS.items():
        arguments(sub.add_parser(name, help=help_text, parents=[common]))
    args = parser.parse_args(argv)
    args.result = getattr(args, "result", None)
    args.language = getattr(args, "language", "en")
    fn = COMMANDS[args.command][0]
    try:
        result = fn(args)
        code = OK
    except Refusal as refusal:
        result = {"message": words(refusal.key, args.language, **refusal.values), "refused": refusal.key}
        code = REFUSED
    except Exception as error:  # a failure: said, and its trace kept for a report
        result = {"message": words("failed", args.language, what=str(error) or type(error).__name__),
                  "trace": traceback.format_exc()}
        code = FAILED
    result.setdefault("ok", code == OK)
    write_result(args.result, result)
    return code


if __name__ == "__main__":
    sys.exit(main())
