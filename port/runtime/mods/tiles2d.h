/* The 2D's tiles by name, and HD tile sheets from mods. A tile the layers or sprites draw is named by
 * where its bytes came from (origin.h): its game file's tile sheet (names.h,
 * "<file>[#<entry>]/chr/<n>") and its number there. A mod's PNG of that name
 * (mods/<Mod>/graphics/<name>.png, any whole multiple of the original's size) is drawn instead at its
 * own resolution: indexed, with the game's palettes (its fades and palette effects kept); in colour,
 * its colours. Only while the tile's bytes are still the sheet's: tiles the game draws over itself
 * (its text) stay its own. */
#ifndef KHDAYS_TILES2D_H
#define KHDAYS_TILES2D_H

#include "../../gpu/vram_map.h"

#include <stdint.h>

/* The map has a word for each 32 bytes (a tile of 4 bits a pixel; a tile
 * of 8 has its word at its first 32) of the 2D memory's first
 * KHDAYS_TILES2D_PAGES pages of 16 KB: engine A's layers and sprites,
 * engine B's (frame_package.h KHDAYS_2D_ABG to KHDAYS_2D_BOBJ). */
#define KHDAYS_TILES2D_PAGE_WORDS 512
#define KHDAYS_TILES2D_PAGES 64

/* The map of the tiles drawn instead into `out` (KHDAYS_TILES2D_PAGES *
 * KHDAYS_TILES2D_PAGE_WORDS words): where a replaced sheet's tile starts,
 * the replacement's id + 1 (textures.h) in bits 20-31 and the tile's number
 * in its sheet in bits 0-19; elsewhere 0. A page is mapped again when its
 * memory (`memory_version`: display.c's count per 2D page), its bank or its
 * bytes' origin changed; `out_version` moves with a page's words, the
 * package's copy as track_pages keeps it. Zeros while HD graphics are off. */
void khdays_tiles2d_map(const KhdaysVramPages *pages, const uint32_t *memory_version, uint32_t *out,
                        uint32_t *out_version);

/* The tile drawn instead at byte `address` of an engine's layers' 2D memory (0 engine A's, 1 engine
 * B's), as the last map has it: the replacement's id + 1 in bits 20-31 and the tile's number in its
 * sheet in bits 0-19; 0 when none. For the parts the port draws itself (ui/hud.c). */
uint32_t khdays_tiles2d_bg_tile(int engine, uint32_t address);

#endif
