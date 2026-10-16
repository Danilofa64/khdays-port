/* The DS's 3D rendering engine in software (GBATEK, "DS 3D Video"): it draws a polygon list of the
 * port's geometry (gfx/g3d.h) into engine A's BG0 as the DS does, the reference the GPU renderer is
 * checked against; nothing of the game runs through it. Where GBATEK leaves the arithmetic open the
 * choice is marked: close to the hardware, not bit-exact. */
#ifndef KHDAYS_GPU3D_H
#define KHDAYS_GPU3D_H

#include "vram_map.h"
#include "../gfx/g3d.h"

#include <stdint.h>

/* `list` (in drawing order) into `out` (256x192: RGB 6 bits each in bits
 * 0-5/8-13/16-21, alpha 0-31 in 24-28) -- what the DS draws of it, without
 * the polygons its RAM would not have held -- with the rendering registers from
 * `io` (0x04000000: DISP3DCNT, the clear colour and depth, the edge colours,
 * the fog, the toon table, the alpha test) and textures from `vram`. */
void khdays_gpu3d_draw(const KhdaysG3dList *list, const uint8_t *io, const KhdaysVramPages *vram, uint32_t *out);

#endif
