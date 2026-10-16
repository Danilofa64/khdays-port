/* The port's own 2D before the GPU draws it: the frame description
 * (frame.h) built from what the game asked for, and its composition on the
 * CPU (compose.h). Plain C: the runtime passes in the game's registers,
 * palettes, sprite attributes and the memory its layers read, laid out as on
 * the DS. */
#ifndef KHDAYS_GFX_DESCRIBE_H
#define KHDAYS_GFX_DESCRIBE_H

#include "frame.h"
#include "../gpu/vram_map.h"

#include <stdint.h>

/* What a screen carries from one frame to the next: each window's vertical
 * flag (set on the line matching its Y1, cleared on its Y2, counting all 263
 * lines -- GBATEK, "DS Window Glitches"). */
typedef struct KhdaysGfxLatch {
    uint8_t win_y[2];
} KhdaysGfxLatch;

/* What the game asked of engine `engine` (0 = A, 1 = B): `io` the I/O
 * registers from 0x04000000 (engine B's at +0x1000), `palette` 0x05000000,
 * `oam` 0x07000000. Its windows' flags move on through the frame's 263 lines. */
void khdays_gfx_describe(const uint8_t *io, const uint8_t *palette, const uint8_t *oam, int engine,
                         KhdaysGfxLatch *latch, uint32_t desc[KHDAYS_DESC_WORDS]);

/* A frame not drawn: only the windows' flags move on. */
void khdays_gfx_describe_skip(const uint8_t *io, int engine, KhdaysGfxLatch *latch);

/* Where a screen's layers and sprites read (the game's memory, as it maps its
 * banks), for the CPU's composition. */
typedef struct KhdaysGfxMemory {
    const KhdaysVramPages *vram;
    const uint8_t *palette;   /* 0x05000000: both engines' */
    const uint8_t *bank[4];   /* banks A-D, for a VRAM display */
    const uint32_t *layer3d;  /* 256x192, or NULL: no 3D */
    /* The port's elements (gfx/elements.h), as many as the description's
     * KD_ELEMENTS says, and their images' texels by KE_IMAGE (NULL: none) */
    const uint32_t *elements;
    const uint32_t *(*element_texels)(uint32_t image);
    /* What the composition leaves out: a bit for each cell of the text
     * layers' maps (gfx/elements.h KE_CELLS), and one for each of the 128
     * sprites (NULL: nothing) -- what the parts stand for. */
    const uint8_t *left_out_cells;
    const uint8_t *left_out_obj;
} KhdaysGfxMemory;

/* Widescreen, engine A's description: `columns` DS columns (256 <
 * columns, even), the HUD's pieces found as the 2D engine finds them
 * (gpu2d.h KhdaysGpu2dWide) -- the 8x8 cells layers 1-3 cover, grouped with
 * the cells they touch, a group wholly in the left half moved to the left
 * edge, one in the right half to the right edge, one across the middle
 * staying -- into KD_WIDE and KD_SIDES. Only for a screen showing the 3D
 * (the layers, layer 0 the 3D and shown); otherwise KD_WIDE stays 0. */
void khdays_gfx_describe_wide(uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory, int columns);

/* The screen `desc` describes, 256x192 as 0x00RRGGBB, composed on the CPU;
 * or only its pixels in [x0, x1) x [y0, y1) (the rest of `out` untouched). */
void khdays_gfx_compose_cpu(const uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory, uint32_t *out);
void khdays_gfx_compose_cpu_rect(const uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory,
                                 uint32_t *out, int x0, int y0, int x1, int y1);

/* One pixel as kd_pixel gives it (compose.h): the screen's 6-bit channels,
 * 8-bit with KDF_FINE, or with KDF_SPLIT the 2D apart from the 3D (kd_split);
 * 0 outside the screen. Safe from any thread. */
uint32_t khdays_gfx_pixel_cpu(const uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory, int x, int y);

#endif
