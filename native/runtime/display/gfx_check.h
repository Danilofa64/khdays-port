/* KHDAYS_GFX_CHECK=N: the port's own 2D beside the reference, at one VBlank in N. Those VBlanks are
 * drawn by the reference renderer (native/gpu/gpu2d.c) even headless, each screen also composed by
 * native/gfx, and the two compared pixel for pixel; the first differing frames are written out, and
 * every 600 VBlanks a count. At N = 1 on a headless walk the check can leave the game's thread no
 * time. */
#ifndef KHDAYS_GFX_CHECK_H
#define KHDAYS_GFX_CHECK_H

#include "../../gpu/gpu2d.h"
#include "../../hal/hal.h"

/* Whether the check runs (read once), and whether VBlank `frame` is one it
 * checks. */
int khdays_gfx_check_on(void);
int khdays_gfx_check_at(u32 frame);

/* Engine `engine`'s screen as the reference drew it (256x192, 0x00RRGGBB). */
void khdays_gfx_check_screen(const KhdaysGpuInput *in, int engine, u32 frame, const uint32_t *reference);

/* A frame the reference drew or skipped without the check (only its windows'
 * flags moved on). */
void khdays_gfx_check_skip(const KhdaysGpuInput *in, int engine);

#endif
