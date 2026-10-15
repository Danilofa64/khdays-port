/* Where the game's data comes from: which game file, and where in it, the bytes of main RAM and of
 * each VRAM bank hold, followed from the loads to the copies. It names the graphics the game uploads,
 * for replacements and the dump; data the game makes itself has no name.
 *
 * Followed through the game's loads (prepare.py HOOKS), the SDK's copies (hal/sdk_mi.c) and DMA
 * (io_regs.c). A store of the game's own C is not seen, so a user of a name checks the bytes too. */
#ifndef KHDAYS_ORIGIN_H
#define KHDAYS_ORIGIN_H

#include "../../hal/hal.h"

/* Whether the bytes are followed: on when something needs the names
 * (KHDAYS_TRACE_ORIGIN, the replacements, the dump). */
int khdays_origin_on(void);

/* The names wanted from now on, by a part of the port that reads them
 * (the HUD's pieces, ui/hud.c). */
void khdays_origin_want(void);

/* A game load: the file the ROM holds from `top` to `bottom` (FSFile's
 * top/bottom), read into `buffer` -- `size` bytes, LZ-decompressed when
 * `compressed`. */
void khdays_origin_loaded(u32 top, u32 bottom, const void *buffer, u32 size, int compressed);

/* `size` bytes copied from `source` to `destination`. */
void khdays_origin_copy(u32 source, u32 destination, u32 size);

/* `size` bytes at `destination` overwritten with nothing from a file (a fill). */
void khdays_origin_clear(u32 destination, u32 size);

/* How many times what bank `bank` holds has changed: a count to compare
 * with an earlier one. */
u32 khdays_origin_bank_changes(int bank);

/* Where bank `bank` (0 = A)'s byte at `offset` came from: the file's name
 * (as the FNT names it, with "@<hex>" for a part of a file and ".z" kept
 * for a decompressed one) and the offset into that file's data. 0 if no
 * file is known. */
int khdays_origin_of_bank(int bank, u32 offset, const char **file, u32 *file_offset, u32 *run);

/* The same for main RAM's byte at `address`. */
int khdays_origin_of_ram(u32 address, const char **file, u32 *file_offset, u32 *run);

#endif
