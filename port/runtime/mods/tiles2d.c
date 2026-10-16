/* The 2D's tiles by name, and HD tile sheets from mods (tiles2d.h). */
#include "tiles2d.h"
#include "../movies/video.h"

#include "names.h"
#include "../core/origin.h"
#include "../core/rom.h"
#include "textures.h"
#include "../core/vram.h"
#include "../../host/render/png_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The areas mapped, where their pages are in the 2D memory (frame_package.h). */
static const struct {
    int area, first, pages;
} areas[] = {
    {KHDAYS_AREA_ABG, 0, 32},
    {KHDAYS_AREA_AOBJ, 32, 16},
    {KHDAYS_AREA_BBG, 48, 8},
    {KHDAYS_AREA_BOBJ, 56, 8},
};

/* What each page was mapped from, and its words. */
static struct {
    const u8 *at;
    u32 changes, version;
    int hd, valid;
} seen[KHDAYS_TILES2D_PAGES];
static uint32_t words[KHDAYS_TILES2D_PAGES][KHDAYS_TILES2D_PAGE_WORDS];
static uint32_t words_version[KHDAYS_TILES2D_PAGES];

static int trace = -1;

/* --- The sheets mods replace ------------------------------------------------------- */

typedef struct Sheet {
    char name[160];
    int replacement; /* textures.h id, or -1 */
    int looked;      /* whether a mod's was looked for (HD graphics on) */
    int dumped;      /* whether it was written out */
} Sheet;
static Sheet *sheets;
static int nsheets, cap_sheets;

/* Sheet `name` (from `file` at `offset`) met: written out the first time
 * when the dump is on; with `hd`, the replacement a mod has for it
 * (mods/<Mod>/graphics/<name>.png), read once -- -1 if none, or it cannot
 * be used (said once). */
static int sheet_met(const char *name, const char *file, u32 offset, const KhdaysTileSheet *sheet, int hd)
{
    char relative[200], path[260];
    const char *error = NULL;
    const char *dump = khdays_textures_dump_dir();
    const u32 rows = (sheet->tiles + sheet->columns - 1) / sheet->columns;
    int width = 0, height = 0, indexed, scale, id = -1;
    uint8_t *pixels;
    uint32_t *texels;
    Sheet *s = NULL;
    for (int i = 0; i < nsheets && s == NULL; ++i) {
        if (strcmp(sheets[i].name, name) == 0)
            s = &sheets[i];
    }
    if (s == NULL) {
        if (nsheets == cap_sheets) {
            Sheet *grown = (Sheet *)realloc(sheets, sizeof(Sheet) * (size_t)(cap_sheets ? cap_sheets * 2 : 64));
            if (grown == NULL)
                return -1;
            sheets = grown;
            cap_sheets = cap_sheets ? cap_sheets * 2 : 64;
        }
        s = &sheets[nsheets++];
        snprintf(s->name, sizeof(s->name), "%s", name);
        s->replacement = -1;
        s->looked = s->dumped = 0;
        if (trace) {
            fprintf(stderr, "tiles: %s (%u tiles of %u bytes, %u a row)\n", name, sheet->tiles, sheet->tile_bytes,
                    sheet->columns);
        }
    }
    if (dump[0] != '\0' && !s->dumped) {
        s->dumped = 1;
        khdays_names_dump_sheet(file, offset, dump);
    }
    if (!hd || s->looked) {
        return hd ? s->replacement : -1;
    }
    s->looked = 1;
    snprintf(relative, sizeof(relative), "graphics/%s.png", name);
    if (!khdays_mods_find(relative, path, sizeof(path))) {
        return -1;
    }
    pixels = khdays_png_read_indexed(path, &width, &height, NULL, &error);
    indexed = pixels != NULL;
    if (pixels == NULL && error != NULL && strcmp(error, "not indexed") == 0) {
        pixels = khdays_png_read(path, &width, &height, &error);
    }
    if (pixels == NULL) {
        fprintf(stderr, "tiles: %s cannot be read (%s): the game's tiles are drawn\n", path, error);
        return -1;
    }
    scale = width / (int)(sheet->columns * 8u);
    if (scale < 1 || scale > 255 || width != scale * (int)(sheet->columns * 8u) || height < scale * (int)(rows * 8u)) {
        fprintf(stderr, "tiles: %s is %dx%d, not a whole multiple of its sheet's %ux%u: the game's tiles are drawn\n",
                path, width, height, sheet->columns * 8u, rows * 8u);
        khdays_png_free(pixels);
        return -1;
    }
    /* kept as long as the port runs: the renderer reads them from any thread */
    texels = (uint32_t *)malloc((size_t)width * (size_t)height * 4u);
    if (texels != NULL) {
        for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
            texels[i] = indexed ? pixels[i] : ((const uint32_t *)pixels)[i];
        }
        const KhdaysTextureReplacement *kept;
        id = khdays_textures_add_info(name, width, height, texels,
                                      KHDAYS_REPLACEMENT_TILES | (indexed ? KHDAYS_REPLACEMENT_INDEXED : 0u) |
                                          (uint32_t)scale << 8 | sheet->columns << 16);
        kept = khdays_textures_replacement(id);
        if (kept == NULL || kept->rgba != texels) {
            free(texels); /* not taken: no room for it, or the name had its texels already */
        }
    }
    khdays_png_free(pixels);
    s->replacement = id;
    fprintf(stderr, "tiles: %s drawn instead from %s (%dx%d, %d times, %s)\n", name, path, width, height, scale,
            indexed ? "the game's palettes" : "its own colours");
    return id;
}

/* --- A page ------------------------------------------------------------------------- */

static int bank_at(const u8 *at, u32 *offset)
{
    for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
        const u8 *base = khdays_vram_bank(bank);
        if (at >= base && at < base + khdays_vram_bank_size[bank]) {
            *offset = (u32)(at - base);
            return bank;
        }
    }
    return -1;
}

static void map_page(const u8 *at, int bank, u32 bank_offset, int hd, int by_sheet, uint32_t *out)
{
    memset(out, 0, KHDAYS_TILES2D_PAGE_WORDS * 4u);
    if (bank < 0)
        return;
    for (u32 u = 0; u < KHDAYS_TILES2D_PAGE_WORDS;) {
        const char *file;
        u32 offset, run, units, entry;
        char name[160];
        KhdaysTileSheet sheet;
        int id;
        if (khdays_video_unit(bank, bank_offset + u * 32u, at + u * 32u, &entry)) {
            out[u++] = entry; /* a movie's frame, drawn as the port's picture (video.h) */
            continue;
        }
        if (!by_sheet || !khdays_origin_of_bank(bank, bank_offset + u * 32u, &file, &offset, &run) ||
            !khdays_names_tiles(file, offset, name, sizeof(name), &sheet)) {
            ++u;
            continue;
        }
        id = sheet_met(name, file, offset, &sheet, hd);
        /* the units the same span holds, while they are the sheet's */
        units = run / 32u > 0 ? run / 32u : 1u;
        for (u32 k = 0; k < units && u < KHDAYS_TILES2D_PAGE_WORDS; ++k, ++u) {
            const u32 at_sheet = offset + k * 32u - sheet.tiles_at;
            const u32 tile = at_sheet / sheet.tile_bytes;
            u32 length = sheet.tile_bytes;
            if (offset + k * 32u < sheet.tiles_at || tile >= sheet.tiles)
                break;
            if (id < 0 || at_sheet % sheet.tile_bytes != 0)
                continue;
            if (length > (KHDAYS_TILES2D_PAGE_WORDS - u) * 32u)
                length = (KHDAYS_TILES2D_PAGE_WORDS - u) * 32u;
            if (memcmp(at + u * 32u, sheet.data + (size_t)tile * sheet.tile_bytes, length) != 0) {
                continue; /* the game drew over it: its own */
            }
            out[u] = (uint32_t)(id + 1) << 20 | tile;
        }
    }
}

void khdays_tiles2d_map(const KhdaysVramPages *pages, const uint32_t *memory_version, uint32_t *out,
                        uint32_t *out_version)
{
    const int hd = khdays_textures_hd();
    const int dump = khdays_textures_dump_dir()[0] != '\0';
    const int movies = khdays_video_kept();
    if (trace < 0) {
        const char *v = getenv("KHDAYS_TRACE_TILES");
        trace = v != NULL && *v != '\0' && *v != '0';
    }
    for (size_t a = 0; a < sizeof(areas) / sizeof(areas[0]); ++a) {
        for (int k = 0; k < areas[a].pages; ++k) {
            const int n = areas[a].first + k;
            const u8 *at = pages->page[areas[a].area][k];
            u32 bank_offset = 0, changes = 0;
            const int bank = at != NULL ? bank_at(at, &bank_offset) : -1;
            uint32_t fresh[KHDAYS_TILES2D_PAGE_WORDS];
            if (bank >= 0)
                changes = khdays_origin_bank_changes(bank);
            if (!(seen[n].valid && seen[n].at == at && seen[n].changes == changes &&
                  seen[n].version == memory_version[n] && seen[n].hd == (hd | dump << 1 | movies << 2))) {
                seen[n].valid = 1;
                seen[n].at = at;
                seen[n].changes = changes;
                seen[n].version = memory_version[n];
                seen[n].hd = hd | dump << 1 | movies << 2;
                if ((hd || dump || movies) && at != NULL) {
                    map_page(at, bank, bank_offset, hd, hd || dump, fresh);
                } else {
                    memset(fresh, 0, sizeof(fresh));
                }
                if (memcmp(fresh, words[n], sizeof(fresh)) != 0) {
                    memcpy(words[n], fresh, sizeof(fresh));
                    ++words_version[n];
                }
            }
            if (out_version[n] != words_version[n]) {
                memcpy(out + (size_t)n * KHDAYS_TILES2D_PAGE_WORDS, words[n], sizeof(words[n]));
                out_version[n] = words_version[n];
            }
        }
    }
}

uint32_t khdays_tiles2d_bg_tile(int engine, uint32_t address)
{
    const size_t a = engine ? 2u : 0u; /* areas[]: engine A's layers, engine B's */
    const uint32_t page_bytes = KHDAYS_TILES2D_PAGE_WORDS * 32u, page = address / page_bytes;
    if (page >= (uint32_t)areas[a].pages) {
        return 0u;
    }
    return words[areas[a].first + (int)page][(address % page_bytes) / 32u];
}
