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
| `steam_shortcut.py` | the port added to Steam as a non-Steam game, and taken out again |
| `collection_archive.py` | the collections' archives (`.hed`/`.pkg`) read as their own code reads them |
| `hd_inputs.txt` | the collection files the HD tools read (traced: `setup_steps.py trace-inputs`) |
| `build_installer.py` | builds the installer |
| `pinned.json` | the Python and its libraries the installer carries, each pinned by SHA-256 |
| `tests/` | the installer's own tests, run by ctest as `khdays-installer-*` |

## The install

By default in `C:\Program Files\Kingdom Hearts 3582 Days`, so it installs with administrator
rights once; because the port keeps its saves, settings and game data beside itself, the install
folder is then made writable by the player (the Users group is granted Modify on it), so the game
writes there at runtime without administrator rights. `/PRIVILEGESREQUIRED=lowest` on the command
line installs per-user instead, in `%LOCALAPPDATA%\Programs\...`, with no administrator and no
grant needed. The folder has the repository's layout at its root -- `tools/`, `native/tools/`,
`native/installer/` beside `data/`, `mods/` and `saves/` -- because the tools find the game's data
and write the mods from their own place; `setup/` holds the Python and `state.json`, the steps
done. The port, started from a shortcut, takes its own folder as its working folder when the one
it was started in has no `data/extracted`.

Whatever a step makes is written under a `.partial` name and renamed when whole (the port never
takes a `.partial` extraction), so an interrupted install is finished by running the installer
again. Updating or repairing keeps the player's saves, settings and mods; uninstalling removes
what the installer made and asks whether the saves and settings go too (to the Recycle Bin).

## The HD pieces

Optional, from the player's own copies, on the HD page: **HD menus and HUD** (both KINGDOM HEARTS
HD 1.5+2.5 ReMIX and HD 2.8 Final Chapter Prologue: the tools' `mods/HD PACK`) and **HD videos**
(1.5+2.5: the theater's videos and their subtitles in `data/cutscenes`). The helper reads the
entries the tools need straight from the collections' archives (`collection_archive.py`: the
format and its encryption found by our own reverse engineering of the collections' executables;
the cipher's table is read from the player's own `KINGDOM HEARTS II FINAL MIX.exe`, never kept),
runs the tools under a folder of their own and puts what they made in place whole.

## Add to Steam

Optional, off by default, on a page of its own shown only when Steam is found: the port added to
the player's Steam library as a non-Steam game (its name and icon), so it starts from Steam like
any other game, and on a Steam Deck appears in Game Mode. The entry goes in each local Steam
user's `userdata/<account>/config/shortcuts.vdf`; nothing of the collections is changed.

Steam reads those files when it starts and writes them when it closes, so they are changed only
while Steam is closed: if it is running, the installer asks before closing it, and offers to open
it again afterwards. The binary file is changed only when it reads back to exactly its own bytes
(another program's shortcuts are never lost), with a one-time `shortcuts.vdf.khdays-backup` kept
beside it. The entry's id is the port's own, made from its path, so the same number finds it to
remove it. On Linux or a Steam Deck the port, a Windows game, runs through Proton: the entry gets
the same Proton the 1.5+2.5 collection is already mapped to in `config/config.vdf` (none when the
collection has none, as on Windows). Turning the option off on a later run, or uninstalling,
takes the entry (and that mapping) out again.

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
without showing anything (add `/HD1525=<folder> /HD28=<folder> /HD=menus,videos` for the HD pieces,
`/STEAM=yes` to add the port to Steam -- a running Steam is then closed without asking and not
reopened); a file that is not the supported ROM stops it before anything is installed. The
uninstaller takes `/VERYSILENT /SUPPRESSMSGBOXES` too (the saves are then kept; the Steam entry is
taken out, closing Steam if it runs).
