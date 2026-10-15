/* ov024's MobiClip blit, which the decompilation keeps as hand-written ARM
 * (libs/mobiclip/video/asm_stubs/auto/Ov024_MobiClip_BlitRows.c, 0x02086004;
 * its header describes the algorithm), translated.
 *
 * The view it reads (+0x00 to +0x18): the luma plane, the chroma plane, the
 * destination, the destination's stride in bytes, the width in pixels, the
 * height in rows, and the ramp table.
 *
 * Each 2x2 luma quad and its chroma pair (Co, and Cg 128 bytes further in the
 * same 256-byte row) become four 15-bit pixels, through three windows into
 * one saturating ramp indexed signed (the table + 0x100): green at Cg, red at
 * Co - Cg, blue at red - 2 Co. The quad's anti-diagonal pixels index four
 * levels lower (the checkerboard dither). A word holds two pixels,
 * 0x8000 | R + (G << 5) + (B << 10) in each half, summed as the routine sums
 * them.
 *
 * Each pass does eight quads (the width counted down by 16) and two rows (the
 * height counted down by 2); both loops test the count after the step,
 * signed, as the routine does. */
#include "hal.h"


static u32 pixel(const u8 *red, const u8 *green, const u8 *blue, s32 luma)
{
    return (u32)red[luma] + ((u32)green[luma] << 5) + ((u32)blue[luma] << 10);
}

void Ov024_MobiClip_BlitRows(const u32 *view)
{
    const u8 *luma = (const u8 *)view[0];
    const u8 *chroma = (const u8 *)view[1];
    u8 *row0 = (u8 *)view[2];
    const u32 stride = view[3];
    const s32 width = (s32)view[4];
    s32 rows = (s32)view[5];
    const u8 *ramp = (const u8 *)view[6] + 0x100;
    u8 *row1 = row0 + stride;
    const s32 luma_gap = 0x200 - width;
    const s32 chroma_gap = 0x100 - (s32)((u32)width >> 1);
    const s32 dest_gap = (s32)(stride - (u32)width) * 2;
    do {
        s32 columns = width;
        do {
            for (int quad = 0; quad < 8; ++quad) {
                const s32 co = chroma[0] - 0x80;
                const s32 cg = chroma[0x80] - 0x80;
                const u8 *green = ramp + cg;
                const u8 *red = ramp + (co - cg);
                const u8 *blue = red - 2 * co;
                const s32 y00 = luma[0], y01 = luma[1] - 4, y10 = luma[0x100] - 4, y11 = luma[0x101];
                ++chroma;
                luma += 2;
                /* the low pixel with its 0x8000, the high one added, then
                 * 0x80000000 -- in the routine's order */
                *(u32 *)row0 = ((pixel(red, green, blue, y00) | 0x8000u) + (pixel(red, green, blue, y01) << 16)) |
                               0x80000000u;
                *(u32 *)row1 = ((pixel(red, green, blue, y10) | 0x8000u) + (pixel(red, green, blue, y11) << 16)) |
                               0x80000000u;
                row0 += 4;
                row1 += 4;
            }
            columns -= 16;
        } while (columns > 0);
        luma += luma_gap;
        chroma += chroma_gap;
        row0 += dest_gap;
        row1 += dest_gap;
        rows -= 2;
    } while (rows > 0);
}
