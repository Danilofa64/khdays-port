#!/usr/bin/env python3
"""The static analysis of the port's own sources.

    python native/tools/analyze.py [--tree build/native/obj-release-x64]

Builds the khdays-analyze target of a configured tree -- the port's own
sources (runtime, host, hal, server, ui, gfx, gpu, sound; not the
decompilation's rewritten files) compiled with MSVC's /analyze and their real
flags, objects not linked -- and lists every analysis finding (C6xxx,
C26xxx, C28xxx, C33xxx). Exit 0 only with none. A finding that is no defect
is suppressed at its line, with the reason beside it:

    #pragma warning(suppress: 6387) /* why it is not a defect */
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "native"))
import build  # noqa: E402

FINDING = re.compile(r"^(?P<file>.+?)\((?P<line>\d+)\)\s*: warning (?P<code>C(?:6|26|28|33)\d{3}): (?P<text>.*)$")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", type=Path, default=ROOT / "build" / "native" / "obj-release-x64")
    args = ap.parse_args()
    if not (args.tree / "build.ninja").exists():
        sys.exit(f"{args.tree} is not a configured build tree (native/build.py)")
    env = build.msvc_env()
    # every object again: a finding is said only when its file is compiled
    subprocess.run(["cmake", "--build", str(args.tree), "--", "-t", "clean", "khdays-analyze"], env=env,
                   capture_output=True, text=True, errors="replace")
    run = subprocess.run(["cmake", "--build", str(args.tree), "--target", "khdays-analyze", "--", "-k", "0"],
                         env=env, capture_output=True, text=True, encoding="cp850", errors="replace")
    out = run.stdout + run.stderr
    findings: dict[tuple[str, int, str], str] = {}
    others = 0
    for line in out.splitlines():
        match = FINDING.match(line.strip())
        if match:
            where = Path(match["file"]).resolve()
            try:
                shown = str(where.relative_to(ROOT))
            except ValueError:
                shown = str(where)
            if not where.is_relative_to(ROOT / "native"):
                others += 1  # the system's headers, a third party's: not the port's to answer
                continue
            findings[(shown, int(match["line"]), match["code"])] = match["text"].split(": Lines:")[0]
    for (file, line, code), text in sorted(findings.items()):
        print(f"{file}:{line}: {code}: {text}")
    errors = [line for line in out.splitlines() if " error C" in line or "FAILED:" in line]
    if run.returncode != 0 and not findings:
        print("\n".join(errors[:20]) or out[-2000:])
        return 2
    print(f"analysis: {len(findings)} finding(s) in the port's own sources ({others} in others' headers, not counted)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
