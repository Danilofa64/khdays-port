# How the port is built

The game is the decompilation's C (`external/khdays-decomp`, read-only),
compiled for x64 Windows. `port/build.py` runs `port/tools/prepare.py`, which
turns that C into the build's sources in `build/port/gen-x64`, then configures
`port/CMakeLists.txt` with Ninja inside Visual Studio's x64 environment and
builds: `build/port/obj-x64` (Debug), `obj-release-x64` (Release) or
`obj-asan-x64` (`--asan`: AddressSanitizer on the port's own code).

Ninja learns which headers each file includes from cl's `/showIncludes` lines,
which a Spanish install of Visual Studio writes as "Nota: inclusión del
archivo:" in the console's code page, matched byte for byte against the prefix
CMake found when it first configured the tree. A tree configured in one code
page and built in another knew no file's headers, so a change to a header alone
rebuilt nothing. `build.py` therefore configures and builds in the system's OEM
code page, whatever terminal runs it.

## What prepare.py makes

- **The sources.** Each module's `delinks.txt` names exactly the files the DS
  build links, so every function has one provider. A few files get patched
  copies for MSVC (the decomp's C++ turned into C, below).
- **What the DS linker provided.** `hal_symbols.txt`: files whose only source
  is ARM assembly, which `port/hal` implements. `duplicates.txt`: globals
  several split files defined; one keeps the definition, the rest become
  `extern`. `ds_functions.c`: the native function at each DS address.
- **The rewrite** (`port/tools/references`). The game's C and the HAL are
  rewritten with every pointer a 4-byte reference: the DS address it had. The
  rewritten copies go to `rewritten/`; the data area's and the function
  references' numbers to `khdays_data_area.h`; the DS addresses the port's side
  reads to `khdays_ds_symbols.h`; `sources.cmake` lists the rewritten files
  (`KHDAYS_REWRITTEN`). Symbols at fixed DS addresses (the BSS no C file
  defines, `OVERLAY_n_ID`, the decomp's linker-absolute symbols) are constants
  of the rewrite.

## The hooks

The port meets the game at a few hundred points, each a text replacement in
the decomp's C applied before the rewrite:

- `HOOKS`: by the function's module and DS address (`"main@0201cac0"`), a list
  of `(old, new)` replacements in its file. Each entry's comment says what the
  port does there and where its side lives (`runtime/...`); what the game does
  at those points is in `RUNTIME_MAP.md`.
- `FILE_HOOKS`: the same by the decomp's file, for functions `HOOKS` cannot
  name -- DS Protect's (ov028) are data in the symbols, their bodies encrypted
  in the ROM, and several share a file.
- `UI_HOOKS`: the routines whose drawing the port takes over, as
  `(function, part, the port's function, kind)`. The game's body stays, renamed
  `<function>__khdays_game`, and the routine asks its part's mode (the parts:
  `port/ui/parts.h`; `KHDAYS_UI`). A "replace" routine runs the game's body
  (off), the port's with the same parameters (on), or both, the port's first,
  so the game's drawing can be compared (check). A "follow" routine always
  runs the game's body, then (check, on) the port's, told what the game was
  handed and what it returned.
- `ABI_FIXES`: the places where the C's meaning differs from the ROM's and no
  rule below covers it, each with the ROM's evidence; the decomp is told in
  `DECOMP_FINDINGS.md`.

## The ABI repairs

The decomp's C matches the ROM byte for byte, yet some of it leans on the ARM
calling convention: a call passes fewer arguments than its callee reads (the
callee finds whatever r0-r3 held), or a caller uses the "result" of a function
defined `void` (whatever it left in r0). Natively those are garbage.
`port/abi/ghidra_abi.txt` records, from the ROM
(`port/tools/ghidra/KhdaysAbiScan.java`, `ghidra_abi_targets.py`), what those
registers hold at each such call and return, and `abi_repair.py` rewrites the
C where that source is mechanical:

- **returns**: r0 is the result of the function's last call, or of a call it
  jumps to at the end, its first argument untouched, or a constant -- the
  function returns that;
- **arguments**: the missing register still holds the caller's own argument,
  the previous call's result, a constant or a data address -- the call passes
  it.

Everything else becomes a stop that names the call (`KHDAYS_ABI_GAP`),
resolved from the ROM when the game reaches it. Besides:

- Calls passing fewer arguments than the definition takes pass zeros for every
  missing word. On the ARM9 those are registers; natively the words past the
  argument registers are slots of the caller's own stack, which the callee
  owns: one that assigns to such a parameter (as the ARM code uses the
  register) writes over its caller's frame, and so may an optimizing compiler
  once the parameter is dead (MSVC `/O2` does).
- Functions whose address the C takes (callbacks, handlers, tables) are called
  through pointers, often with fewer arguments, which the padding cannot see.
  Their files are compiled without optimization (`#pragma optimize`):
  unoptimized, a function writes a parameter's slot only where its C assigns
  it. (Optimized, casting Fire returned to address 0 with its saved registers
  zeroed.)
- A prototype declaring a parameter narrower (`char`, `short`) than the
  definition takes it: the ARM caller passes it extended in its register,
  which the definition reads whole; a native caller defines only the narrow
  part. The prototype takes an `int` and each call converts the argument
  itself. The same for a narrow result declared 32-bit (`FX_Atan2` returned
  negative angles 0x10000 too large).
- Locals the C never sets and reads only to add `(x - x)` -- the lever that
  makes mwcc copy a register as the ROM does (`RandNextScaled(n) + (v - v)`) --
  start at 0: the sum is the same, but reading an unset local is undefined
  behaviour an optimizer may act on.
- A struct argument loaded into r1-r3 (`ldmia`), a 64-bit parameter (two
  words) or a `VecFx32` by value (three) is passed as the DS laid it out (the
  rewrite's block, below). A caller expecting a struct by value where the
  definition takes the ARM ABI's result pointer gets that pointer, as MSVC
  passes it for a struct over 8 bytes; since it would be a host pointer, the
  rewrite stops on such declarations and `FILE_HOOKS`/`ABI_FIXES` fix them.

## The registers

`registers.py` rewrites the game's accesses to the DS's I/O registers so that
none faults. The I/O pages stay protected in the game's view: an access this
misses still faults, and the trap reports it. By the register's kind (where
each kind's effect is: `runtime/core/io_regs.c`):

- **Holds a value** (the 2D of both engines, the DMA's sources, destinations
  and fill, EXMEMCNT, POWCNT, the 3D's settings...): its macro, the casts of
  its address, a pointer set to it, an SDK function given it all reach
  `khdays_io_plain(offset)` -- plain memory, the bytes the port reads when it
  draws. Only where everything the access can reach holds values: a pointer's
  reach is read off its uses in the C, an SDK function's off its own C
  (`PLAIN_CALLEES`).
- **Acts when written** (DMA control, IF, VRAMCNT, the card's ROMCTRL, the
  geometry's ports, DISPSTAT) **or is computed when read** (KEYINPUT, the
  geometry's results, the card's data port): its statements become
  `khdays_io_store(address, width, value)` (a compound assignment loading
  first) and its reads `khdays_io_load(address, width)`.
- **Reached through an address computed at run time** (a DMA channel's, a
  timer's, a pointer the C moves): the pointer keeps the DS address and each
  access through it is routed at `&(lvalue)`, right whatever the kind.

Before that the C's spellings of an address are brought to one literal (its
macros, constant sums such as `HW_REG_BASE + 0x440`, accessor macros), and a
struct laid over registers is laid out from its definition. The math unit and
IME/IE are `prepare.py`'s `UNTRAPPED_REGISTER`; the geometry's word stores
through casts its `GEOMETRY_STORE`; the port's SDK routines (`HAL_CALLEES`)
and the DMA's (`VALUE_CALLEES`) take a register's DS address as it is; a few
files read by hand are `REVIEWED_PLAIN_FILES`. Whatever else names a register
is listed in `registers_left.txt` with its line, for a hook or a rule.

## The references

`port/tools/references` parses each file, types it (`types.py`) and emits it
again (`emit.py`) with every pointer of the game's side a 4-byte reference.
Each expression is emitted as a *value* (a pointer as its reference, an
integer as itself) or a *host lvalue* (`(*(T *)KHDAYS_W(r))` for an object
reached through a reference, a local by its name): the window is added only
where a reference is dereferenced.

Some functions take their arguments as the DS laid them out: one block of
4-byte slots in the caller's frame, in the window, its reference the one
parameter `khdays_va`; a call to one stores its arguments in such a block
first. They are those declared or defined with a variable argument list
anywhere (the decomp's `va_start` reaches the arguments from the last named
parameter's address, as mwcc's stack had them), and those a file declares
otherwise than their definition takes them: `Ov000_IsSelectionMoveCollisionFree`
is defined `(int, int, int, ...)` and called with a struct by value where it
reads two ints; `Ov006_MissionPlaceCell` is defined with a struct by value and
called through `(void *, int, ...)` with its four words. On the DS the words
land in the same places either way; in a block they do on any host
(`build.py` `block_abi`).

A declaration from the port's own headers (`runtime/`, `host/`, `gfx/`,
`sound/`, `compat/decomp_compat.h`) is the port's: its types keep host
pointers, and the game's side converts at its uses.

## Memory, linking and the ROM

- **Game data at its DS address.** Every object a C file defines that the DS
  linker placed (`symbols.txt`) becomes an absolute symbol at that address, so
  all code meets it where the DS had it, including code that reaches other
  memory relative to it. An initialised definition keeps its initializer under
  a second name and registers it (`KHDAYS_DATA_INIT`); the runtime copies it
  into place when its module loads, over the image the DS itself loaded there,
  so pointers to functions in it are native.
- **Functions at their DS addresses** (`ds_functions.c`), for a call through a
  DS address the ROM's own data holds, such as an overlay's static
  initializers (`runtime/core/window.c`, `overlays.c`). The ITCM's are listed
  with their sizes: the MobiClip decoder is copied over the ITCM while a movie
  plays, so one runs natively only while its bytes are still the game's
  (NitroSystem's G3D calls two through tables at 0x01ffa1f8 and 0x01ffa598).
- **Addresses several overlays share.** The decomp's relocations name them all;
  on the DS the one loaded runs, but the C names one of them, not always that
  one (ov012 opens its movie through `func_ov008_020846c0`, ov008's shop list,
  where the ROM reaches ov024's MobiClip open at the same address). Natively
  such a reference goes through a thunk to the loaded overlay's function.
- **Split statics** (`PHANTOMS`): globals some split files define that
  `symbols.txt` does not name, statics of one original translation unit
  repeated in each file the decomp split it into. On the DS they are fields of
  a block the module's symbol anchors (the decomp's `tools/share_bss.py`);
  natively they are references at the addresses the ROM's code reaches them
  by (base + offset from its disassembly). A used one missing fails the link.
- **Code only the ROM has.** What only an assembly file holds and `port/hal`
  does not implement is the ROM's bytes at its DS address: data is read there;
  a call into code there stops the game, naming it. `rom_code.txt` lists what
  may be code: MobiClip's routines the game never reaches (the audio decoders
  of kinds 1 and 2 -- every movie with sound is kind 3 -- and the deblocking
  filter, whose one caller passes mode 0) and a label inside
  `func_0200302c`, which the HAL has.
- **Replaced modules** (`REPLACED_MODULES`): ov105, NitroSDK's wireless
  library (WM, its ARM7 protocol, `wh.c`), which the game calls at 24
  functions; none of its files is built and `port/hal/sdk_wireless.c` defines
  them. A call to anything else in a replaced module stops the game, naming it.
- **The decomp's C++** (ov024's MobiClip stream layer) as C: its classes are
  only declared, to call through vtables that are the game's data. After
  `transform()` (mwcc's two destructor slots in place, `VIRTUAL_DTOR`; every
  member `__cdecl`; `delete` spelled out) a class is a struct with its vtable's
  reference first, and a virtual call a call through the vtable's slot with the
  object first -- the call the ROM makes (ov024 0x02084ec4:
  `ldr r1,[r0]; ldr r1,[r1,#n]; blx r1`).

## Multiplayer: who asks "am I the host"

The battle is host-simulated: a host runs every character by
`Ov022_ActorUpdate`'s host branch from the inputs `Ov022_RefreshSlotRows`
copies in, and a guest replays the host's states. With prediction on, a
guest's own character runs the host's branch from its own last control (copied
in by the game's `Ov022_LoadNodeFromResource`), the host's state only
correcting it, and what that branch sends is left to the host
(`runtime/net/mp_predict.c`).

Inside that update the game's "am I the host" checks -- the local player's
index against 0 (`ROLE_QUERY`, `ROLE_QUERY_VAR`) -- answer through the port
(`khdays_mp_role`) as a host's; at every other moment, as the game's. A query
compared with an owner or a slot ("is this my character") stays the game's.
Only a character's own run asks through the port (`ROLE_DIRS`: ov022's actors
and the characters' overlays); what the character does to the others and to
the world -- hits delivered or taken, HP, items, abilities, the mission's
tallies, the shared tables, the members' events, the network (`ROLE_KEPT`) --
stays the game's answer, so a guest never makes locally what the host decides
and sends.

## The regression walks

`port/tools/regress.py` plays the native build through scripted input without
touching anyone's save. Every run happens in a new temporary directory holding
a link to the repository's `data/` and a copy of the save.

```
python port/tools/regress.py [EXE] --walk-save OLD.sav [--player-save saves/khdays.sav]
                                   [--only NAME] [--record] [--env NAME=VALUE ...]
python port/tools/regress.py [EXE] --input FILE --save PATH --log PATH
                                   [--env NAME=VALUE ...] [--timeout SECONDS] [--window] [--keep]
```

The first form plays the walks: routes every change must still complete,
scene for scene (the Mission Mode loop, Story Mode, a mission in play,
challenge 07, a save from the camp menu into a new file, the camp's reset, and
multiplayer with two or four copies of the game on this PC, on the local
network and through a local `khdays-server`). The save stays untouched except
by the walk that saves, whose file must hold every copy it signed whole
(`port/tools/save_file.py`). Their scripts and the scene sequences they must
produce are in `port/tools/regress_walks/`. The second form runs one script,
for traces and experiments. `EXE` is the Release build's unless given;
`--settings FILE` starts the game with a copy of FILE as its settings, `--mods
DIR` with DIR as its mods.
