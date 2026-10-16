"""A source of the game's side preprocessed as the build compiles it (MSVC's preprocessor, the target's
includes, definitions and forced include, line markers kept), MSVC's dialect mapped to ISO C for the
parser; of the pragmas, pack and optimize stay.
"""
from __future__ import annotations

import re
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PORT = ROOT / "port"
DECOMP = ROOT / "external" / "khdays-decomp"


def decomp_defines() -> list[str]:
    """compat/decomp_defines.txt: the definitions, one per line."""
    text = (PORT / "compat" / "decomp_defines.txt").read_text(encoding="utf-8")
    return [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]


@dataclass
class Flags:
    """What the khdays-decomp target compiles a source with (port/CMakeLists.txt)."""
    includes: list[Path] = field(default_factory=list)
    defines: list[str] = field(default_factory=list)
    forced: list[Path] = field(default_factory=list)

    @staticmethod
    def game_side() -> "Flags":
        dsprot = next((DECOMP / "src").rglob("dsprot_types.h")).parent
        return Flags(includes=[PORT / "compat" / "include", DECOMP / "include", dsprot],
                     defines=decomp_defines(),
                     forced=[PORT / "compat" / "decomp_compat.h"])

    def arguments(self) -> list[str]:
        return ([f"/I{p}" for p in self.includes] + [f"/D{d}" for d in self.defines] +
                [f"/FI{p}" for p in self.forced])


PRAGMA = re.compile(r"^[ \t]*#[ \t]*pragma\b(?![ \t]+(?:pack|optimize)\b).*$", re.M)
DECLSPEC = re.compile(r"__declspec\s*\(\s*\w+(?:\s*\([^)]*\))?\s*\)")
INLINE = re.compile(r"\b__(?:force)?inline\b")
INT64 = re.compile(r"\b__int64\b")
# C's own calling convention, written out (prepare.py's C of the decomp's
# C++); any other one is no ISO C, and stops the parser
CDECL = re.compile(r"\b__cdecl\b")


def iso_dialect(text: str) -> str:
    """MSVC's dialect in the preprocessed text mapped to ISO C for the parser."""
    text = PRAGMA.sub("", text)
    text = DECLSPEC.sub("", text)
    text = INLINE.sub("inline", text)
    text = CDECL.sub("", text)
    return INT64.sub("long long", text)


class PreprocessError(Exception):
    pass


def compiler(env: dict[str, str]) -> str:
    cl = shutil.which("cl", path=env.get("PATH") or env.get("Path"))
    if cl is None:
        raise PreprocessError("no cl in the Visual Studio environment")
    return cl


def preprocess(source: Path, flags: Flags, env: dict[str, str], cl: str | None = None, raw: bool = False) -> str:
    """`source` preprocessed (line markers kept), in ISO C's dialect -- or as
    the preprocessor wrote it, `raw`."""
    args = [cl or compiler(env), "/nologo", "/E", "/Zp4"] + flags.arguments()
    if source.suffix == ".cpp":
        raise PreprocessError(f"{source}: C++ is not the type pass's")
    r = subprocess.run(args + [str(source)], env=env, capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if r.returncode:
        raise PreprocessError(f"{source}: the preprocessor failed:\n{r.stderr.strip()[:2000]}")
    return r.stdout if raw else iso_dialect(r.stdout)
