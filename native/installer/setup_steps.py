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
import hashlib
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
    # (the decompressed view too: the HD tools read files there)
    inputs = {"rom": checked["sha256"], "decompressed": True}
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
        # the files the game keeps LZ-compressed, also as they are read (decompressed/)
        decompress = load_tool(folder / "decompress_data", "decompress_data")
        decompress.decompress_extraction(work)
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


# ---- The HD pieces -----------------------------------------------------------------------------

# The tools that make the maintainer's mods/HD PACK (the HD menus and HUD, both collections) and the
# theater's videos (1.5+2.5), each with its arguments: {mare}, {settings} the 1.5+2.5 theater's and
# settings menu's files (their original/ folder), {ddd} 2.8's kh3d_first (its original/ and
# remastered/), {hand} KH2's title sheet (kh2_first, remastered), {movies} the theater's videos.
HD_MENU_TOOLS = [
    ("theater_title.py", ["{mare}"]),
    ("theater_settings.py", ["{settings}"]),
    ("newgame_dialog.py", ["{ddd}", "{mare}", "{hand}"]),
    ("theater_mission.py", ["{mare}"]),
    ("theater_cine.py", ["{mare}"]),
    ("save_screen.py", ["{ddd}", "{mare}"]),
    ("ddd_camp.py", ["{ddd}", "{mare}", "{settings}"]),
]
HD_VIDEO_TOOLS = [
    ("theater_movies.py", ["{mare}", "--movies", "{movies}", "--videos", "--subtitles"]),
]
HD_MOD = "HD PACK"
HAND = "menu/fm/title.2ld"  # kh2_first's entry whose remastered sheet newgame_dialog takes the glove from
HAND_ASSET = "UK_title_2ld0.png"
INPUTS_FILE = HERE / "hd_inputs.txt"

# Run by the trace: the tool under an audit hook writing every file opened and folder listed.
_TRACER = r"""
import os, runpy, sys
log = open(os.environ["KHDAYS_TRACE_LOG"], "a", encoding="utf-8")
def hook(event, args):
    if event == "open" and isinstance(args[0], (str, bytes, os.PathLike)):
        log.write("open\t" + os.fsdecode(args[0]) + "\n")
    elif event in ("os.scandir", "os.listdir") and args and args[0] is not None:
        log.write("list\t" + os.fsdecode(args[0]) + "\n")
sys.addaudithook(hook)
tool = sys.argv[1]
sys.argv = sys.argv[1:]
runpy.run_path(tool, run_name="__main__")
"""


def tool_arguments(template: list[str], places: dict[str, Path]) -> list[str]:
    return [str(places[a[1:-1]]) if a.startswith("{") and a.endswith("}") else a for a in template]


def _inputs_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("--mare", type=Path, required=True, help="<1.5+2.5>/Image/Mare.hed_out/original (an extraction)")
    p.add_argument("--settings", type=Path, required=True, help="<1.5+2.5>/Image/SettingMenu.hed_out/original")
    p.add_argument("--ddd", type=Path, required=True, help="<2.8>/Image/dt/kh3d_first.hed_out")
    p.add_argument("--kh2", type=Path, required=True, help="<1.5+2.5>/Image/dt/kh2_first.hed_out")
    p.add_argument("--movies", type=Path, required=True, help="<1.5+2.5>/STEAM/Mare/MOVIE/Days/dt")
    p.add_argument("--data", type=Path, default=HERE.parent.parent / "data" / "extracted",
                   help="the game's extracted data")
    p.add_argument("--out", type=Path, default=INPUTS_FILE)


@command("trace-inputs", "developers: which collection files the HD tools read, traced into hd_inputs.txt",
         _inputs_arguments)
def trace_inputs(args: argparse.Namespace) -> dict[str, Any]:
    import subprocess
    import tempfile
    repository = HERE.parent.parent
    hand = args.kh2 / "remastered" / HAND / HAND_ASSET
    places = {"mare": args.mare, "settings": args.settings, "ddd": args.ddd, "hand": hand, "movies": args.movies}
    # the archives the sources are extractions of, and how a file there names its entry
    sources = [("Mare", args.mare, "original"), ("SettingMenu", args.settings, "original"),
               ("kh3d_first", args.ddd / "original", "original"), ("kh3d_first", args.ddd / "remastered", "remastered"),
               ("kh2_first", args.kh2 / "original", "original"), ("kh2_first", args.kh2 / "remastered", "remastered"),
               ("movies", args.movies, "plain")]
    entries: dict[str, set[str]] = {}
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        # the tools in the repository's layout, the game's data beside them (read only: a junction to
        # data/extracted alone, so nothing they write reaches the repository's data or mods)
        for rel in [f"native/tools/{t}" for t, _ in HD_MENU_TOOLS + HD_VIDEO_TOOLS] + \
                   ["tools/decompress_data/decompress_data.py"]:
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(repository / rel, root / rel)
        (root / "data").mkdir()
        subprocess.run(["cmd", "/c", "mklink", "/J", str(root / "data" / "extracted"), str(args.data.resolve())],
                       check=True, capture_output=True)
        log = root / "trace.log"
        try:
            for i, (tool, template) in enumerate(HD_MENU_TOOLS + HD_VIDEO_TOOLS):
                progress(i, len(HD_MENU_TOOLS) + len(HD_VIDEO_TOOLS), tool)
                command_line = [sys.executable, "-c", _TRACER, str(root / "native" / "tools" / tool),
                                *tool_arguments(template, places)]
                if (tool, template) in HD_MENU_TOOLS:
                    command_line += ["--mod", "trace"]
                subprocess.run(command_line, cwd=root, check=True, env={**os.environ, "KHDAYS_TRACE_LOG": str(log)})
            for line in log.read_text(encoding="utf-8").splitlines():
                kind, _, raw = line.partition("\t")
                path = Path(raw).resolve()
                for archive, folder, layout in sources:
                    folder = folder.resolve()
                    if path != folder and folder not in path.parents:
                        continue
                    files = [path] if kind == "open" else [p for p in path.rglob("*") if p.is_file()] if path.is_dir() else []
                    for f in files:
                        rel = f.relative_to(folder).as_posix()
                        if layout == "remastered":
                            rel = rel.rsplit("/", 1)[0]  # the asset's entry (its name follows it)
                            while rel and not (folder.parent / "original" / rel).is_file():
                                rel = rel.rsplit("/", 1)[0] if "/" in rel else ""
                            if not rel:
                                continue
                        entries.setdefault(archive, set()).add(rel)
                    break
        finally:
            subprocess.run(["cmd", "/c", "rmdir", str(root / "data" / "extracted")], capture_output=True)
    tools_digest = hashlib.sha256(b"".join((repository / "native" / "tools" / t).read_bytes()
                                           for t, _ in HD_MENU_TOOLS + HD_VIDEO_TOOLS)).hexdigest()
    lines = ["# The collection files the HD tools read (native/installer/setup_steps.py trace-inputs):",
             "# '<archive> <entry>' (its data and remastered assets) or 'movies <file>'.",
             f"# tools {tools_digest}"]
    for archive in sorted(entries):
        lines += [f"{archive} {rel}" for rel in sorted(entries[archive])]
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return {"inputs": sum(len(v) for v in entries.values()), "out": str(args.out)}


# The collections' places (from their Steam folder) and what each HD choice needs.
KH2_EXE = "KINGDOM HEARTS II FINAL MIX.exe"
COLLECTION_ARCHIVES = {
    "1.5+2.5": {"Mare": "Image/Mare", "SettingMenu": "Image/SettingMenu", "kh2_first": "Image/dt/kh2_first"},
    "2.8": {"kh3d_first": "Image/dt/kh3d_first"},
}
MOVIES = "STEAM/Mare/MOVIE/Days/dt"
DATA_TOOLS = {"theater_mission.py", "theater_cine.py", "save_screen.py", "ddd_camp.py"}  # take --data

MESSAGES.update({
    "hd-missing": {
        "en": "This folder lacks files the HD pieces need: {missing}",
        "es": "A esta carpeta le faltan archivos que necesitan las piezas HD: {missing}",
        "fr": "Il manque à ce dossier des fichiers dont les éléments HD ont besoin : {missing}",
        "de": "In diesem Ordner fehlen Dateien, die die HD-Teile brauchen: {missing}",
        "it": "In questa cartella mancano file che servono ai pezzi HD: {missing}",
    },
    "hd-needs-both": {
        "en": "The HD menus and HUD need both collections.",
        "es": "Los menús y el HUD HD necesitan las dos colecciones.",
        "fr": "Les menus et l'interface HD ont besoin des deux collections.",
        "de": "Die HD-Menüs und das HD-HUD brauchen beide Sammlungen.",
        "it": "I menu e l'HUD HD richiedono entrambe le raccolte.",
    },
    "hd-needs-1525": {
        "en": "The HD videos need KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
        "es": "Los vídeos HD necesitan KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
        "fr": "Les vidéos HD ont besoin de KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
        "de": "Die HD-Videos brauchen KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
        "it": "I video HD richiedono KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
    },
    "hd-making": {
        "en": "Making the HD pieces: {what}",
        "es": "Haciendo las piezas HD: {what}",
        "fr": "Création des éléments HD : {what}",
        "de": "Die HD-Teile werden erstellt: {what}",
        "it": "Creazione dei pezzi HD: {what}",
    },
    "hd-ready": {
        "en": "The HD pieces are ready.",
        "es": "Las piezas HD están listas.",
        "fr": "Les éléments HD sont prêts.",
        "de": "Die HD-Teile sind fertig.",
        "it": "I pezzi HD sono pronti.",
    },
})


def hd_inputs() -> dict[str, list[str]]:
    """hd_inputs.txt: archive -> its entries (or movies -> its files)."""
    out: dict[str, list[str]] = {}
    for line in INPUTS_FILE.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            archive, _, path = line.partition(" ")
            out.setdefault(archive, []).append(path)
    return out


def check_folder(kind: str, folder: Path) -> list[str]:
    """What `folder` lacks of what the HD pieces read from collection `kind` (empty: all there)."""
    import collection_archive
    inputs = hd_inputs()
    missing = []
    for archive, rel in COLLECTION_ARCHIVES[kind].items():
        base = folder / rel
        if not (base.with_name(base.name + ".hed").is_file() and base.with_name(base.name + ".pkg").is_file()):
            missing.append(rel + ".hed/.pkg")
            continue
        index = collection_archive.Archive(base, None)
        try:
            absent = [p for p in inputs.get(archive, []) if not index.has(p)]
        finally:
            index.close()
        if absent:
            missing.append(f"{rel}: {len(absent)} entries ({absent[0]}...)")
    if kind == "1.5+2.5":
        absent = [m for m in inputs.get("movies", []) if not (folder / MOVIES / m).is_file()]
        if absent:
            missing.append(f"{MOVIES}: {len(absent)} files ({absent[0]}...)")
        try:
            collection_archive.load_table(folder / KH2_EXE)
        except (OSError, collection_archive.ArchiveError):
            missing.append(KH2_EXE)
    return missing


@command("find-collections", "the KINGDOM HEARTS collections Steam has on this computer, each checked", lambda p: None)
def find_collections(args: argparse.Namespace) -> dict[str, Any]:
    import steam_library
    steam = steam_library.steam_folder()
    found = []
    for kind, app in (("1.5+2.5", steam_library.APP_1525), ("2.8", steam_library.APP_28)):
        folder = steam_library.app_folder(app, steam) if steam is not None else None
        missing = check_folder(kind, folder) if folder is not None else None
        found.append({"kind": kind, "folder": str(folder) if folder else None,
                      "valid": folder is not None and not missing, "missing": missing or []})
    flat = {}  # the same, one field each, for the installer's script
    for c, key in zip(found, ("1525", "28")):
        flat[f"folder_{key}"] = c["folder"] if c["valid"] else None
    return {"steam": str(steam) if steam else None, "collections": found, **flat}


@command("check-collection", "whether a folder holds what the HD pieces read from that collection (exit 1 when not)",
         lambda p: (p.add_argument("kind", choices=sorted(COLLECTION_ARCHIVES)), p.add_argument("folder", type=Path)))
def check_collection(args: argparse.Namespace) -> dict[str, Any]:
    missing = check_folder(args.kind, args.folder)
    if missing:
        raise Refusal("hd-missing", missing="; ".join(missing))
    return {"valid": True, "missing": []}


def data_folder(install: Path) -> Path:
    for folder in sorted(extracted_root(install).glob("*")):
        if folder.suffix != ".partial" and (folder / "nitrofs").is_dir():
            return folder
    raise Refusal("data-missing")


MESSAGES["data-missing"] = {
    "en": "The game's data is not prepared yet.",
    "es": "Los datos del juego aún no están preparados.",
    "fr": "Les données du jeu ne sont pas encore préparées.",
    "de": "Die Daten des Spiels sind noch nicht vorbereitet.",
    "it": "I dati del gioco non sono ancora pronti.",
}


def replace_folder(made: Path, final: Path) -> None:
    """`made` (whole) put at `final`, an older one there replaced."""
    final.parent.mkdir(parents=True, exist_ok=True)
    old = final.with_name(final.name + ".old")
    remove(old)
    if final.exists():
        os.replace(final, old)
    os.replace(made, final)
    remove(old)


def _build_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("install", type=Path)
    p.add_argument("--menus", action="store_true", help="the HD menus and HUD (mods/HD PACK): both collections")
    p.add_argument("--videos", action="store_true", help="the theater's videos and subtitles: 1.5+2.5")
    p.add_argument("--1525", dest="c1525", type=Path, help="the KINGDOM HEARTS HD 1.5+2.5 ReMIX folder")
    p.add_argument("--28", dest="c28", type=Path, help="the KINGDOM HEARTS HD 2.8 Final Chapter Prologue folder")


@command("build-hd", "the HD pieces made from the player's collections", _build_arguments)
def build_hd(args: argparse.Namespace) -> dict[str, Any]:
    import collection_archive
    import subprocess
    install = args.install.resolve()
    if args.menus and (args.c1525 is None or args.c28 is None):
        raise Refusal("hd-needs-both")
    if args.videos and args.c1525 is None:
        raise Refusal("hd-needs-1525")
    for kind, folder in (("1.5+2.5", args.c1525), ("2.8", args.c28)):
        if folder is not None and (args.menus or kind == "1.5+2.5"):
            missing = check_folder(kind, folder)
            if missing:
                raise Refusal("hd-missing", missing="; ".join(missing))
    data = data_folder(install)
    inputs = hd_inputs()
    work = install / "setup" / "hd-work"
    remove(work)
    root, src = work / "root", work / "src"
    pieces = []
    try:
        # the tools in the repository's layout under a root of their own: their outputs land there
        tools = tool_folders()
        for tool, _ in HD_MENU_TOOLS + HD_VIDEO_TOOLS:
            (root / "native" / "tools").mkdir(parents=True, exist_ok=True)
            shutil.copy2(tools["hd"] / tool, root / "native" / "tools" / tool)
        (root / "tools" / "decompress_data").mkdir(parents=True)
        shutil.copy2(tools["rom"] / "decompress_data" / "decompress_data.py", root / "tools" / "decompress_data")
        places = {"movies": args.c1525 / MOVIES if args.c1525 else Path(),
                  "mare": src / "Mare.hed_out" / "original", "settings": src / "SettingMenu.hed_out" / "original",
                  "ddd": src / "kh3d_first.hed_out",
                  "hand": src / "kh2_first.hed_out" / "remastered" / HAND / HAND_ASSET}
        # the collections' files the tools read, made from their archives
        wanted = {"Mare": args.c1525} if args.videos else {}
        if args.menus:
            wanted = {"Mare": args.c1525, "SettingMenu": args.c1525, "kh2_first": args.c1525, "kh3d_first": args.c28}
        cipher = collection_archive.Cipher(collection_archive.load_table(args.c1525 / KH2_EXE)) if args.menus else None
        for archive, folder in wanted.items():
            kind = "1.5+2.5" if archive in COLLECTION_ARCHIVES["1.5+2.5"] else "2.8"
            progress(0, 1, words("hd-making", args.language, what=archive))
            with collection_archive.Archive(folder / COLLECTION_ARCHIVES[kind][archive], cipher) as a:
                collection_archive.materialise(a, inputs.get(archive, []), src / f"{archive}.hed_out")
        runs = (HD_MENU_TOOLS if args.menus else []) + (HD_VIDEO_TOOLS if args.videos else [])
        for i, (tool, template) in enumerate(runs):
            progress(i, len(runs), words("hd-making", args.language, what=tool))
            # (the installed Python is isolated by its ._pth file; -I would hide a developer's own packages)
            line = [sys.executable, str(root / "native" / "tools" / tool), *tool_arguments(template, places)]
            if (tool, template) in HD_MENU_TOOLS:
                line += ["--mod", HD_MOD]
            if tool in DATA_TOOLS:
                line += ["--data", str(data)]
            done = subprocess.run(line, cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace")
            if done.returncode != 0:
                raise RuntimeError(f"{tool}: {done.stderr.strip().splitlines()[-1] if done.stderr.strip() else done.returncode}")
            pieces.append({"tool": tool, "made": True})
        # whole, into their places
        state = load_state(install)
        if args.menus:
            replace_folder(root / "mods" / HD_MOD, install / "mods" / HD_MOD)
            mark_step(state, "hd-menus", {"1.5+2.5": str(args.c1525), "2.8": str(args.c28)})
        if args.videos:
            replace_folder(root / "data" / "cutscenes", install / "data" / "cutscenes")
            mark_step(state, "hd-videos", {"1.5+2.5": str(args.c1525)})
        save_state(install, state)
    finally:
        remove(work)
    progress(1, 1, words("hd-ready", args.language))
    return {"pieces": pieces, "message": words("hd-ready", args.language)}


@command("compare-hd", "developers: two folders of HD pieces compared (pictures by their pixels, the rest by bytes)",
         lambda p: (p.add_argument("made", type=Path), p.add_argument("reference", type=Path)))
def compare_hd(args: argparse.Namespace) -> dict[str, Any]:
    from PIL import Image
    made = {p.relative_to(args.made).as_posix(): p for p in args.made.rglob("*") if p.is_file()}
    reference = {p.relative_to(args.reference).as_posix(): p for p in args.reference.rglob("*") if p.is_file()}
    differences = []
    for rel in sorted(set(made) | set(reference)):
        if rel not in made or rel not in reference:
            differences.append({"file": rel, "why": "only in the made pieces" if rel in made else "missing"})
            continue
        a, b = made[rel], reference[rel]
        if a.read_bytes() == b.read_bytes():
            continue
        if a.suffix.lower() == ".png":
            with Image.open(a) as x, Image.open(b) as y:
                if x.size == y.size and x.convert("RGBA").tobytes() == y.convert("RGBA").tobytes():
                    continue
        differences.append({"file": rel, "why": "different"})
    return {"equal": not differences, "files": len(reference), "differences": differences}


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
