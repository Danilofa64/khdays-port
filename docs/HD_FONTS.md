# HD font rendering

PNG fonts replace NFTR artwork through the existing mod loader. Menu-specific
font choices are documented in [Scoped HD font mappings](FONT_MAPPING.md).
No new third-party dependency is required.

## Files and glyph identity

Keep the original resource path under the mod's `fonts/` directory:

```text
mods/<Mod>/fonts/text/font_eu_10.nftr.png
mods/<Mod>/fonts/text/font_eu_10.nftr.json
```

The graphics dump exports these pairs, including every original glyph index,
its metrics and character map. The PNG and adjacent JSON must belong to the
same mod. Font files and mappings are cached until restart.

A minimal, illustrative source-sized atlas definition is:

```json
{
  "source": "text/font_eu_10.nftr",
  "format": "NFTR",
  "atlas": {"width": 24, "height": 32},
  "cell_width": 24,
  "cell_height": 32,
  "baseline": 25,
  "max_width": 24,
  "line_feed": 32,
  "scale": 0.35,
  "baseline_offset": 0,
  "glyphs": [
    {"index": 0, "x": 0, "y": 0, "w": 24, "h": 32,
     "left": 0, "width": 18, "advance": 20}
  ],
  "charmap": [{"codepoint": 65, "glyph": 0}],
  "kerning": []
}
```

`atlas.width/height` and glyph `x/y/w/h` use reference image coordinates. The
actual PNG may be uniformly larger, for example 48x64 for this 24x32 reference.
That changes sampling quality, not logical size. Atlas layouts are arbitrary;
glyph rectangles must stay inside the reference image. PNG RGB is tinted by the
game's palette color; PNG alpha supplies all replacement coverage. An empty or
transparent glyph remains a valid replacement.

Indices must be unique and contiguous from zero. A nonempty `charmap` binds
original NFTR codepoints to replacement indices, so array order can differ from
the original. Missing codepoints fall back. An empty map preserves legacy
index-based binding; it is unsuitable for routing between different NFTRs.
Aliases of one original glyph use the first original codepoint encountered.
Duplicate replacement codepoints are rejected.

`source` must match the selected resource, including its original subtree.
Leading NitroFS slashes and slash direction are normalized. A combined font such
as `text/font_eu_10all.nftr` needs its own matching pair; the loader does not infer
that `font_eu_10` supplies its characters.

## Logical metrics and sizing

Without `scale`, metrics remain in Days units and artwork keeps the original
compatibility fit inside the Days cell. Unchanged NFTR metrics preserve the
full-cell artwork, including originally blank pixels. Editing `width` clips its
logical drawing area; it never stretches a character. Baseline adjustments are
inactive in this mode.

An explicit `scale` opts into native source-font units. Supply the source cell,
bearings, widths, advances, line feed and kerning before scaling. One uniform
factor applies to all of them. Artwork fits inside the replacement source cell,
preserving its aspect ratio; the original Days bitmap cell does not limit its
size. For example, an 18x24 cell at scale 0.5 occupies 9x12 logical pixels, with
an advance of 18 becoming 9. A 2x or 4x PNG produces the same geometry.

| Field | Meaning |
| --- | --- |
| `left` | Horizontal bearing from the character origin |
| `width` | Logical drawing width after the bearing; clips, never stretches |
| `advance` | Cursor movement before the next character |
| `cell_width`, `cell_height` | Source cell used for uniform artwork sizing |
| `line_feed` | Multiline step; the primary font controls mixed-font lines |
| `baseline` | Source baseline aligned to the original Days baseline |
| `baseline_offset` | Optional additional vertical displacement in Days units |
| `max_width`, `bpp` | Export metadata; not replacement sizing controls |

For scaled fonts, vertical placement is:

```text
artwork_y = draw_y + days_baseline - baseline * scale + baseline_offset
```

The Days baseline is the signed CGLP baseline. Positive offsets move artwork
down; negative offsets move it up. The offset defaults to zero and is not scaled
again. Horizontal and multiline alignment resolve before this displacement.

Fractional advances, bearings, baseline positions and alignment remain precise
through capture and output-resolution rasterization. Integer game APIs round
only their final result. Measurement uses the clipped drawing extent after the
bearing, with cursor advances used between characters. Compatibility mode keeps
the original advance-based measurement behavior. Intentional game character
spacing and fixed counter-column steps remain in effect.

Optional pair kerning uses codepoints:

```json
"kerning": [{"left": 65, "right": 86, "adjust": -2}]
```

**Do not add `scale` to already scaled Days-unit metrics.** Existing KH2 packs
using Days-unit values can retain compatibility mode. To use native sizing,
regenerate their definitions with native KH2 cell dimensions and `fontinfo.bar`
bearings, drawing widths and advances, then specify one scale. The renderer does
not change mod definitions. PNG upscaling requires no metric regeneration.

## Per-glyph and secondary fallback

Reserve an internal slot without changing any later mapping:

```json
{"index": 42, "fallback": "original"}
```

This forces the requested Days NFTR glyph, with its native metrics and pixel-grid
rendering. It bypasses all HD artwork, including a secondary atlas. Other fields
may be omitted on that entry. Never remove an internal entry and renumber its
successors to represent a missing glyph.

An optional secondary pair supplies artwork for missing primary characters:

```text
mods/<Mod>/fonts/text/font_eu_10.nftr.fallback.png
mods/<Mod>/fonts/text/font_eu_10.nftr.fallback.json
```

Resolution is primary PNG, secondary PNG, then the originally requested NFTR.
Use `"fallback": "secondary"` to reserve a primary slot for this chain. Missing
primary map/array entries also try the secondary automatically. Blank artwork
and zero widths do not trigger fallback.

The secondary uses the same schema and original `source` without `.fallback`.
It requires a nonempty codepoint map, with its own contiguous indices; it can
contain just icons or unsupported characters. Its default `"metrics": "original"`
fits artwork inside the original Days cell and retains original positioning,
width and advance. Secondary metrics, scale and baseline do not alter this mode.
Use `"metrics": "custom"` to enable its own metrics and optional uniform scale.

A secondary-only pack is supported. Missing or invalid pairs continue through
the chain. Secondary entries may force `"original"` but cannot request another
secondary. Kerning never crosses primary/secondary/DS boundaries; custom secondary
kerning applies between two secondary characters. Native fallback never acquires
HD metrics or HD suppression.

Scoped routes prefer the alternate target's secondary. If absent, they inherit
the requested source's secondary, bound by codepoint. For example, routing
`font_eu_10` to `font_eu_10all` can still use `font_eu_10.nftr.fallback.*`.
There is at most one secondary provider; final DS fallback always uses the
originally requested NFTR.

## Capture, publication and compositing

The traced source is `external/khdays-decomp` commit
`a2b0478b87e3102732094c34f6ca89595f98ab93`. Relevant symbols include
`Font_LoadUTF16`, `Resource_BindByName`, `DrawGlyphLine`, `LetterChar`,
`ClearContinuous`, `ClearAreaLine`, and `include/nnsys/g2d.h`.
All adaptations live in the port; matching sources remain unchanged.

1. Resource binding associates a font handle and FINF identity with its original
   path. RAM provenance also recovers identities after SDK initialization or
   font-object copying. Resource release invalidates bindings.
2. Port generation hooks surround the original glyph draw. Its original body
   always runs. The bitmap address identifies the glyph index; original bitmap
   samples never determine HD visibility or coverage. `NNSG2dGlyph` contains
   two 32-bit references even on x64.
3. Each canvas retains captured commands, a source observation and text-free
   graphics. HD capture leaves preceding graphics intact. Native fallback
   publishes the game's DS pixels, including unchanged-valued pixels.
4. Character transfers publish independent graphics, commands and pixel ownership
   into the displayed VRAM state. Explicit clears and memory-copy/fill hooks
   handle equal-byte writes. Partial publication and overwrites retire only
   affected command regions; full resource copies preserve command provenance.
5. BG tilemaps establish placement, palette, flips and scrolling. Display-layer
   selection checks actual tilemap use, since upload and display BGs can differ.
   Primary images retain the existing canvas/layout anchor. Other placements
   share one viewport-sized supplementary graphics/HD pair that follows it.
6. Dirty graphics and HD tiles update incrementally. Game palette colors tint
   PNG glyphs. The compositor retains logical size, clipping, layer priority and
   host layout transforms.

Text-only canvases retain their clear fill in the graphics plane. Mixed canvases
retain the graphics that preceded text, including backgrounds beneath original
ink. Both use the same explicit separation; neither is classified by its glyph
silhouette or palette. A redesigned PNG can fill originally transparent pixels
or omit originally opaque pixels without exposing DS glyphs.

Scaled artwork bounds can exceed its native DS write footprint while remaining
inside the canvas clip. Clearing and scrolling update captured clips; enlarged
artwork does not claim ownership of neighboring storage. External overwrites
inside the native write footprint conservatively retire the affected draw.

Layout transforms in `font_layout.py` and `font_fractional.py` adapt
`Text_DrawGlyph`, `Text_DrawLine`, `Text_DrawAlignedLines`,
`NNSi_G2dFontGetStringWidth`, `NNSi_G2dFontGetTextRect`,
`Ov002_StepCrawlChar`, and ov008/ov025 `DrawRowCountDigits`.
Shared measurement/draw paths use the same metrics. Crawling text keeps a bounded
fractional residual, reset when its stream or expected cursor changes.
Scope provenance and individual menu draw paths are in [FONT_MAPPING.md](FONT_MAPPING.md).

### Placement regression evidence

At the pinned revision, `Ov002_BuildTileRow` and `Ov002_DrawPanelRowText` draw
command labels into one 8x24-tile 4-bpp BG1 canvas, uploaded at character offset
`0x1a00` and then freed. `Ov002_PanelRefreshRowHeader` and
`Ov002_PanelWriteSlotLabel` reposition its strips through tilemap entries;
selection neither redraws the glyphs nor changes them to OBJ text.

For primary source row 1, destination-minus-source tile origins are:

| Selected | Talk/primary | Magic | Items |
| --- | --- | --- | --- |
| Talk | (2,16) | (1,14) | (1,14) |
| Magic | (1,16) | (2,14) | (1,14) |
| Items | (1,16) | (1,14) | (2,14) |

Each label contributes sixteen cells. A single majority-origin overlay could
reject both Magic and Items when Magic was selected, even though Items did not
move. The supplementary image covers every remaining placement; tests also
cover primary source row 2 and all three labels simultaneously.

Portrait names expose a separate case: copied text enters character memory
through BG1 but is shown on BG2. Provenance-preserving CPU copies and actual
display-layer selection retain that text after the original canvas is freed.
Initial panel names/quantities test another distinction: late graphics writes
inside enlarged PNG overhang must preserve commands whose native DS write
footprint was not overwritten. Both cases are covered without forced redraws.

## Caching, bounds and diagnostics

Unchanged frames reuse images without painting or uploading. Primary tiles skip
glyph-color scans when neither their dirty bit nor palette changes. Palette
hashes are shared per engine during submission. Supplementary updates render
each distinct source tile/palette/flip once, then copy repeated placements.
Palette keys include extended-palette selectors and the displayed BG slot.
Serial submission reuses a fixed scratch tile; canvas membership uses a bitset.

The untinted premultiplied glyph cache is bounded to 32 MiB and 8,192 entries.
Keys exclude fractional translation, so subpixel positions do not multiply cache
entries. PNG alpha bounds and grayscale/white-mask fast paths reduce sampling
and blending. Each individual raster is also limited to 32 MiB.

Each canvas accepts at most 8,192 source or displayed command fragments and
512 KiB of storage per observation/graphics plane. Limits or allocation failures
return to native rendering; a full clear enables capture again. Supplementary
images cover at most 33x25 tiles. The existing dimension-based compositor image
pool has no new global GPU-memory budget or retirement policy.

Metadata limits include 16 MiB per JSON, 16,384 glyphs, 65,536 map entries or
kerning pairs, 16,384-pixel atlas sides and scale from 1/64 through 16. Invalid
metadata logs its path and reason, then preserves fallback. JSON numbers are
locale independent. Mappings add their own bounded route cache.

Set these environment variables before launch for diagnostics:

- `KHDAYS_TRACE_TEXT=1`: bindings, canvases, placement and overlay eligibility.
- `KHDAYS_PROFILE_TEXT=1`: slow text-stage timings in the normal session log.
- `KHDAYS_HD_FONT_DISABLE=1`: native-text control run with other mods retained.

Keep them unset for ordinary play. No source regeneration is needed for changing
mod files or these diagnostics. Regenerate only when generation hooks change.

## Verification and remaining limits

Run `port\tests\run_hd_fonts.cmd`, or append `asan` for AddressSanitizer.
It discovers VS2022 x64 and runs seven synthetic suites: camp scope selection,
canvas/VRAM behavior, layout following, metadata/routing, secondary fallback,
rasterization and generated SDK layout. The layout suite uses the existing
`build/port/gen-x64` generated sources; prepare them once for the current hooks.
Run `python port/tests/font_scope_hooks_test.py` for the generator-hook checks.
The assets test target `khdays-font-test` covers NFTR decoding and malformed input.

Coverage includes native KH2-sized cells over smaller Days cells, positive and
negative bearings, `pipsqueak.` at scale 0.35, multiline/center/right alignment,
fractional drift, doubled PNG resolution, mixed/consecutive DS fallback, initial
panel population, partial/equal-byte clears, copied portraits, overwritten VRAM,
all three command-menu selections, scrolling, palette/flips, detached canvases,
supplementary placement and bounded repeated-tile stress.

Before merging, visually smoke-test command selections (including Items while
Magic is selected), empty inventory slots, the first panel-menu draw, repeated
pause-menu scrolling, portrait names, tutorials, mission selection and HUD/results
in Story and Mission Mode. Compare native fallback and no-mod behavior as controls.
Existing user testing established that remaining transition microstutters also
occurred in the older build; do not attribute all game stalls to HD text.

Covered rendering uses line-layout 4/8-bpp BG character canvases. True OBJ glyph
replacement, movie glyph records, TTF/OTF and mod hot reload are not implemented.
Pre-rendered text graphics require texture mods. A raw, unhooked identical-byte
write cannot be observed from memory snapshots; any new such game path needs an
operation hook. The renderer must not infer it by comparing original glyph ink.
