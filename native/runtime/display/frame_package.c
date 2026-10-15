/* Reading a frame package back (frame_package.h). */
#include "frame_package.h"

uint32_t khdays_frame_pixel_again(const KhdaysFramePackage *p, const uint32_t *layer3d, int x, int y)
{
    const KhdaysGpu2dInfo *info = &p->info[0];
    /* the 3D layer scrolls over 512 dots, the second 256 transparent */
    const uint32_t sx = ((uint32_t)x + info->bg0_hofs) & 511;
    const uint32_t c = sx < KHDAYS_FRAME_W ? layer3d[y * KHDAYS_FRAME_W + sx] : 0;
    /* the 3D's 6-bit colour as the 2D engine takes it: 5 bits */
    const uint16_t c555 = (uint16_t)(((c >> 1) & 31) | ((c >> 9) & 31) << 5 | ((c >> 17) & 31) << 10);
    const uint16_t composed =
        khdays_gpu2d_recompose(&p->composition[y * KHDAYS_FRAME_W + x], c555, (c >> 24) & 31, info);
    return khdays_gpu2d_finish(composed, info);
}

int khdays_frame_check_composition(const KhdaysFramePackage *p, const uint32_t *layer3d, int *first)
{
    int bad = 0;
    *first = -1;
    for (int y = 0; y < KHDAYS_FRAME_H; ++y) {
        for (int x = 0; x < KHDAYS_FRAME_W; ++x) {
            const int i = y * KHDAYS_FRAME_W + x;
            if (khdays_frame_pixel_again(p, layer3d, x, y) != p->screens[0][i]) {
                if (*first < 0) {
                    *first = i;
                }
                ++bad;
            }
        }
    }
    return bad;
}
