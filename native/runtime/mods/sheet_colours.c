/* The colours the game draws each 2D sheet's tiles with (sheet_colours.h). */
#include "sheet_colours.h"

#include "../core/io.h"
#include "names.h"
#include "../core/origin.h"
#include "textures.h"
#include "../core/vram.h"
#include "../core/window.h"
#include "../../ui/bg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define LOOK_EVERY 8    /* VBlanks between looks at what is shown */
#define WRITE_EVERY 300 /* VBlanks between writes of what changed */
#define TILE_SETS 4     /* the colour sets a tile is counted with: the one most seen is written */
#define FIRST_EXTRA 17  /* the first row written for a set of colours that is not its row's most seen
                           (16 is the studio's greys for the tiles never seen) */

/* The colours of one palette row as the game had them -- 16, or 256 for 8-bit
 * tiles -- and the row (0-15; -1 for one read back under a row of its own).
 * Kept per tile, not per row: the same row holds other colours for other tiles
 * (the field HUD copies each slot's portrait palette into the slot's row). */
typedef struct ColourSet {
    int per, row;
    uint16_t *colours;
} ColourSet;
static ColourSet *sets;
static int nsets, cap_sets;

typedef struct Sheet {
    char name[160];
    u32 tiles;
    int per;               /* colours a row: 16, or 256 for a sheet of 8-bit tiles */
    int32_t *tile_set;     /* tiles x TILE_SETS: the colour sets each tile was drawn with, -1 none */
    uint16_t *tile_votes;  /* tiles x TILE_SETS: how often */
    int32_t *best;         /* each tile's set most often seen, -1 never seen */
    int changed;
} Sheet;

static Sheet *sheets;
static int nsheets, cap_sheets;
static u32 frames, written_at;

/* The colour sets this look read, by where it read them (palette memory
 * holds still during a look). */
typedef struct CachedSet {
    const u16 *from;
    int per, row, set;
} CachedSet;
static CachedSet *cached;
static int ncached, cap_cached;

/* Why a tile shown was not put down to a sheet (KHDAYS_TRACE_SHEET_COLOURS). */
enum { TRACED, NO_ORIGIN, NO_SHEET, OTHER_DEPTH, DRAWN_OVER, REASONS };
static const char *const reason_names[REASONS] = {"traced", "no file behind it", "no sheet there",
                                                  "a sheet of the other depth", "drawn over by the game"};

/* Each tile of the memory looked at, in this look: its sheet + 1 (0: none),
 * its number there and, when none, why. Engine A's sprites have the most
 * (256 KB, a word for each 32 bytes). */
typedef struct Named {
    uint32_t look;
    int sheet;
    u32 tile;
    int reason;
    const char *file;
    u32 offset;
} Named;
static Named named[0x40000 / 32];
static uint32_t look;

/* KHDAYS_TRACE_SHEET_COLOURS: per engine, its 4 layers and its sprites, the
 * tiles shown and why those not traced were not, by file (the first few). */
#define TRACE_FILES 6
static int trace = -1;
static struct {
    u32 reasons[REASONS];
    struct {
        const char *file;
        int reason;
        u32 count;
    } files[TRACE_FILES];
} traced[2][5];

static Sheet *sheet_named(const char *name, u32 tiles, int per)
{
    for (int i = 0; i < nsheets; ++i) {
        if (strcmp(sheets[i].name, name) == 0) return sheets[i].per == per ? &sheets[i] : NULL;
    }
    if (nsheets == cap_sheets) {
        const int grown_cap = cap_sheets ? cap_sheets * 2 : 64;
        Sheet *grown = (Sheet *)realloc(sheets, sizeof(Sheet) * (size_t)grown_cap);
        if (grown == NULL) return NULL;
        sheets = grown;
        cap_sheets = grown_cap;
    }
    Sheet *s = &sheets[nsheets];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->tiles = tiles;
    s->per = per;
    s->tile_set = (int32_t *)malloc((size_t)tiles * TILE_SETS * sizeof(int32_t));
    s->tile_votes = (uint16_t *)calloc((size_t)tiles * TILE_SETS, sizeof(uint16_t));
    s->best = (int32_t *)malloc((size_t)tiles * sizeof(int32_t));
    if (s->tile_set == NULL || s->tile_votes == NULL || s->best == NULL) {
        free(s->tile_set);
        free(s->tile_votes);
        free(s->best);
        return NULL;
    }
    memset(s->tile_set, 0xff, (size_t)tiles * TILE_SETS * sizeof(int32_t)); /* -1 */
    memset(s->best, 0xff, (size_t)tiles * sizeof(int32_t));
    ++nsheets;
    return s;
}

/* The set of these colours in row `row` (found or added), or -1 when there is no room. */
static int colour_set(int per, int row, const uint16_t *colours)
{
    for (int i = 0; i < nsets; ++i) {
        if (sets[i].per == per && sets[i].row == row && memcmp(sets[i].colours, colours, (size_t)per * 2u) == 0) return i;
    }
    if (nsets == cap_sets) {
        const int grown_cap = cap_sets ? cap_sets * 2 : 64;
        ColourSet *grown = (ColourSet *)realloc(sets, sizeof(ColourSet) * (size_t)grown_cap);
        if (grown == NULL) return -1;
        sets = grown;
        cap_sets = grown_cap;
    }
    sets[nsets].colours = (uint16_t *)malloc((size_t)per * 2u);
    if (sets[nsets].colours == NULL) return -1;
    memcpy(sets[nsets].colours, colours, (size_t)per * 2u);
    sets[nsets].per = per;
    sets[nsets].row = row;
    return nsets++;
}

static int bank_at(const u8 *at, u32 *offset)
{
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        const u8 *base = khdays_vram_bank(bank);
        if (base != NULL && at >= base && at < base + khdays_vram_bank_size[bank]) {
            *offset = (u32)(at - base);
            return bank;
        }
    }
    return -1;
}

/* The sheet and tile whose tile of `bytes` (32: 4 bits a pixel, 64: 8) at
 * `at` holds, its bytes still the sheet's, or -1. `slot`: where this look
 * keeps it; `where` (0-4: a layer, the sprites) counts it for the trace. */
static int tile_of(const u8 *at, u32 bytes, Named *slot, u32 *tile, int engine, int where)
{
    if (slot->look != look) {
        const char *file = NULL;
        u32 bank_offset, offset = 0, run;
        char name[160];
        KhdaysTileSheet sheet;
        const int bank = bank_at(at, &bank_offset);
        slot->look = look;
        slot->sheet = 0;
        slot->reason = NO_ORIGIN;
        if (bank >= 0 && khdays_origin_of_bank(bank, bank_offset, &file, &offset, &run)) {
            slot->reason = NO_SHEET;
            if (khdays_names_tiles(file, offset, name, sizeof(name), &sheet)) {
                slot->reason = OTHER_DEPTH;
                if (sheet.tile_bytes == bytes && offset >= sheet.tiles_at && (offset - sheet.tiles_at) % bytes == 0u &&
                    (offset - sheet.tiles_at) / bytes < sheet.tiles) {
                    slot->reason = DRAWN_OVER;
                    if (memcmp(at, sheet.data + (offset - sheet.tiles_at), bytes) == 0) {
                        const Sheet *s = sheet_named(name, sheet.tiles, bytes == 64u ? 256 : 16);
                        if (s != NULL) {
                            slot->reason = TRACED;
                            slot->sheet = (int)(s - sheets) + 1;
                            slot->tile = (offset - sheet.tiles_at) / bytes;
                        }
                    }
                }
            }
        }
        slot->file = file;
        slot->offset = offset;
    }
    if (trace) {
        ++traced[engine][where].reasons[slot->reason];
        for (int k = 0; slot->reason != TRACED && k < TRACE_FILES; ++k) {
            const char *f = slot->file != NULL ? slot->file : "no file";
            if (traced[engine][where].files[k].file == NULL) {
                traced[engine][where].files[k].file = f;
                traced[engine][where].files[k].reason = slot->reason;
            }
            if (strcmp(traced[engine][where].files[k].file, f) == 0 && traced[engine][where].files[k].reason == slot->reason) {
                ++traced[engine][where].files[k].count;
                break;
            }
        }
    }
    *tile = slot->tile;
    return slot->sheet - 1;
}

static int blank(const u8 *at, u32 bytes)
{
    for (u32 k = 0; k < bytes; ++k) {
        if (at[k] != 0) return 0;
    }
    return 1;
}

/* How often tile `tile` of `s` was counted with set `set`. */
static int votes_for(const Sheet *s, u32 tile, int set)
{
    for (int k = 0; k < TILE_SETS; ++k) {
        if (s->tile_set[(size_t)tile * TILE_SETS + (size_t)k] == set) return s->tile_votes[(size_t)tile * TILE_SETS + (size_t)k];
    }
    return 0;
}

/* Tile `tile` of sheet `index` drawn with the colours of set `set`. */
static void seen(int index, u32 tile, int set)
{
    Sheet *s = &sheets[index];
    int32_t *ids = &s->tile_set[(size_t)tile * TILE_SETS];
    uint16_t *votes = &s->tile_votes[(size_t)tile * TILE_SETS];
    int found = -1, least = 0;
    if (set < 0) return;
    for (int k = 0; k < TILE_SETS; ++k) {
        if (ids[k] == set) found = k;
        if (votes[k] < votes[least]) least = k;
    }
    if (found < 0) {
        found = least;
        ids[found] = set;
        votes[found] = 0;
    }
    if (votes[found] < 0xffffu) ++votes[found];
    if (s->best[tile] != set && (s->best[tile] < 0 || votes[found] > votes_for(s, tile, s->best[tile]))) {
        s->best[tile] = set;
        s->changed = 1;
    }
}

/* An extended palette's 256 colours: `area`'s memory at `offset`, or NULL where none is mapped. */
static const u16 *extended(int area, u32 offset)
{
    const u8 *page = khdays_vram_pages()->page[area][offset / KHDAYS_VRAM_PAGE];
    return page != NULL ? (const u16 *)(page + offset % KHDAYS_VRAM_PAGE) : NULL;
}

/* The set of the `per` colours at `from` (palette memory; NULL: none mapped,
 * all 0) in row `row`, read once a look. */
static int set_for(const u16 *from, int per, int row)
{
    uint16_t colours[256];
    for (int i = 0; i < ncached; ++i) {
        if (cached[i].from == from && cached[i].per == per && cached[i].row == row) return cached[i].set;
    }
    for (int k = 0; k < per; ++k) colours[k] = from != NULL ? (uint16_t)(from[k] & 0x7fffu) : 0;
    const int set = colour_set(per, row, colours);
    if (ncached == cap_cached) {
        const int grown_cap = cap_cached ? cap_cached * 2 : 64;
        CachedSet *grown = (CachedSet *)realloc(cached, sizeof(CachedSet) * (size_t)grown_cap);
        if (grown == NULL) return set;
        cached = grown;
        cap_cached = grown_cap;
    }
    cached[ncached].from = from;
    cached[ncached].per = per;
    cached[ncached].row = row;
    cached[ncached].set = set;
    ++ncached;
    return set;
}

/* The cells a text layer shows on the screen. */
static void look_at_layer(int engine, int bg)
{
    KhdaysBgLayer l;
    const uint32_t dispcnt = IO32(engine ? 0x1000u : 0u);
    if (!khdays_bg_text_layer(engine, bg, &l)) return;
    const u32 bytes = l.colors256 ? 64u : 32u;
    /* 8-bit cells: the extended palettes' slot of this layer (BG0 and BG1 may take 2 and 3) */
    const u32 cnt = IO16((engine ? 0x1000u : 0u) + 0x08u + 2u * (u32)bg);
    const u32 slot = bg < 2 && (cnt & 0x2000u) ? (u32)bg + 2u : (u32)bg;
    const int ext = l.colors256 && (dispcnt & 0x40000000u);
    ++look; /* the characters' base is the layer's own */
    for (int cy = 0; cy <= 192 / 8; ++cy) {
        for (int cx = 0; cx <= 256 / 8; ++cx) {
            const uint32_t entry = khdays_bg_map_entry(engine, &l, (l.hofs >> 3) + cx, (l.vofs >> 3) + cy);
            const uint32_t character = entry & 0x3ffu;
            const u8 *at = khdays_bg_memory(engine, l.chars + character * bytes);
            u32 tile;
            int index, row = (int)(entry >> 12);
            if (at == NULL || blank(at, bytes)) continue;
            index = tile_of(at, bytes, &named[character], &tile, engine, bg);
            if (index < 0) continue;
            if (!l.colors256) {
                seen(index, tile, set_for((const u16 *)KHDAYS_W((engine ? 0x05000400u : 0x05000000u) + (u32)row * 32u), 16, row));
            } else if (ext) {
                seen(index, tile,
                     set_for(extended(engine ? KHDAYS_AREA_BBGEXT : KHDAYS_AREA_ABGEXT, slot * 0x2000u + (u32)row * 0x200u), 256, row));
            } else { /* the 256 colours of the layers' palette, row 0 */
                seen(index, tile, set_for((const u16 *)KHDAYS_W(engine ? 0x05000400u : 0x05000000u), 256, 0));
            }
        }
    }
}

/* An affine layer of 8-bit tiles (BG2 or BG3): an affine one (byte entries, the
 * layers' palette) or an extended one (entries as a text layer's, its
 * extended palette), its cells the screen shows through its matrix. */
static void look_at_affine(int engine, int bg)
{
    static const int8_t family[8][4] = {
        {0, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 1}, {0, 0, 0, 2},
        {0, 0, 1, 2}, {0, 0, 2, 2}, {0, -1, 3, -1}, {-1, -1, -1, -1},
    };
    static uint8_t shown[128 * 128 / 8]; /* the cells shown, of a map up to 128 x 128 */
    const u32 io = engine ? 0x1000u : 0u;
    const uint32_t dispcnt = IO32(io), cnt = IO16(io + 0x08u + 2u * (u32)bg);
    const u32 mode = dispcnt & 7u;
    const int kind = engine && mode >= 6u ? -1 : family[mode][bg];
    const int ext = kind == 2;
    if (!(dispcnt & (0x100u << bg)) || (kind != 1 && kind != 2) || (ext && (cnt & 0x80u))) {
        return; /* not shown, not affine, or a bitmap */
    }
    const u32 cells = 16u << ((cnt >> 14) & 3u); /* across and down */
    const u32 map = (engine ? 0u : ((dispcnt >> 27) & 7u) * 0x10000u) + ((cnt >> 8) & 31u) * 0x800u;
    const u32 chars = khdays_bg_chars_base(engine, bg);
    const u32 regs = io + 0x20u + 0x10u * (u32)(bg - 2);
    const int32_t pa = (int16_t)IO16(regs), pb = (int16_t)IO16(regs + 2u), pc = (int16_t)IO16(regs + 4u),
                  pd = (int16_t)IO16(regs + 6u);
    const int32_t x0 = (int32_t)(IO32(regs + 8u) << 4) >> 4, y0 = (int32_t)(IO32(regs + 12u) << 4) >> 4;
    const int wrap = (cnt & 0x2000u) != 0;
    const u32 slot = (u32)bg; /* BG2 and BG3 take their own slots */
    memset(shown, 0, sizeof(shown));
    /* every fourth dot of the screen, through the matrix (8 fractional bits) */
    for (int y = 0; y < 192; y += 4) {
        for (int x = 0; x < 256; x += 4) {
            int32_t tx = (x0 + pa * x + pb * y) >> 8, ty = (y0 + pc * x + pd * y) >> 8;
            const int32_t size = (int32_t)cells * 8;
            if (wrap) {
                tx = ((tx % size) + size) % size;
                ty = ((ty % size) + size) % size;
            } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
                continue;
            }
            const u32 cell = (u32)(ty / 8) * cells + (u32)(tx / 8);
            shown[cell / 8] |= (uint8_t)(1u << (cell % 8));
        }
    }
    ++look;
    for (u32 cell = 0; cell < cells * cells; ++cell) {
        const u8 *e, *at;
        u32 entry, character, tile;
        int index, row = 0;
        if (!(shown[cell / 8] & (1u << (cell % 8)))) continue;
        e = khdays_bg_memory(engine, map + cell * (ext ? 2u : 1u));
        if (e == NULL) continue;
        entry = ext ? (u32)(e[0] | e[1] << 8) : e[0];
        character = entry & 0x3ffu;
        at = khdays_bg_memory(engine, chars + character * 64u);
        if (at == NULL || blank(at, 64u)) continue;
        index = tile_of(at, 64u, &named[character], &tile, engine, bg);
        if (index < 0) continue;
        if (ext && (dispcnt & 0x40000000u)) {
            row = (int)(entry >> 12);
            seen(index, tile,
                 set_for(extended(engine ? KHDAYS_AREA_BBGEXT : KHDAYS_AREA_ABGEXT, slot * 0x2000u + (u32)row * 0x200u), 256, row));
        } else {
            seen(index, tile, set_for((const u16 *)KHDAYS_W(engine ? 0x05000400u : 0x05000000u), 256, 0));
        }
    }
}

/* The engine's sprites that may show (not a window's, not a bitmap). */
static void look_at_sprites(int engine)
{
    /* a sprite's tiles across and down, by shape and size */
    static const uint8_t sizes[3][4][2] = {
        {{1, 1}, {2, 2}, {4, 4}, {8, 8}},
        {{2, 1}, {4, 1}, {4, 2}, {8, 4}},
        {{1, 2}, {1, 4}, {2, 4}, {4, 8}},
    };
    const uint32_t dispcnt = IO32(engine ? 0x1000u : 0u);
    const u16 *oam = (const u16 *)KHDAYS_W(0x07000000u + (engine ? 0x400u : 0u));
    const KhdaysVramPages *pages = khdays_vram_pages();
    const int area = engine ? KHDAYS_AREA_BOBJ : KHDAYS_AREA_AOBJ;
    const u32 area_pages = engine ? 8u : 16u;
    const int one_d = (dispcnt & 0x10u) != 0;
    const u32 boundary = 32u << ((dispcnt >> 20) & 3u);
    const u32 palette = engine ? 0x05000600u : 0x05000200u;
    if (!(dispcnt & 0x1000u)) return;
    ++look;
    for (int i = 0; i < 128; ++i) {
        const u16 a0 = oam[i * 4], a1 = oam[i * 4 + 1], a2 = oam[i * 4 + 2];
        const int shape = a0 >> 14, size = a1 >> 14, mode = (a0 >> 10) & 3, wide = (a0 & 0x2000u) != 0;
        const u32 bytes = wide ? 64u : 32u;
        int w, h, x, y, set, row = a2 >> 12;
        if ((a0 | a1 | a2) == 0 || (!(a0 & 0x100u) && (a0 & 0x200u)) || mode >= 2 || shape == 3) {
            continue; /* unused, hidden, a window's or a bitmap */
        }
        w = sizes[shape][size][0];
        h = sizes[shape][size][1];
        x = a1 & 0x1ff;
        if (x >= 256) x -= 512;
        y = a0 & 0xff;
        if (x >= 256 || x + w * 8 <= 0 || (y >= 192 && y + h * 8 <= 256)) continue; /* off the screen */
        if (!wide) {
            set = set_for((const u16 *)KHDAYS_W(palette + (u32)row * 32u), 16, row);
        } else if (dispcnt & 0x80000000u) { /* the sprites' extended palettes */
            set = set_for(extended(engine ? KHDAYS_AREA_BOBJEXT : KHDAYS_AREA_AOBJEXT, (u32)row * 0x200u), 256, row);
        } else {
            set = set_for((const u16 *)KHDAYS_W(palette), 256, 0);
        }
        for (int ty = 0; ty < h; ++ty) {
            for (int tx = 0; tx < w; ++tx) {
                /* 1D: the sprite's tiles one after the other; 2D: in a sheet 32 units (of 32 bytes) wide */
                const u32 address = one_d ? (u32)(a2 & 0x3ffu) * boundary + (u32)(ty * w + tx) * bytes
                                          : (((u32)(a2 & 0x3ffu) + (u32)ty * 32u + (u32)tx * (bytes / 32u)) & 0x3ffu) * 32u;
                const u8 *page = pages->page[area][(address / KHDAYS_VRAM_PAGE) % area_pages];
                const u8 *at;
                u32 tile;
                int index;
                if (page == NULL || address / 32u >= sizeof(named) / sizeof(named[0])) continue;
                at = page + address % KHDAYS_VRAM_PAGE;
                if (blank(at, bytes)) continue;
                index = tile_of(at, bytes, &named[address / 32u], &tile, engine, 4);
                if (index >= 0) seen(index, tile, set);
            }
        }
    }
}

static void make_directories(const char *path)
{
    char part[MAX_PATH];
    for (int i = 0; path[i] != '\0' && i < MAX_PATH - 1; ++i) {
        if ((path[i] == '/' || path[i] == '\\') && i > 0) {
            memcpy(part, path, (size_t)i);
            part[i] = '\0';
            CreateDirectoryA(part, NULL);
        }
    }
}

/* What the file already says (an earlier session's, another copy's) taken in
 * for the tiles this session has not seen: each with the colours of its row
 * in the file, counted once, so that what this session sees twice decides. */
static void take_written(const char *path, Sheet *s)
{
    static char line[256 * 5 + 64];
    int *of_row = NULL, nrows = 0; /* the set each row of the file is, -1 none */
    FILE *f = fopen(path, "r");
    if (f == NULL) return;
    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned first, last;
        int r, n = 0;
        if (sscanf(line, "row %d%n", &r, &n) == 1 && r >= 0) {
            uint16_t c[256];
            char *at = line + n, *end = NULL;
            int k = 0;
            for (; k < s->per; ++k, at = end) {
                const unsigned long v = strtoul(at, &end, 16);
                if (end == at) break;
                c[k] = (uint16_t)(v & 0x7fffu);
            }
            if (k != s->per) continue; /* another depth's colours: not this sheet's record */
            if (r >= nrows) {
                int *grown = (int *)realloc(of_row, sizeof(int) * (size_t)(r + 1));
                if (grown == NULL) break;
                of_row = grown;
                while (nrows <= r) of_row[nrows++] = -1;
            }
            of_row[r] = colour_set(s->per, r < 16 ? r : -1, c);
        } else if (sscanf(line, "%u-%u %d", &first, &last, &r) == 3 && r >= 0 && r < nrows && of_row[r] >= 0) {
            for (u32 t = first; t <= last && t < s->tiles; ++t) {
                if (s->best[t] >= 0) continue;
                s->best[t] = of_row[r];
                s->tile_set[(size_t)t * TILE_SETS] = of_row[r];
                s->tile_votes[(size_t)t * TILE_SETS] = 1;
            }
        }
    }
    free(of_row);
    fclose(f);
}

/* Its tiles' colours written: each set its tiles were most often drawn with as a
 * row -- under its own row's number for the one most tiles of that row have,
 * from FIRST_EXTRA on for the others --, then each tile's row. */
static void write_sheet(const char *dump, Sheet *s)
{
    char path[MAX_PATH];
    int *used = NULL, *count = NULL, *number = NULL, nused = 0, next = FIRST_EXTRA;
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.colours", dump, s->name);
    make_directories(path);
    take_written(path, s);
    for (u32 t = 0; t < s->tiles; ++t) {
        int i = 0;
        if (s->best[t] < 0) continue;
        while (i < nused && used[i] != s->best[t]) ++i;
        if (i == nused) {
            int *u = (int *)realloc(used, sizeof(int) * (size_t)(nused + 1));
            int *c = u != NULL ? (int *)realloc(count, sizeof(int) * (size_t)(nused + 1)) : NULL;
            if (u != NULL) used = u;
            if (c == NULL) goto done;
            count = c;
            used[nused] = s->best[t];
            count[nused++] = 0;
        }
        ++count[i];
    }
    number = (int *)malloc(sizeof(int) * (size_t)(nused > 0 ? nused : 1));
    if (number == NULL) goto done;
    for (int i = 0; i < nused; ++i) number[i] = -1;
    for (int r = 0; r < 16; ++r) {
        int top = -1;
        for (int i = 0; i < nused; ++i) {
            if (sets[used[i]].row == r && (top < 0 || count[i] > count[top])) top = i;
        }
        if (top >= 0) number[top] = r;
    }
    for (int i = 0; i < nused; ++i) {
        if (number[i] < 0) number[i] = next++;
    }
    f = fopen(path, "w");
    if (f == NULL) goto done;
    s->changed = 0;
    fprintf(f, "# the palette rows the game drew this sheet's tiles with, and their colours (BGR555)\n");
    for (int want = 0; want < next; ++want) {
        for (int i = 0; i < nused; ++i) {
            if (number[i] != want) continue;
            fprintf(f, "row %d", want);
            for (int k = 0; k < s->per; ++k) fprintf(f, " %04x", sets[used[i]].colours[k]);
            fprintf(f, "\n");
        }
    }
    for (u32 t = 0; t < s->tiles;) {
        u32 last = t;
        int i = 0;
        if (s->best[t] < 0) {
            ++t;
            continue;
        }
        while (i < nused && used[i] != s->best[t]) ++i; /* every tile's set is among them (above) */
        while (last + 1 < s->tiles && s->best[last + 1] == s->best[t]) ++last;
        if (i < nused) fprintf(f, "%u-%u %d\n", t, last, number[i]);
        t = last + 1;
    }
    fclose(f);
done:
    free(used);
    free(count);
    free(number);
}

static void report(void)
{
    static const char *const where[5] = {"BG0", "BG1", "BG2", "BG3", "sprites"};
    for (int e = 0; e < 2; ++e) {
        for (int w = 0; w < 5; ++w) {
            u32 all = 0;
            for (int r = 0; r < REASONS; ++r) all += traced[e][w].reasons[r];
            if (all == 0) continue;
            fprintf(stderr, "sheet colours: %c %s: %u tiles shown", e ? 'B' : 'A', where[w], all);
            for (int r = 0; r < REASONS; ++r) {
                if (traced[e][w].reasons[r] > 0) fprintf(stderr, ", %u %s", traced[e][w].reasons[r], reason_names[r]);
            }
            for (int k = 0; k < TRACE_FILES && traced[e][w].files[k].file != NULL; ++k) {
                fprintf(stderr, "%s%s (%s) x%u", k == 0 ? "; not traced: " : ", ", traced[e][w].files[k].file,
                        reason_names[traced[e][w].files[k].reason], traced[e][w].files[k].count);
            }
            fprintf(stderr, "\n");
        }
    }
    memset(traced, 0, sizeof(traced));
}

void khdays_sheet_colours_frame(void)
{
    const char *dump = khdays_textures_dump_dir();
    if (dump[0] == '\0' || ++frames % LOOK_EVERY != 0) return;
    ncached = 0; /* the palettes may have changed since the last look */
    if (trace < 0) {
        const char *v = getenv("KHDAYS_TRACE_SHEET_COLOURS");
        trace = v != NULL && *v != '\0' && *v != '0';
    }
    for (int engine = 0; engine < 2; ++engine) {
        for (int bg = 0; bg < 4; ++bg) look_at_layer(engine, bg);
        look_at_affine(engine, 2);
        look_at_affine(engine, 3);
        look_at_sprites(engine);
    }
    if (frames - written_at < WRITE_EVERY) return;
    written_at = frames;
    if (trace) report();
    for (int i = 0; i < nsheets; ++i) {
        if (sheets[i].changed) write_sheet(dump, &sheets[i]);
    }
}
