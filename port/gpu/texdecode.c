/* Decodes whole textures in the DS's formats (see texdecode.h). */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "texdecode.h"

void khdays_tex_decode(const KhdaysVramPages *vram, uint32_t param, uint32_t pltt, uint32_t *out)
{
    int width, height;
    khdays_tex_size(param, &width, &height);
    for (int t = 0; t < height; ++t) {
        for (int s = 0; s < width; ++s) {
            out[t * width + s] = khdays_tex_texel(vram, param, pltt, s, t);
        }
    }
}
