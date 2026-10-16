/* The 2D's tiles by name, and HD tile sheets from mods: a tile is named by its file's sheet and number
 * ("<file>[#<entry>]/chr/<n>"); a mod's PNG of that name is drawn instead at its own resolution
 * (indexed with the game's palettes, or in colour) while the tile's bytes are still the sheet's
 * (docs/RENDERING.md, "Mods' graphics"). */
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

/* The map of the tiles drawn instead into `out`: where a replaced sheet's tile starts, the replacement's
 * id + 1 in bits 20-31 and the tile's number in bits 0-19, else 0. A page is mapped again when its
 * memory, bank or origin changed; `out_version` moves with its words. Zeros while HD graphics are off. */
void khdays_tiles2d_map(const KhdaysVramPages *pages, const uint32_t *memory_version, uint32_t *out,
                        uint32_t *out_version);

/* The tile drawn instead at byte `address` of an engine's layers' 2D memory (0 engine A's, 1 engine
 * B's), as the last map has it: the replacement's id + 1 in bits 20-31 and the tile's number in its
 * sheet in bits 0-19; 0 when none. For the parts the port draws itself (ui/hud.c). */
uint32_t khdays_tiles2d_bg_tile(int engine, uint32_t address);

#endif
