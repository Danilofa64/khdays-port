# Tools

Python 3.11+, run from the repository's root. Nothing here downloads, copies
or distributes the game: each tool reads your own copy and writes under
`data/`, which is never committed. The installer runs the same tools.

## From your ROM to the game's data

```powershell
py .\tools\verify_rom.py "E:\path\game.nds"
py .\tools\extract_data.py "E:\path\game.nds"
py .\tools\decompress_data.py ".\data\extracted\<hash>"
```

- `verify_rom.py` checks the ROM's SHA-256 against `supported_roms.json`.
  `--json` prints what it read; `--print-entry` prints an entry for that
  list. Register only clean dumps whose region and revision you confirmed,
  with their real hash. Exit codes: 0 supported, 1 read error, 2 not
  registered.
- `extract_data.py` writes `data/extracted/<first 16 hex digits of the
  SHA-256>/`: `system/` (header, ARM9, ARM7, overlay tables, FNT, FAT),
  `nitrofs/` (the game's files), `metadata.json` and `manifest.json` (each
  file's place, size and hash). It refuses an unregistered ROM
  (`--allow-unknown` for development) and an existing extraction (`--force`
  replaces it).
- `decompress_data.py` writes `decompressed/` beside `nitrofs/`: each LZ10 or
  LZ11 file as the game reads it (`x.p.z` becomes `x.p`), with
  `decompressed_manifest.json`. `--force` replaces it.

`tests/` holds their tests: `python -m unittest discover -s tools/tests`.

## HD mods from the HD collections

`hd/` makes the optional HD look (title, menus, save screen, theater, camp,
pause, videos) from your own copies of the KINGDOM HEARTS HD collections;
nothing of them is in the repository. The README's sections on each mod say
which tool makes it and from which files. The installer runs them when you
choose the HD pack.
