/* The DS's 3D texture formats (GBATEK, "DS 3D Texture Formats"), read from
 * the texture and palette slots of VRAM. One copy of the rules, shared by the
 * software 3D renderer (gpu3d.c, per texel) and the GPU renderer's texture
 * cache (whole textures, texdecode.c). Moved here unchanged from gpu3d.c. */
#ifndef KHDAYS_TEXDECODE_H
#define KHDAYS_TEXDECODE_H

#include "vram_map.h"

#include <stdint.h>

static inline uint8_t khdays_tex8(const KhdaysVramPages *vram, uint32_t address)
{
    const uint8_t *page;
    address &= 0x7ffff;
    page = vram->page[KHDAYS_AREA_TEX][address / KHDAYS_VRAM_PAGE];
    return page ? page[address % KHDAYS_VRAM_PAGE] : 0;
}

static inline uint16_t khdays_tex16(const KhdaysVramPages *vram, uint32_t address)
{
    return (uint16_t)(khdays_tex8(vram, address) | khdays_tex8(vram, address + 1) << 8);
}

static inline uint16_t khdays_pal16(const KhdaysVramPages *vram, uint32_t address)
{
    const uint8_t *page;
    address %= 0x18000;
    page = vram->page[KHDAYS_AREA_TEXPAL][address / KHDAYS_VRAM_PAGE];
    return page ? (uint16_t)(page[address % KHDAYS_VRAM_PAGE] | page[address % KHDAYS_VRAM_PAGE + 1] << 8) : 0;
}

static inline int khdays_tex_wrap(int c, int size, int repeat, int flip)
{
    if (!repeat) {
        return c < 0 ? 0 : c >= size ? size - 1 : c;
    }
    if (flip) {
        const int period = c & (2 * size - 1);
        return period >= size ? 2 * size - 1 - period : period;
    }
    return c & (size - 1);
}

/* The size of the texture TEXIMAGE_PARAM `param` describes. */
static inline void khdays_tex_size(uint32_t param, int *width, int *height)
{
    *width = 8 << ((param >> 20) & 7);
    *height = 8 << ((param >> 23) & 7);
}

/* A texel: RGB 5 bits each in bits 0-14, alpha 0-31 in bits 16-20. `s` and
 * `t` are wrapped, clamped or flipped as `param` says. */
static inline uint32_t khdays_tex_texel(const KhdaysVramPages *vram, uint32_t param, uint32_t pltt, int s, int t)
{
    const uint32_t base = (param & 0xffff) * 8;
    const int ss = 8 << ((param >> 20) & 7), ts = 8 << ((param >> 23) & 7);
    const unsigned format = (param >> 26) & 7;
    const int zero_transparent = (param >> 29) & 1;
    const uint32_t pal = pltt * (format == 2 ? 8 : 16);
    uint32_t index;
    s = khdays_tex_wrap(s, ss, (param >> 16) & 1, (param >> 18) & 1);
    t = khdays_tex_wrap(t, ts, (param >> 17) & 1, (param >> 19) & 1);
    switch (format) {
    case 1: {
        const uint8_t v = khdays_tex8(vram, base + (uint32_t)(t * ss + s));
        const unsigned a3 = v >> 5;
        return (khdays_pal16(vram, pal + (v & 31) * 2) & 0x7fff) | (a3 * 4 + a3 / 2) << 16;
    }
    case 2: index = (khdays_tex8(vram, base + (uint32_t)(t * ss + s) / 4) >> (2 * (s & 3))) & 3; break;
    case 3: index = (khdays_tex8(vram, base + (uint32_t)(t * ss + s) / 2) >> (4 * (s & 1))) & 15; break;
    case 4: index = khdays_tex8(vram, base + (uint32_t)(t * ss + s)); break;
    case 5: {
        const uint32_t block = base + (uint32_t)((t / 4) * (ss / 4) + s / 4) * 4;
        const uint32_t bits = (uint32_t)khdays_tex8(vram, block + (t & 3)) >> (2 * (s & 3)) & 3;
        const uint32_t slot1 = 0x20000 + ((block & 0x1ffff) / 2) + ((block >= 0x40000) ? 0x10000 : 0);
        const uint16_t info = khdays_tex16(vram, slot1);
        const uint32_t pa = pltt * 16 + (info & 0x3fff) * 4;
        const unsigned mode = info >> 14;
        uint16_t c0, c1;
        if (bits == 3 && (mode == 0 || mode == 1)) {
            return 0;
        }
        if (bits < 2 || mode == 0 || mode == 2) {
            return (khdays_pal16(vram, pa + bits * 2) & 0x7fff) | 31u << 16;
        }
        c0 = khdays_pal16(vram, pa);
        c1 = khdays_pal16(vram, pa + 2);
        {
            uint32_t out = 0;
            for (int sh = 0; sh < 15; sh += 5) {
                const unsigned a = (c0 >> sh) & 31, b = (c1 >> sh) & 31;
                unsigned v;
                if (mode == 1)
                    v = (a + b) / 2;
                else if (bits == 2)
                    v = (a * 5 + b * 3) / 8;
                else
                    v = (a * 3 + b * 5) / 8;
                out |= v << sh;
            }
            return out | 31u << 16;
        }
    }
    case 6: {
        const uint8_t v = khdays_tex8(vram, base + (uint32_t)(t * ss + s));
        return (khdays_pal16(vram, pal + (v & 7) * 2) & 0x7fff) | (uint32_t)(v >> 3) << 16;
    }
    case 7: {
        const uint16_t v = khdays_tex16(vram, base + (uint32_t)(t * ss + s) * 2);
        return (v & 0x7fff) | (v & 0x8000 ? 31u << 16 : 0);
    }
    default: return 0x7fff | 31u << 16;
    }
    if (index == 0 && zero_transparent) {
        return 0;
    }
    return (khdays_pal16(vram, pal + index * 2) & 0x7fff) | 31u << 16;
}

/* The whole texture, width x height texels in the format above, rows top to
 * bottom (texdecode.c). */
void khdays_tex_decode(const KhdaysVramPages *vram, uint32_t param, uint32_t pltt, uint32_t *out);

#endif
