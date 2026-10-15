#!/usr/bin/env python3
"""How long a press takes to become an action, alone and in a two-copy mission.

    python native/tools/mp_latency.py --walk-save OLD.sav [--delay MS] [--loss P] [--presses N]
                                      [--jitter MS] [--spacing F] [--players N] [--alone] [--extra MOVES]
                                      [--window] [--env NAME=VALUE ...] [--logs DIR] [--guest-shots DIR]

Two copies of the Release build -- players 1 and 2, the multiplayer walk's way in to the field -- each with
scripted presses in the field: the host attacks twice (its own input, applied at once as alone), then the
guest attacks N times, `spacing` frames apart, then runs three times. The frame each action starts is read
from the copies' session logs: the first sound of a weapon's swing or shot (`weap_swing`, `weap_fire`), or
of a step (`_run`), after the press. Printed per side: the frames from each press to its action, and the
presses no action followed. From the host's log the same for the guest's presses: when the host's game ran
them (counted from the host's own entry into the field, which the copies make within a few frames of each
other: a frame or two either way). `--delay` delays every datagram each copy sends by MS (both ways);
`--loss` loses that share of them (0.1 for a tenth); `--jitter` adds up to that many ms more at random to
each (uneven arrivals, as over the internet). `--extra` gives the guest more moves after its runs;
`--guest-shots` keeps the guest's screen shots (KHDAYS_SHOTS). `--players 3` or `4` adds guests that join
and stand still (the field's pace with three or more). `--alone` measures the play
walk's two attacks with no session. Nothing reaches the internet: the copies meet on this PC's own network.
"""
from __future__ import annotations

import argparse
import re
import shutil
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "native" / "tools"))
import regress  # noqa: E402

WALKS = ROOT / "native" / "tools" / "regress_walks"
HOST_PRESSES = [1600, 1700]  # frames into the field (scene 2)
GUEST_FIRST = 1800
SCENE_FIELD = re.compile(r"f(\d+)\s+scene 19 -> 2")
ACTION = re.compile(r"f(\d+)\s+sound: seqarc/(\S*weap_(?:swing|fire)\S*) starts")
STEP = re.compile(r"f(\d+)\s+sound: seqarc/(\S*_run\S*) starts")
# an attack's voice (PLY_<character>/vo_x<n>at<n>): the character's own, played as its attack starts
VOICE = re.compile(r"f(\d+)\s+sound: seqarc/(PLY_[A-Z]+/vo_x\d+at\d+) starts")


def field_script(script: str, presses: list[int], runs: list[int], extra: str = "") -> str:
    """A walk script up to its field section, then attacks at `presses`, runs right at `runs` and the `extra`
    moves (the walk script's own form, frames into the field)."""
    head = script.split(" s2 ")[0]
    moves = [f"{p}+A {p + 6}-A" for p in presses] + [f"{r}+RIGHT {r + 60}-RIGHT" for r in runs]
    return head + " s2 " + " ".join(moves + ([extra] if extra else []))


def field_start(text: str) -> int | None:
    match = SCENE_FIELD.search(text)
    return int(match.group(1)) if match else None


def delays(text: str, presses: list[int], pattern: re.Pattern[str], spacing: int) -> tuple[list[int], int]:
    """For each press (frames into the field), the frames to the first action after it and before the next
    press; and how many presses had none."""
    start = field_start(text)
    if start is None:
        return [], len(presses)
    events = sorted(int(m.group(1)) - start for m in pattern.finditer(text) if int(m.group(1)) > start)
    found, lost = [], 0
    for i, press in enumerate(presses):
        end = presses[i + 1] if i + 1 < len(presses) else press + spacing
        after = [e for e in events if press <= e < end]
        if after:
            found.append(after[0] - press)
        else:
            lost += 1
    return found, lost


def summary(name: str, found: list[int], lost: int) -> str:
    if not found:
        return f"  {name}: no action ({lost} presses)"
    spread = f"{min(found)}..{max(found)}" if min(found) != max(found) else str(found[0])
    return (f"  {name}: {len(found)} actions, frames to the action {spread} (median {statistics.median(found):g})"
            f", {lost} presses with none")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=regress.DEFAULT_EXE)
    ap.add_argument("--walk-save", type=Path, required=True, help="the save the walks start from (only copied)")
    ap.add_argument("--delay", type=float, default=0.0, help="ms each copy's datagrams are delayed (one way)")
    ap.add_argument("--loss", type=float, default=0.0, help="the share of each copy's datagrams lost")
    ap.add_argument("--jitter", type=float, default=0.0,
                    help="up to this many ms more, at random, for each datagram (uneven arrivals, as online)")
    ap.add_argument("--presses", type=int, default=6)
    ap.add_argument("--spacing", type=int, default=100, help="frames between the guest's presses")
    ap.add_argument("--players", type=int, default=2, choices=[2, 3, 4], help="copies in the session")
    ap.add_argument("--alone", action="store_true", help="the play walk's attacks with no session")
    ap.add_argument("--voice", action="store_true",
                    help="an attack's start by the character's attack voice, not its weapon's sound")
    ap.add_argument("--extra", default="", help="more moves for the guest after its runs, as a walk script "
                    "writes them (\"3500+B 3506-B\": frames into the field)")
    ap.add_argument("--window", action="store_true",
                    help="the copies in windows, not headless (the picture as presented: KHDAYS_SHOTS_HD)")
    ap.add_argument("--env", action="append", default=[], help="NAME=VALUE for both copies")
    ap.add_argument("--logs", type=Path, help="keep the copies' logs here")
    ap.add_argument("--guest-shots", type=Path,
                    help="keep the guest's screen shots (KHDAYS_SHOTS, each copy's own shots/ folder) here")
    args = ap.parse_args()
    if not args.exe.exists():
        sys.exit(f"{args.exe} does not exist")
    extra = {"KHDAYS_LOG": "1"}
    for item in args.env:
        name, sep, value = item.partition("=")
        if not sep:
            sys.exit(f"--env takes NAME=VALUE, not {item}")
        extra[name] = value
    if args.delay > 0:
        extra["KHDAYS_NET_DELAY_MS"] = f"{args.delay:g}"
    if args.loss > 0:
        extra["KHDAYS_NET_LOSS"] = f"{args.loss:g}"
    if args.jitter > 0:
        extra["KHDAYS_NET_JITTER_MS"] = f"{args.jitter:g}"
    logs = args.logs
    if logs is not None:
        logs.mkdir(parents=True, exist_ok=True)

    def kept(work: Path, name: str) -> str:
        text = ""
        for session in sorted((work / "saves").rglob("session-*.log")) if (work / "saves").exists() else []:
            text = session.read_text(encoding="utf-8", errors="replace")
            if logs is not None:
                (logs / f"{name}.session.log").write_text(text, encoding="utf-8")
        return text

    action = VOICE if args.voice else ACTION
    if args.alone:
        presses = HOST_PRESSES
        work = regress.make_workdir(args.walk_save)
        try:
            script = field_script((WALKS / "play.input").read_text(encoding="utf-8").strip(), presses, [])
            regress.run_games(args.exe, [(work, script, work / "game.log")], extra, 150, False)
            print("alone:")
            print(summary("attacks", *delays(kept(work, "alone"), presses, action, 100)))
        finally:
            regress.remove_workdir(work)
        return 0

    guest = [GUEST_FIRST + args.spacing * i for i in range(args.presses)]
    runs_from = guest[-1] + args.spacing + 100
    runs = [runs_from, runs_from + 200, runs_from + 400]
    join = (WALKS / "multiplayer_2.input").read_text(encoding="utf-8").strip()
    scripts = [field_script((WALKS / "multiplayer_1.input").read_text(encoding="utf-8").strip(), HOST_PRESSES, []),
               field_script(join, guest, runs, args.extra)]
    scripts += [field_script(join, [], []) for _ in range(args.players - 2)]  # they join and stand still
    # to the field (about 150 s), then the presses and the runs
    last = max([runs[-1] + 200] + [int(m) for m in re.findall(r"(\d+)[+-]", args.extra)])
    timeout = 160 + (last + 200) // 60
    works = [regress.make_workdir(args.walk_save, player=n) for n in range(1, args.players + 1)]
    try:
        players = [(w, s, w / "game.log") for w, s in zip(works, scripts)]
        regress.run_games(args.exe, players, extra, timeout, args.window)
        host_text, guest_text = kept(works[0], "host"), kept(works[1], "guest")
        rtt = re.findall(r"round trip ([0-9.]+) ms", guest_text)
        print(f"{args.players} players, one way {args.delay:g} ms (+ up to {args.jitter:g}), {args.loss * 100:g}% lost"
              + (f" (round trip as measured: {rtt[-1]} ms)" if rtt else ""))
        print("host:")
        print(summary("its own attacks", *delays(host_text, HOST_PRESSES, action, 100)))
        print(summary("the guest's attacks, as the host ran them", *delays(host_text, guest, action, args.spacing)))
        print("guest:")
        print(summary("its attacks", *delays(guest_text, guest, action, args.spacing)))
        print(summary("its runs", *delays(guest_text, runs, STEP, 200)))
        if field_start(host_text) is None or field_start(guest_text) is None:
            print("a copy never reached the field")
            return 1
    finally:
        if args.guest_shots is not None and (works[1] / "shots").exists():
            shutil.copytree(works[1] / "shots", args.guest_shots, dirs_exist_ok=True)
        for w in works:
            regress.remove_workdir(w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
