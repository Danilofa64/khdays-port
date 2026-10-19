# HD font rendering

For menu-specific font choices, see [Scoped HD font mappings](FONT_MAPPING.md).

The PNG and JSON still live at `mods/<Mod>/fonts/<original-resource-path>.png`
and `.json`, for example `fonts/text/font_eu_10.nftr.png`. Atlas rectangles and
physical image resolution do not change the game's pen advances, cell size,
canvas dimensions, or screen coordinates. Transparent PNG samples are valid
replacement artwork, including for an originally blank glyph.

## Pipeline and provenance

The source traced here is `external/khdays-decomp` commit
`a2b0478b87e3102732094c34f6ca89595f98ab93`, especially
`libs/nns/g2d/calls/DrawGlyphLine.c`, `LetterChar.c`, `ClearContinuous.c`, and
`ClearAreaLine.c`, and `include/nnsys/g2d.h`. No matching source is modified.

1. `Font_LoadUTF16` / `Resource_BindByName` associate the original resource path
   with a font. NitroFS root slashes and separator variants normalize to the same
   relative resource key. Bindings check the NFTR info reference, survive repeated
   initialization of the same file, and are invalidated when that file is freed.
   Copied font objects can share that identity; direct SDK initialization recovers
   the resource name from RAM provenance. `font_runtime` resolves the PNG/JSON
   override through the existing mod loader.
2. The generated `DrawGlyphLine` wrapper observes the canvas before the original
   function and captures its glyph operation afterwards. The original game body
   always runs. Its rewritten `NNSG2dGlyph` contains **two 32-bit references**,
   even on x64: widths, then bitmap address. The bitmap address identifies an
   index; bitmap samples are never read by the replacement renderer.
3. Each source canvas has a last-observed storage snapshot and a graphics plane.
   Before a glyph draw, unattributed source writes update that plane. Afterwards,
   an overridden draw updates the observation and records a resource, glyph
   index, logical cell, color argument, and clip rectangle; it leaves graphics
   intact. Missing overrides copy the game's actual draw rectangle into the
   graphics plane, including unchanged-valued samples needed for DS fallback.
4. Clear callbacks update graphics and remove or clip source commands even when
   the resulting DS bytes equal their previous values. Full clears reset the
   command list. Partial clears split active clips; they are not replay history.
   The port's `sdk_mi.c` also reports completed byte/halfword/word CPU fills with
   their actual written ranges. These update source or mapped VRAM state even
   if an empty NFTR bitmap made the bytes unchanged.
   `TextWindow_ScrollUp` is captured as an explicit move: its intermediate copies
   and clear cannot invalidate commands, and completion translates/clips commands
   and the graphics plane. Its results still become visible only on transfer.
5. Character-load callbacks publish graphics and commands **only for the
   transferred range**. A second command list and graphics snapshot describe
   VRAM independently of later source edits. Full transfers use a bulk path;
   partial transfers preserve the rest of the displayed state.
6. Transfer ownership uses one 64-bit mask per 8x8 tile, independent of font
   alpha. Overlapping canvases relinquish exactly the transferred pixels, so
   both cannot display the same DS glyph beneath a replacement. A first partial
   upload also snapshots otherwise unowned native graphics in its remaining
   pixels. Source buffers and source commands are released on detach; displayed commands survive until
   VRAM replacement. Fully overwritten or relinquished detached canvases expire.
7. VBlank reconciles unattributed VRAM changes against the storage snapshot and
   finds every visible tilemap translation of the canvas. Each mapped tile supplies its
   visibility, palette bank and flips. Graphics and HD glyph images are emitted
   in the same BG slot, with direct RGBA text above the indexed graphics plane.
   The existing UI compositor retains scroll, layout, priority and DS clipping.

## Text-only and mixed canvases

A text-only canvas has its clear fill in the graphics plane; capturing its text
never adds DS glyph samples there. A mixed canvas has the graphics that actually
preceded the glyph, including graphics beneath its original ink. Both follow the
same explicit two-plane rule. Neither a bounding-box erase nor a PNG-alpha mask
is applied to the graphics plane, and a canvas is never declared text-only from
its palette or original glyph silhouette.

Consequently, a replacement can occupy pixels that were transparent in the NFTR
or omit pixels that were opaque there. Its entire logical cell can contain PNG
artwork; original `glyphWidth` does not trim that artwork. The game still chooses
glyph identity using its NFTR; replacement metrics control pen layout. A successful
replacement takes all its artwork/alpha from the PNG, tinted by the game palette.

## Replacement layout metrics

Without `scale`, metric values remain in Days logical units: existing JSONs are
not scaled again. Artwork retains the legacy fit inside the Days cell and text
uses the SDK's advance-based measurement. Edited width clips the drawing area;
an unchanged width keeps the existing full-cell PNG behavior, including artwork
for originally blank glyphs. No replacement retains the original NFTR layout.

An explicit numeric `scale` opts into source-font sizing. `cell_width` and
`cell_height` then describe the replacement's native logical cell, independently
of both the Days cell and atlas resolution. The PNG rectangle fits uniformly
inside this source cell multiplied by `scale`. The same factor is applied once
to `left`, `width`, `advance`, `line_feed`, and kerning. For example:

```json
"scale": 0.5,
"cell_width": 18,
"cell_height": 24,
"line_feed": 24
```

This gives a KH2 System cell a 9x12 logical drawing size. An Event cell declared
as 24x32 gives 12x16 at the same scale. A native advance of 18 becomes 9 and a
bearing of -2 becomes -1. Positions are rounded to the game's integer logical
pixel grid; there is no independent horizontal/vertical scale. Accepted scale
values are finite and between 1/64 and 16.

`left` is the bearing from the pen to the artwork origin. `advance` moves the pen
for the next character and is independent of the fixed artwork cell. `width`
clips the logical drawing area from that origin, without stretching or resizing
the PNG. In source-size mode, zero width means no artwork, even if the original
NFTR glyph had zero width. Glyph operations retain the larger artwork/suppression
bounds, while source-byte observation still follows the original DS write area.

Source-size line measurement takes the union of the declared drawing intervals
after bearing, kerning and width clipping; widths beyond the artwork do not add
padding. Empty glyphs retain their logical cursor extent for space layout. It
does not add original NFTR width or trailing padding. Center/right alignment
normalizes the line's left bound, including negative bearings, to align the
actual drawing box. A uniform bearing can consequently leave a visually aligned
line in place, while changing its pen origin. Left-aligned text retains the pen
origin. Multiline measurement uses line feed between rows and the source cell
height for the final row; block anchors and per-line alignment use these bounds.

Scaled fonts align their replacement baseline to the original Days baseline:
`artwork_y = draw_y + days_baseline - round(baseline * scale) + baseline_offset`.
The Days baseline is the signed CGLP baseline, not the cell height. The JSON
`baseline` uses native source-font units. Optional integer `baseline_offset`
defaults to 0 and uses final logical Days pixels (positive down, negative up;
accepted range -1024 through 1024). For example, native baseline 18 at scale 0.5
aligns to Days baseline 6 by moving the artwork up 3 pixels; offset -1 moves it
up 4. No-scale fonts ignore both baseline positioning and the offset.

This translation affects artwork only: advances, cell size, line feed and
horizontal/block layout stay unchanged. Each line aligns to its own Days
baseline after the game resolves alignment. Clips cover the union of the native
write area and shifted artwork, bounded by the canvas. Source draw identity
stays separate, so repeated draws, fallback and scrolling still replace the
correct operation. Partial clears continue to clip the surviving artwork.

The original canvas remains the intended clipping region: enlarging artwork does
not enlarge a game window. Full/partial clears clip the expanded commands, scroll
moves them, and only transferred/owned tiles can display them. Tilemap visibility,
palette and flips still apply per destination tile. No per-glyph textures or
per-origin image allocations were added; the bounded supplementary viewport and
dirty tile updates remain in use. Artwork outside the canvas or untransferred
tile ranges stays clipped rather than overwriting unrelated graphics.

Existing KH2 JSONs containing already-scaled Days metrics must be regenerated
before adding `scale`: export native KH2 `fontinfo.bar` bearings/drawing widths/
advances, native 18x24 or 24x32 cell dimensions and native line spacing, then set
the desired uniform factor. Simply adding scale to Days-unit metrics would scale
them twice. The renderer does not edit mod files or compensate their advances.
Atlas `x/y/w/h` remain sampling rectangles in reference-atlas pixels; a 2x/4x PNG
changes sampling only and requires no change to source metrics or `scale`.

`line_feed` controls multiline spacing. Optional pair kerning uses Unicode
codepoints and a signed logical adjustment, for example:

```json
"kerning": [{"left": 65, "right": 86, "adjust": -2}]
```

This tightens `AV` by two units in both drawing and measurement. Pairs reset at
line boundaries; absent pairs add zero. Layout overrides apply only when HD text
is enabled and an atlas is bound. Missing fonts/glyphs retain original metrics.
Original NFTR width records remain untouched for bitmap drawing and glyph capture;
JSON values are passed as native integers without narrowing to NFTR bytes.

For selective original artwork, keep the glyph's index and add
`"fallback": "original"`. An entry may retain its other fields or use the
short form:

```json
{"index": 42, "fallback": "original"}
```

That index remains reserved; later indices and `charmap` references do not move.
The flagged entry's PNG rectangle and replacement metrics are ignored. The
original NFTR supplies artwork, bearing, drawing width and advance, without the
replacement scale or baseline adjustment. Replacement kerning pairs touching
that character are disabled. Font-wide replacement line spacing still applies
to the surrounding string. Existing entries without the flag behave as before;
other fallback values are rejected. Keep the entry rather than deleting it or
renumbering the array. Restart the game after editing cached font metadata.

This uses the same unavailable-glyph path as an absent replacement index. The
original blit runs normally, and its native pixels are retained in the canvas's
graphics plane rather than captured as HD commands or removed by HD suppression.
Regression coverage includes internal and consecutive fallback entries, mixed
4bpp/8bpp canvases, equal-byte redraws, clears, native metrics, mixed-string
measurement/right alignment, reordered maps and both legacy/scaled metadata.

Port-only transforms in `port/tools/font_layout.py` adapt `Text_DrawGlyph`,
`Text_DrawLine`, `NNSi_G2dFontGetStringWidth`, `NNSi_G2dFontGetTextRect`,
`Text_DrawAlignedLines`, `Ov002_StepCrawlChar`, and the ov008/ov025
`DrawRowCountDigits` routines at the decompilation commit above. This covers
the traced shared text pipeline, crawling text and direct counter advances.
Hardcoded slot positions in game UI remain fixed. Movie glyph records and
unimplemented OBJ replacement paths retain their existing behavior.

`max_width` remains passive metadata. A nonempty `charmap` maps Unicode
characters to replacement atlas indices; it need not repeat the original NFTR
index order. Binding reads the original FINF/CMAP tables (direct, table and scan
forms), then builds an original-index-to-atlas-index lookup once per loaded
atlas. Metrics, availability, geometry, baseline and painting use this same
lookup. Missing characters use original metrics/artwork. An empty charmap keeps
legacy index-based mapping. Aliases of a single original glyph use its first
character code. Duplicate replacement codepoints are rejected. `baseline` controls scaled artwork placement
only; it remains passive metadata without explicit scale.
Cell dimensions become active only with explicit scale. Future font backends can
supply the same geometry, scalar layout and raster interfaces.

## Updates and limits

Dirty bits distinguish graphics and HD tile work. Unchanged frames do not paint
or upload either image. Palette and map changes invalidate the affected HD tiles;
glyph/clear/transfer changes invalidate their regions. The existing immutable,
untinted raster cache remains capped at 32 MiB. Each temporary glyph raster is
also limited to 32 MiB before allocation. Its paint interface has no NFTR
bitmap dependency and can accept another raster backend later.

There are at most 8192 active source or displayed command fragments per canvas;
the limit counts surviving clips, not past redraws. Snapshot storage is bounded
to 512 KiB per plane per canvas. Allocation/fragmentation limits fall back to the
DS image, with full clears enabling capture again. Canvas images continue to use
the existing dimension-based UI texture pool; this change does not add texture
retirement to that pool or a global GPU-memory budget.

An unhooked memory write is observable only when bytes change. Such source/VRAM
changes conservatively retire a draw whose original DS write footprint was
overwritten, including all its published fragments, without inspecting glyph
ink. Enlarged/shifted PNG artwork does not give text ownership of neighbouring
storage. Graphics writes in that overhang update graphics without retiring HD
text. Writes within the native footprint remain conservatively invalidating. A raw
store or unhooked copy of identical bytes has no observable event; clearing an
originally blank glyph therefore requires a captured clear, SDK CPU fill, or
character-load operation. If another game path needs stronger semantics, add a
specific operation hook instead of bitmap matching. Known character-load ranges,
SDK fills and clear callbacks handle equal-byte writes explicitly.

The covered path is line-layout, 4-bit/8-bit BG character canvases. OBJ text
canvases remain diagnostic/fallback, as before. The primary translation keeps
the existing canvas images and layout anchor. Every other visible tile containing
a captured replacement command shares one supplementary graphics/HD image pair,
bounded to 33x25 tiles (the viewport plus partial scrolling cells). Other graphics
remain native. Membership comes from command clips, never NFTR samples or PNG
alpha. Source operations and VRAM ownership remain shared. Supplementary tiles
update only when their source, attributes, dirty regions or palette change, before
the primary renderer consumes dirty bits. The pair is released when unused.
Mod hot reload / TTF / OTF rasterization are not implemented here.

## Command-menu selection regression

Read-only tracing of the same decompilation commit establishes this path:

- `Ov002_BuildTileRow` initializes five surfaces, calls
  `Ov002_DrawPanelRowText`, uploads each through `GX_LoadBG1Char`, then frees
  its source buffers. The first descriptor in `data/ov002_tile_rows.c` is
  8x24 tiles, 4bpp, starting at tile `0xd0` (BG1 character offset `0x1a00`).
- `Ov002_DrawPanelRowText` draws the fixed labels into this first surface at
  `16*i+3` pixels. `Ov002_PickTextFitting64` chooses the default font or
  `/text/font_eu_08s.nftr` by string width. Selection does not choose a new font
  or redraw the glyphs. Captured glyphs survive the freed buffer in the published
  command list, with the same canvas identity and VRAM ownership.
- Class 0 of `Ov002_PanelApplyCursorMove` refreshes the old/new headers with
  `bRightAlign=0/1`. `Ov002_PanelRefreshRowHeader` chooses source tile
  `0xd0+16*nRow`, palette `0xf` enabled or `0xe` disabled, and slot
  `nRow<=2 ? 3 : nRow+1`. Magic uses source row 3; Items uses row 4.
  The primary label can use row 1 or 2, as selected by
  `Ov002_PanelSelectSource`'s one-based source index.
- `Ov002_PanelWriteSlotLabel` clears eleven cells on each of two map rows,
  then writes the eight upper/lower tiles into resource 9. The selected indent
  is 2 cells; the unselected indent is 1. Display rows are 18, 20 and 22.
  Upper tiles are `0xe0..0xe7` or `0xf0..0xf7` for the primary label,
  `0x100..0x107` for Magic and `0x110..0x117` for Items; lower tiles add 8.
  The palette occupies the high nibble of each entry.

These labels use BG1 characters and a changing tilemap, including the selected
label. The traced path does not upload these labels through OBJ. No special
label text, selected-font binding or NFTR silhouette test is needed to explain
the failure: the former `shown_at` chose one majority origin for the whole
surface. `make_image` accepted only tiles consistent with that origin, and
`emit` omitted only those native BG cells. Other strips remained native DS text.

The following are the tile-coordinate origins implied by **all sixteen map
entries of each label**, computed as destination minus source tile coordinates.
Each tuple is `(x,y)`; all three columns must be HD eligible and have their
native cells omitted after the fix.

| Primary source row | Selected | Talk/primary origin | Magic origin | Items origin |
| --- | --- | --- | --- | --- |
| 1 | Talk | (2,16) | (1,14) | (1,14) |
| 1 | Magic | (1,16) | (2,14) | (1,14) |
| 1 | Items | (1,16) | (1,14) | (2,14) |
| 2 | Talk | (2,14) | (1,14) | (1,14) |
| 2 | Magic | (1,14) | (2,14) | (1,14) |
| 2 | Items | (1,14) | (1,14) | (2,14) |

With primary source row 1 and Magic selected, all origins have sixteen cells.
The former tie selects the first visible strip (Talk), rejecting both Magic
and Items. **Items' entries and position are identical between Talk-selected
and Magic-selected states**, but Talk's changed origin changes the winning
group. This is a code-derived explanation of the reported observation; the
actual running session's primary source row still needs a runtime trace.

`text.c` now covers all translations through the existing compositor slot.
The supplementary viewport image follows the primary wrapped canvas instance
containing the most visible mapped cells. An explicit compositor attachment
preserves layout/HUD transforms and excludes the viewport rectangle from
widescreen grouping. Per-cell palette, flips, ownership and PNG clipping remain
authoritative. `KHDAYS_TRACE_TEXT=1` reports the primary origin and supplementary
text cell count when the supplementary map changes.

## Image-allocation regression

The first multi-origin implementation allocated full-canvas graphics/HD images
for every translation, including aliases of a cleared background tile. A repeated
tile across a 32x24 screen can imply 768 origins. For a 256x192 canvas at scale 4,
768 graphics images alone require 2304 MiB, before HD copies or GPU storage.
It also multiplied compositor records and raster work. The command-strip tests
did not cover this case. A subsequently reported game run lagged and crashed;
the local session log records an access violation without a symbolic stack,
so the precise crashing instruction has not been attributed.

The repeated-background-tile regression failed allocation-count, memory-budget
and submission-count assertions against that implementation. The corrected path
uses the fixed-size supplementary pair above; origin voting is a fixed 64x64
histogram rather than a growing, linearly searched origin list. Canvases without
published replacement commands allocate no supplementary images. Even an HD
replacement for an originally blank glyph repeated across the entire screen
uses four images total, including the original primary pair. At scale 8 the
synthetic 256x192 case peaks at 49.78 MiB of live CPU image storage; GPU copies
and the existing texture pool are outside that measurement. Image count and work
no longer multiply by the number of tilemap origins.

## Intermittent letters and unreplaced runs

Further tracing of the same decompilation commit found concrete BG failures,
without requiring an OBJ explanation:

- Field strings in `ov002_field/data/ov002_strings_0207eadc.c` and
  `ov002_strings_0207eb40.c` name `/text/font_eu_08s.nftr` and
  `/text/font_eu_10s.nftr`. Normal field fonts and camp fonts also use relative
  `text/...` forms. The previous loader rejected all leading-root-slash names.
  Width-based font switching could therefore mix native and HD output. Both
  binding and loader cache keys now normalize these names, retaining metadata
  source checks, traversal checks and existing mod paths.
- `Ov000_SetupLogoTileSurfaces` and `Ov011_SetupTitleTileSurfaces` call
  `NNS_G2dFontInitUTF16` directly on existing loaded files. The previous hook
  discarded bindings unconditionally. The corrected binding rules above cover
  reinitialization, copied font identities, address reuse and direct initialization.
- A missing override at a neighboring proportional pen position used to retire
  every HD command intersecting its rectangle, including a whole neighboring
  glyph. Explicit fallback now retires a reissued draw at the same pen and clips
  other intersecting commands only within its draw rectangle. It does not apply
  the conservative whole-draw rule reserved for unattributed memory changes.
- A zero NFTR `glyphWidth` previously rejected valid PNG artwork. Such a draw
  can now record the full logical PNG cell even though the original SDK draw
  produces no pixels. NFTR bitmap samples do not determine its eligibility.
- `TextWindow_ScrollUp` moves tile rows through `MIi_CpuCopyFast`, then residual
  pixel lines through `MIi_CpuCopyFast`/`MIi_CpuCopy32`, and clears the bottom
  strip. Previously those copies looked like unknown source edits and retired
  moved glyphs. The new before/after hook preserves the operation as a logical
  scroll, including partial top fragments and the text-free graphics plane.

Seven new capture/binding assertions failed before these fixes. Tests now also
cover direct initialization through mocked resource provenance, freed/reused
font identities, actual loader/cache normalization, whole/partial scrolling,
mixed graphics, and delayed publication. The OBJ upload callbacks remain
diagnostic/fallback. The linked SDK canvas vtable in
`libs/nns/g2d/auto/g2d_charcanvas_tables.c` is BG-only; that does not prove no
BG-formatted text buffer is ever consumed by OBJ. Sprite consumers and prebuilt
text graphics still require specific runtime evidence before extending coverage.

## Validation

From the repository root on Windows:

```bat
port\tests\run_hd_fonts.cmd
port\tests\run_hd_fonts.cmd asan
```

The runner discovers Visual Studio with `vswhere`, initializes its x64 tools,
and builds synthetic tests into `build/tests`, without reconfiguring CMake.
The canvas test includes the implementation with fake memory, transfers, map,
palettes and compositor images. It exercises opposing DS/PNG silhouettes,
originally empty glyphs, mixed graphics under original ink, fallback,
full/partial/equal-byte clears and SDK fill events, delayed and partial
publication, raw source and VRAM erasure, per-tile flips/palettes/visibility, overlapping partial-tile
ownership, source release, 1500 menu redraw cycles and 100 rebuilds of a 128-glyph menu.
The command regression reconstructs the native tilemap formulas for both primary
source rows and cycles all three selections 300 times each. Every state checks
all 48 label cells, each label's origin and HD image eligibility, the absence of
the original silhouette and the submitted PNG pixels after all placements have
been queued. It explicitly asserts unchanged Items entries when Magic is selected.
It also checks stable-frame cache reuse, bounded image counts, palette updates,
tilemap removal/restoration with live glyphs, and VRAM erasure across placements.
Repeated blank tiles and 768 copies of a PNG over an empty original glyph check
image count, memory and submissions at scales 4 and 8, unchanged frames, scrolling,
per-copy palettes/flips, and both 4-bit/8-bit canvases. `ui_follow_test.c` includes
the real compositor to check wrapped-anchor selection, HUD attachment, explicit
layouts and exclusion from widescreen groups.
The raster test uses an
arbitrary atlas rectangle to check PNG alpha, tint, transparent RGB, clipping,
logical/physical sizing, cached results and source-over blending.

Only changes to `prepare.py` hooks (including `font_layout.py`) require regeneration using the configured
Python runtime (with `pycparser`) before the normal incremental MSVC build.
The command-menu placement fix changes no hooks and needs only the build step:

```bat
python port\tools\prepare.py --out build\port\gen-x64
cmake --build build\port\obj-x64 --target khdays-native
```

Local validation completed:

- Source-cell sizing follow-up independently confirmed that the previous raster
  fit every PNG into the Days cell, captured clips retained that cell, and the
  extent helper added original NFTR padding (`advance + width - originalWidth`).
  An explicit source scale now controls cell geometry and all font metrics once;
  the padding formula is removed. Native geometry and DS storage observation are
  separate. Line bounds and origin normalization use the same clipped geometry;
  multiline blocks end at the final cell height, rather than the final line feed.
  Modified files are `port/runtime/mods/fonts.cpp`, `fonts.h`, `font_runtime.cpp`,
  `font_runtime.h`, `font_raster.cpp`, `font_raster.h`, `port/ui/text.c`,
  `port/tools/font_layout.py`, `port/tests/hd_text_test.c`, `font_paths_test.cpp`,
  `font_raster_test.cpp`, `font_layout_test.c`, `run_hd_fonts.cmd`, and this document.
  Synthetic coverage includes System 18x24 and Event 24x32 cells replacing 7x8
  and 10x10 Days cells, fractional scale, native bearings/advances/kerning,
  doubled PNG resolution, strict invalid-scale/source rejection, enlarged-cell
  clear/scroll/transfer/mapped-tile rendering, and the existing command/HUD tests.
  The excessive-spacing example checks a legacy 6x8 fit with advance 18 versus
  native 18x24 artwork with advance 18, then 9x12 artwork/advance 9 at scale 0.5.
  SDK integration compiles the generated draw, line measurement, per-line
  alignment, multiline bounds and block-anchor functions, with authored tests at
  `/W4 /WX` and AddressSanitizer. No mod JSONs or decompilation files were edited.
  All five font tests and the VS 2022 x64 incremental native build passed after
  final regeneration. Compositor, one-screen and reference tests passed; render
  fixtures were unavailable. Stress results remain four images and 49.78 MiB
  peak at physical scale eight, with unchanged frames reusing uploads.
- Layout-metric follow-up: all five synthetic executables passed with MSVC
  AddressSanitizer; authored test code compiled with `/W4 /WX`. The new test
  links regenerated `Text_DrawGlyph`, `Text_DrawLine`, and string measurement
  bodies and checks unchanged layout, edited bearings/advances/bounds, pair
  kerning, newline continuation, horizontal/reverse/vertical pen movement and
  untouched NFTR records. Loader tests cover integers beyond NFTR byte limits;
  raster tests verify aspect preservation and independence from logical metrics.
  Regeneration and the existing VS 2022 x64 native target succeeded. Existing
  compositor, one-screen and reference tests passed; frame rendering skipped
  because fixtures were unavailable. Ruff remains unavailable locally; Python
  syntax and all eight source-transform matches were checked.
- Bearing/bounds follow-up: the former measurement used
  `max(advance, left + width)`, so right alignment could cancel a uniform bearing
  and smaller widths could not shrink measured bounds. Measurement now keeps
  bearing independent and permits narrower logical extents. Regressions use
  generated `Text_DrawAlignedLines` as well as the draw/measurement bodies to
  verify `left=10` and `width=0` under left, center and right alignment. Edited
  width also clips PNG artwork, with full-cell compatibility for unchanged metrics.
  Canvas tests cover width-six and width-zero clips in both primary and mapped
  tile rendering, including suppression of DS ink with an empty PNG clip. PNG
  scale and pen spacing remain independent of logical width. All five synthetic
  executables passed with AddressSanitizer after regeneration. Local metadata inspection
  also found mismatched `source` fields in the mod's `font_eu_08s`/`font_eu_10s`
  files; those correctly remain rejected rather than bypassing path validation.
- Missing-letter follow-up: all four synthetic executables passed with `/W4 /WX`
  and AddressSanitizer, including loader paths, bindings and scrolling. One
  `TextWindow_ScrollUp` operation hook was added; regeneration and the existing
  incremental VS 2022 x64 `khdays-native` build succeeded. The generated wrapper
  was inspected for before/game/after order and host-pointer conversion. Existing
  compositor, one-screen and reference tests passed; render fixtures remain absent.
- Allocation-regression follow-up: all three synthetic executables passed with
  `/W4 /WX` and AddressSanitizer, including the repeated-tile resource bounds and
  compositor attachment checks. The six command-selection cases still pass.
  The incremental VS 2022 x64 build and existing compositor/one-screen tests
  passed; render fixtures remain unavailable. No hooks or generated sources changed.
- Command-menu follow-up: the incremental VS 2022 x64 `khdays-native` build
  succeeded without regeneration. Both synthetic tests passed with `/W4 /WX`
  and AddressSanitizer, including 600 command-selection frames, each checked
  again unchanged for cache reuse. Both primary source-row variants are covered.
  `khdays-elements` and `khdays-onescreen` passed; `khdays-render` still skipped
  without frame fixtures. The decompilation checkout remains unchanged.
- Configured Python 3.13 regeneration and the existing incremental VS 2022 x64
  `khdays-native` target succeeded during the earlier independent-plane redesign;
  no CMake reconfiguration was needed.
- Both synthetic executables passed with `/W4 /WX` and MSVC AddressSanitizer,
  including 1500 menu cycles and 100 full rebuilds of a 128-glyph canvas.
- Existing `khdays-elements` and `khdays-onescreen` CTest cases passed.
  `khdays-render` skipped because frame fixtures were unavailable.
- Changed C/C++ files passed `clang-format --dry-run --Werror`; Python syntax
  compilation and `git diff --check` passed. Ruff was unavailable locally.
- Generated glue was inspected for before/game/after order, host-pointer
  conversion, and SDK fill byte-count rounding.

In-game verification still required:

- With an unchanged exported JSON, compare placement against the previous build.
  Then change one glyph's `left`, `width`, and `advance` separately: bearing
  should move artwork, advance should move following glyphs, and edited width
  should clip the drawing area and affect bounds/alignment without stretching
  the PNG. Unchanged widths retain the existing full-cell image. Check centered,
  right-aligned, multiline, crawling and counter text. Test an `AV` kerning pair
  and confirm that it resets across a newline. Hardcoded windows can still clip.
- Use intentionally different silhouettes, empty glyph PNG cells, translucent
  edges, and atlas padding with transparent RGB; check for any DS ink leakage.
- Select populated and empty inventory slots repeatedly; confirm descriptions
  disappear when cleared and do not change before their upload.
- Cycle Talk/Magic/Items selections, disabled commands and selected colors.
- Enter/leave text-heavy pause menus repeatedly; check backgrounds, layering,
  scroll/window clipping, CPU time and memory after the pool has warmed up.
- Exercise mixed graphics/text canvases, partial row updates and alternate
  languages/fonts, including both 4-bit and 8-bit canvas paths.

### Scaled baselines and initial panel population

The ov008 list path traced at the commit above is `Ov008_DrawListEntryRow`
(name plus quantity on the surface at +0x190), `Ov008_DrawListWindow`, and
`Ov008_TickPageBTransition`. The surface template `data_ov008_0208f688` creates a
20x32-tile 8bpp canvas; transition uploads its 0xa000 bytes to sub BG2 +0x4000.
Names use `Text_DrawWithShadow`; quantities use two `Text_DrawDirectional_2`
passes. These are BG canvas glyphs, not an OBJ-only text path.

The source-sizing revision enlarged operation clips while `invalidate_draws`
still treated those clips as storage lifetime bounds. A later unrelated source
or VRAM write in PNG overhang could discard an already captured glyph, leaving
the clean graphics plane with no HD command to composite. Redrawing after the
initial graphics population could restore it because identical stored bytes no
longer caused invalidation. Captures now retain original write bounds separately
from artwork and surviving clips. Actual overwrite/clear handling is retained;
no periodic full redraw or bitmap comparisons are added.

A synthetic 20x32 8bpp initial-population regression draws names and quantities
in eight rows, writes neighbouring graphics in source and VRAM, checks the first
emitted frame and unchanged-frame cache reuse, then repeats the menu redraw and
checks a true native-footprint overwrite still removes the glyph. Restoring the
old lifetime predicate fails this regression (20 assertions in the initial
version). This establishes the failure mechanism; confirmation that it accounts
for the entire reported game-menu failure still requires an in-game run.

Baseline tests cover native baseline scaling, default/positive/negative offsets,
legacy compatibility, multiline positioning, canvas-edge and partial clipping,
reissuing the same draw and scrolling. Existing command selections, mapped
placements, HUD attachment and repeated-menu/cache tests remain enabled. No
hooks, generated sources, mod JSONs or decompilation files change in this follow-up.

### Font paths, character identity and copied portrait text

The game asks for `/text/font_eu_10all.NFTR` in
`ov004_calendar/Ov004_LoadSceneGraphics.c`,
`ov006_mission_mode_select/Ov006_MissionSceneCtor.c`, and
`ov008_camp_menu/Ov008_MissionSceneCtor.c`. The port previously rejected the
uppercase extension before probing mods. Capture and metadata lookup now
canonicalize only the NFTR extension, separators and root prefix. Directory
and resource identity validation are retained; there is no basename-only or
cross-font fallback. `font_eu_10all` still requires its own matching mod pair
at `fonts/text/font_eu_10all.nftr.png` / `.json`.

The original European `font_eu_10` contains 315 glyphs, while the inspected KH2
replacement contains 204 in a different Unicode order. The original combined
`10all` contains 3852 glyphs. Replacement JSON `charmap` is now active, so a
replacement need not manufacture thousands of dummy entries to keep original
indices. A dedicated combined-font atlas can keep its extra symbols/styles;
characters it omits remain DS fallback. No existing mod files were edited.

The cutscene portrait-label path is
`ov002_field/Ov002_SceneApplyPanelRequest.c` (at the decomp commit above): it
copies 0x3840 bytes from the portrait NCGR into scratch canvas storage, draws
name face/shadow through `Text_DrawDirectional_2` (borrowing `font_eu_10s` for
wide labels), then copies the complete result back to the NCGR. The portrait
is uploaded by `Ov002_EnqueueBothSurfaces` through a BG upload target. Previously
only the bitmap bytes survived this copy; the destination had no HD commands.

Port-side SDK CPU copy callbacks now retain complete canvas command/graphics
snapshots on exact, non-overlapping RAM copies. Staging canvases use the source
geometry and publish only upon a later BG transfer. They follow further full
copies, equal-byte replacement, clears and source release. Before origin
metadata is replaced, the destination file offset identifies the containing
allocation, allowing the existing free callback to retire interior NCGR data.
Already transferred VRAM remains valid until overwritten, as for real canvases.
New DS canvases can adopt matching staging records. Geometry mismatches fall
back to exact copied pixels. Partial/interleaved copies do not create staging
records and retain existing storage reconciliation behavior.

At most sixteen staging records and 2 MiB of underlying staged character storage
are retained. No per-frame raster loop was added: unchanged published commands
reuse existing dirty tiles and images. Tests cover two-step scratch/resource
copies, empty/identical-byte replacement, delayed transfer, allocation release,
VRAM-copy exclusion, unchanged frames, repeated reuse and the record budget.
Original CMAP direct/table/scan parsing and reordered/missing glyphs are tested
separately, along with uppercase paths and strict source rejection.

The main camp menu labels and panel control/Stats labels are stored graphics,
including the localized `UI/cm/cmo_<language>.p2` character sheets, rather than
calls into the NFTR text pipeline. `Ov008_InitCampaignMenuContext` opens this
localized archive; cell/model setup and drawing select those authored resources.
Inspection of the local ROM's NCGR sheets confirms embedded label artwork.
These require graphics replacements or an explicit semantic label/clean
background implementation; changing font glyph capture cannot replace baked
letters. They remain outside this font-renderer change. Inspection artifacts
are kept only under ignored `build/tests/font-path-investigation`, never as
source assets. No matching sources or generation hooks were modified.

Validation for this follow-up: the existing VS 2022 x64 incremental
`khdays-native` build succeeded. Only the changed native HAL `sdk_mi.c` was
refreshed through the existing reference rewriter; the decompiled sources did
not need regeneration. All five HD font suites passed with `/W4 /WX` and
AddressSanitizer. Existing elements, onescreen and references CTest cases
passed; render skipped for missing frame fixtures. Changed C/C++ formatting
and `git diff --check` passed. In-game confirmation of portrait labels and the
dedicated combined-font atlas is still required; baked graphics remain as
described above. No mod files were changed.

### Portrait upload layer versus display layer

The portrait follow-up was verified against the running game on 2026-10-18.
The trace captured eight name operations, copied all eight from the 15x15
8bpp scratch canvas into the NCGR and published its 0x3840 bytes at main BG
character offset 0x540 (tile 21). The remaining rejection was after capture:
`GX_LoadBG3Char` supplied BG3 as the assumed display layer, but the game was
displaying the shared characters through BG2. Live BG2CNT was 0x1f81 (8bpp),
BG3CNT 0x1e02 (4bpp); BG2's tilemap referenced all 225 portrait tiles at
column 17, row 8, beginning with entry 0xc015. BG3 had no portrait placement.
The old color-depth/placement check consequently rejected the HD canvas while
native pixels still displayed on BG2 using replacement layout positions.

The compositor now checks the existing display layer first, then other enabled
text BGs of the same engine and pixel depth for tilemap references to the owned
character VRAM. It selects that layer for palette lookup, scrolling, suppression
and overlay submission. The upload still determines the VRAM address and
publication boundaries; it does not need to name the display layer. Existing
primary/supplementary placements and cached images are retained. This selects
one display BG per canvas, as before; simultaneous display on multiple BGs is
not added by this change.

The portrait regression uses the actual 15x15 geometry, overlapping shadow/face
passes and shared BG3-upload/BG2-display routing. It also covers a matching-depth
upload layer with no referencing tiles, map removal, reassignment to another BG,
and 30 unchanged frames with no new images, uploads or raster calls. Disabling
alternate-layer selection in an ignored negative-control build produces 34
assertion failures; all five normal HD font suites pass with AddressSanitizer.
The incremental VS 2022 x64 native build passes. No generation hooks, generated
sources, font mods or matching decompilation files change in this follow-up.

In-game verification passed in the corrected build: the trace resolved the
portrait to BG2 at cell 17,8 and submitted eight operations for Axel and ten for
Roxas. The user confirmed that the HD artwork and spacing are fixed. The
diagnostic launch enabled tracing only for that process; normal launches retain
the usual tracing settings.

### Fractional layout and HD sampling

Scaled metrics previously passed through `layout_units`/`lround` before every
advance, bearing and kerning operation. `Text_DrawLine` then accumulated integer
positions; capture and rasterization also stored integer DS coordinates and
rounded scaled artwork dimensions before multiplying by the UI resolution.
For EVT at scale 0.35, native advances 11 and 7 became 4 and 2 instead of 3.85
and 2.45. Upscaling could not recover those lost fractions.

The port-side `font_layout.py`/`font_fractional.py` transforms now generate double
precision internal glyph, line, measurement and alignment helpers. Public game
entry points retain their integer ABI, with rounding only at that boundary.
The original SDK blitter still receives integer positions and unchanged NFTR
bitmap/width records. A scoped artwork position accompanies that blit into the
HD capture hook; native write/lifetime rectangles remain separate integers.
Captured HD positions and baseline adjustment retain their fractions through
partial clears, RAM copies, VRAM publication, scrolling and supplementary tile
placement. Final compositor placement still follows the same BG/UI transform.

Measurement and painting share unrounded scaled bearings, advances and painted
extents. Multiline feed, center/right alignment and negative bearings use the
same fractional coordinates internally. Existing game-side width consumers and
wrapping decisions still receive an integer total, rounded once per result.
The game character-spacing term and JSON pair kerning are retained; fixed
counter-column steps remain fixed. Original fallback glyphs keep native metrics
and pixel-grid bitmap rendering; kerning touching explicit fallback remains off.
Legacy fonts without scale preserve their integer geometry and alignment rules.

The field text crawl retains only a bounded residual alongside the game's
integer cursor, keyed by context, stream and expected index/cursor. Stream or
cursor changes reset it. This avoids both per-character advance rounding and
repeatedly rescanning an entire string as the crawl progresses.

HD rasterization uses exact cell dimensions and fractional positions at output
resolution. A bounded normalized glyph cache remains independent of fractional
translation; painting samples its premultiplied artwork at the physical pixel
centers. Integer geometry keeps a direct cached-texel path. Tile scissors remain
integer canvas regions, with outward bounds around fractional artwork, so tiled
partial rendering agrees with a single full glyph draw. No per-frame full-canvas
redraw or per-position texture cache was introduced.

Validation on 2026-10-19:

- All five HD font suites passed with VS 2022 x64 `/W4 /WX` and AddressSanitizer.
  Regenerated SDK tests cover EVT's actual `pipsqueak.` native advances at 0.35,
  other uniform scales, exact right alignment, positive/negative bearings,
  intentional spacing, mixed native fallback and a 320-character drift check.
  Canvas tests cover fractional capture, scroll/clear behavior, unchanged-frame
  caching and a 100-step cross-frame crawl, alongside existing BG/HUD regressions.
- Raster tests distinguish x=0.35 from x=0 at UI scale 4, retain logical size when
  PNG resolution doubles and compare tiled versus whole-glyph output.
- An ignored local renderer loaded the user's actual EVT and SYS JSONs/PNGs and
  rendered `pipsqueak.` with rounded versus exact advances for inspection. EVT's
  advance sum is 34.65 rather than 35; SYS's is 46.5 rather than 47. No JSONs,
  metrics, atlas files or decompilation sources were edited for these checks.
- The existing native target builds incrementally after source regeneration for
  the changed hooks. Elements, onescreen and references CTest checks pass; render
  skips without frame fixtures. Ruff, clang-format and whitespace checks pass.

The new renderer was checked with real font assets offscreen and synthetic game
layout/canvas tests; full in-game dialogue/menu visual verification remains.
No font regeneration or scale/advance edits are needed.

## Transition rasterization costs (2026-10-19)

Release profiling of the Saix tutorial-to-mission-selector transition identified
127-257 ms in `build_extra()` at UI scale 8, with negligible VRAM observation or
placement time. Tutorial population also spent up to 53 ms in `build_hd()`.
The reproduction log is local-only:
`build/port/obj-x64/saves/logs/session-20261019-011016.log`.

Supplementary map entries used to call `mapped_tile()` separately for every
visible copy, including repetitions of the same canvas character. An update now
renders each distinct source-character/palette/flip combination once and copies
the resulting base and HD tile rectangles to the other destinations. Reuse is
local to that update: it cannot outlive clears, ownership changes, palette changes,
scrolling or source invalidation. Existing dirty-tile checks still skip unchanged
destinations. Scratch storage is bounded by the 825 viewport cells, with no new
persistent images or glyph-position caches.

The fractional sampler computes vertical interpolation coordinates once per row
and resolves four texel pointers once per pixel instead of once per channel.
Nonnegative byte interpolation uses an exact half-byte comparison rather than
four CRT `lround()` calls per pixel. Layout coordinates and filtering remain
unchanged. A local synthetic 24,000-glyph Release workload dropped from about
2.98 seconds to 1.23 seconds; this is a raster microbenchmark, not an FPS claim.
Cold tile construction also computes the glyph's sampling bounds once instead of
per texel and uses the same exact byte rounding. A 4,000-glyph workload clearing
the cache every 200 glyphs dropped from 784 ms to 440 ms at output scale 4.

An in-game verification run (`session-20261019-011708.log`) confirmed 223-360
supplementary copies sharing one distinct tile update. Supplementary-stage times
dropped to 1-3 ms on two transition frames, with remaining peaks of 14 and 27 ms;
the user reported that transition felt smoother. Initial HUD loading still had
a visible spike, and tutorial raster peaks were 28-35 ms before the subsequent
cold-construction optimization. F12 marker 1 in this run precedes the tutorial;
it identifies initial gameplay/HUD population, not tutorial opening.

Regression coverage requires at most two glyph paints for a tile repeated across
768 map positions, including at scale 8, and checks scrolling, palette/flip
variants, refresh after a tint change and unchanged-frame caching. Another test
checks the reference byte hash for 240 fractional/clipped/tinted cases over
translucent destinations at scales 1, 2, 4 and 8. An ignored comparison helper
also checks each byte against the pre-optimization sampler.

`KHDAYS_PROFILE_TEXT=1` enables opt-in Windows elapsed-clock measurements of text
submission: VRAM observation, placement, supplementary mapping, base images and
HD rasterization. Frames taking at least 2 ms go into the normal session log.
Large supplementary updates also report visible copies versus distinct rendered
tiles. Leave the variable unset for normal play; no source regeneration is needed
for these port-side changes.

### Scoped follow-up and no-mod control

The primary DS canvas's output-scale expansion predates this font work. Its
behavior remains unchanged. The supplementary DS plane, which this font work
added, now stores native-resolution indexed pixels; the existing compositor
scales them to the element's logical rectangle. Tests compare that result with
the previous expanded plane at scales 1-8, both bpp modes, all flips and partial
ownership masks.

The added HD path skips flip loops when no flip is requested, masks unowned
regions as logical-pixel blocks, and skips ownership/flip work after clearing
unmapped tiles. A local test of 20 forced canvas redraws at scale 8 dropped from
256 ms to 46 ms with the same output hash. Primary HD images are allocated only
when captured artwork intersects a visible primary tile. `hd_ready` records
successful replacement masking even when no primary HD image is needed; this
keeps supplementary-only text and zero-width glyphs correct. That branch consumes
the primary dirty bit after the supplementary update so unchanged frames stay
cached. Tests cover supplementary-only text, empty selections, placement changes
and zero-width masking.

The normalized glyph cache stores bounds of nonzero PNG alpha and detects exact
grayscale/white-mask data. Painting skips transparent padding conservatively,
including bilinear neighbors, reuses equal channel samples and short-circuits
opaque source-over or white text over a transparent destination. These are PNG
properties, never original NFTR ink comparisons. The cache remains capped at
32 MiB of pixels and 8,192 entries. A 480-case reference test includes sparse
padding; an exhaustive white-mask test checks every source alpha at six
opacities. Local renders using the real EVT and SYS assets match the earlier
renderer byte for byte.

Controlled rendered runs use a copied save/settings file and the same scripted
load at the player's internal resolution of 10 (UI text clamps to 8). Initial HUD
text preparation measured 3 ms with no mods and 3 ms with HD fonts disabled
while the other mods stayed active. With HD text enabled it measured 22 ms after
the tile fast paths and 20 ms after PNG bounds, before the final grayscale and
unused-primary-image changes. An 18.7 ms game-running burst also occurred without
mods; it must not be attributed solely to the font renderer. These controls live
under ignored `build/tests/text-perf-*` directories. The final build still needs
the player's in-game comparison; automatic game launches were stopped at their
request.

Temporary diagnostics in `host/render/gpu_hd.c` and `runtime/mods/textures.c`
found no significant texture-lock waits in the controlled HUD run; the initial
upload grew to 125 MiB and took 14 ms. Those files were restored exactly because
they were unchanged at the last commit. All retained edits stay within the
existing uncommitted HD-font work, with no mod assets or decompilation edits.
