# The port's installer

What a player runs to install the port: a setup program made by
[Inno Setup](https://jrsoftware.org/isinfo.php) 6 from the script here. It carries its own Python,
so the repository's tools -- the ROM check, the extraction, the HD tools -- run for the player
without anything else installed. Nothing of the game or of the collections is in it: everything
is made on the player's computer from the player's own copies.

## What is here

| File | What it is |
|------|------------|
| `khdays.iss` | the Inno Setup script: the pages, the files, the steps after them, the uninstaller |
| `messages.iss` | the installer's own words in the game's five languages |
| `setup_steps.py` | the helper: each step the installer cannot do by itself (`--help` lists them) |
| `steam_library.py` | where Steam keeps the collections (its folder, libraries, app manifests) |
| `build_installer.py` | builds the installer |
| `pinned.json` | the Python and its libraries the installer carries, each pinned by SHA-256 |
| `tests/` | the installer's own tests, run by ctest as `khdays-installer-*` |

## The install

For the player alone, without administrator rights, by default in
`%LOCALAPPDATA%\Programs\KH 358-2 Days Port`. The folder has the repository's layout at its root
-- `tools/`, `native/tools/`, `native/installer/` beside `data/`, `mods/` and `saves/` -- because
the tools find the game's data and write the mods from their own place; `setup/` holds the
Python and `state.json`, the steps done. The port, started from a shortcut, takes its own folder
as its working folder when the one it was started in has no `data/extracted`.

Whatever a step makes is written under a `.partial` name and renamed when whole (the port never
takes a `.partial` extraction), so an interrupted install is finished by running the installer
again. Updating or repairing keeps the player's saves, settings and mods; uninstalling removes
what the installer made and asks whether the saves and settings go too (to the Recycle Bin).

## Building it

Needs the port's usual build tools (the repository's README, "Getting started") and Inno Setup 6
(its `ISCC.exe` found in its usual places, or named by `--iscc` or `KHDAYS_ISCC`). The files of
`pinned.json` are fetched once into `build/installer/cache/` and used only when their SHA-256
matches.

```powershell
py native\installer\build_installer.py            # builds the Release port first
py native\installer\build_installer.py --no-build # with the Release port already built
```

The setup lands in `build/installer/`, named by the date and the commit.

## Without its pages

For a test or a tester: `/VERYSILENT /SUPPRESSMSGBOXES /DIR=<folder> /ROM=<rom file>` installs
without showing anything; a file that is not the supported ROM stops it before anything is
installed. The uninstaller takes `/VERYSILENT /SUPPRESSMSGBOXES` too (the saves are then kept).
