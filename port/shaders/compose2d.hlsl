// The port's own 2D: one screen at N times the DS's resolution.
//
// Each pixel is composed from the screen's frame description
// (port/gfx/frame.h) by port/gfx/compose.h, the same code the CPU runs in
// port/gfx/compose_cpu.c. At N x an output pixel takes the 2D of the DS
// pixel it lies in (so the DS's pixel art stays sharp) and the 3D of its own
// place in the 3D drawn at N x. In widescreen engine A's screen is KD_WIDE
// DS columns wider on each side (kd_pixel_wide).

#include "../gfx/frame.h"

// Eight buffers, the most SDL's GPU API binds to a pass:
StructuredBuffer<uint> desc : register(t0, space0);       // KHDAYS_DESC_WORDS, then the palette: the layers' 256 colours, the sprites'
StructuredBuffer<uint> bg_mem : register(t1, space0);     // the layers' memory, then its map of tiles drawn instead (runtime/mods/tiles2d.h)
StructuredBuffer<uint> obj_mem : register(t2, space0);    // the sprites', then theirs
StructuredBuffer<uint> bgext_mem : register(t3, space0);  // the extended palettes
StructuredBuffer<uint> objext_mem : register(t4, space0);
StructuredBuffer<uint> vram_mem : register(t5, space0);   // the bank a VRAM display shows
StructuredBuffer<uint> layer3d_mem : register(t6, space0); // the 3D at 1x (256x192), for source3d 1
StructuredBuffer<uint> hd : register(t7, space0);         // the replacements (host/render/gpu_hd.h)

[[vk::image_format("r32ui")]] RWTexture2D<uint> color3d : register(u0, space1); // raster3d's, for source3d 2
[[vk::image_format("r32ui")]] RWTexture2D<uint> attr3d : register(u1, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> out_screen : register(u2, space1);

cbuffer Params : register(b0, space2) {
    uint scale;         // N
    uint width;         // the screen and raster3d's targets, pixels
    uint height;
    uint source3d;      // 0 none, 1 layer3d_mem at 1x, 2 raster3d's targets at N x
    uint disp3dcnt;
    uint swap_params;
    uint clear_color;   // CLEAR_COLOR
    uint clear_depth;   // CLEAR_DEPTH
    uint fog_color;     // FOG_COLOR
    uint fog_offset;    // FOG_OFFSET
    uint fine;          // 1: the 3D's channels have 8 bits, and the screen is composed in 8 (KDF_FINE)
    uint offset3d;      // source3d 2: the 3D's columns left of the DS's (the one picture's, as wide as it)
    uint4 edge_colors;  // EDGE_COLOR, two entries per uint
    uint4 fog_table[2]; // FOG_TABLE, four entries per uint
};

#include "finish3d.hlsli"

static uint g_fx, g_fy; // the output pixel's place inside its DS pixel

uint layer3d_at(uint x, int row)
{
    if (source3d == 1u) {
        return layer3d_mem[(uint)row * 256u + x];
    }
    if (source3d == 2u) {
        return finished3d((int)((x + offset3d) * scale + g_fx), (int)((uint)row * scale + g_fy));
    }
    return 0u;
}

#define KD_FN
#define KD_VOID
#define KD_DESC(i) desc[(i)]
#define KD_BG8(a) ((bg_mem[(a) >> 2] >> (((a) & 3u) * 8u)) & 0xffu)
#define KD_BG16(a) ((bg_mem[(a) >> 2] >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_OBJ8(a) ((obj_mem[(a) >> 2] >> (((a) & 3u) * 8u)) & 0xffu)
#define KD_OBJ16(a) ((obj_mem[(a) >> 2] >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_BGEXT16(a) ((bgext_mem[(a) >> 2] >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_OBJEXT16(a) ((objext_mem[(a) >> 2] >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_PAL16(i) ((desc[KHDAYS_DESC_WORDS + ((i) >> 1)] >> (((i) & 1u) * 16u)) & 0xffffu)
#define KD_VRAM16(a) ((vram_mem[(a) >> 2] >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_3D(x, row) layer3d_at((x), (row))
#define KD_3D_KNOWN (source3d != 0u)
// the maps after the memories: engine A's layers' 512 KB and sprites' 256 KB, engine B's 128 KB each
#define KD_HD_BG(u) bg_mem[((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x20000u) + (u)]
#define KD_HD_OBJ(u) obj_mem[((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x10000u) + (u)]
#define KD_HD_FIRST(id) hd[(id) * 4u]
#define KD_HD_WIDTH(id) hd[(id) * 4u + 1u]
#define KD_HD_HEIGHT(id) hd[(id) * 4u + 2u]
#define KD_HD_INFO(id) hd[(id) * 4u + 3u]
#define KD_HD_TEXEL(i) hd[(i)]
#define KD_SUB_X (((float)g_fx + 0.5f) / (float)scale)
#define KD_SUB_Y (((float)g_fy + 0.5f) / (float)scale)
// the port's elements (gfx/elements.h): after the description and its palettes; their images are
// replacements (host/render/gpu_hd.h), their texels where the replacement's table puts them
#define KD_ELEMENT(i) desc[KHDAYS_DESC_WORDS + 256u + (i)]
#define KD_EL_TEXEL(image, i) hd[hd[(image) * 4u] + (i)]
#define KD_ASFLOAT(u) asfloat(u)
#define KD_LEFT_AT (KHDAYS_DESC_WORDS + 256u + KD_DESC(KD_ELEMENTS) * KE_WORDS)
#define KD_LEFT_OUT_CELL(c) (KD_DESC(KD_LEFT_OUT) != 0u ? (desc[KD_LEFT_AT + ((c) >> 5)] >> ((c) & 31u)) & 1u : 0u)
#define KD_LEFT_OUT_OBJ(i) (KD_DESC(KD_LEFT_OUT) != 0u ? (desc[KD_LEFT_AT + KE_LEFT_CELL_WORDS + ((i) >> 5)] >> ((i) & 31u)) & 1u : 0u)

#include "../gfx/compose.h"

float channel8(uint c6)
{
    return (float)((c6 << 2) | (c6 >> 4)) / 255.0f; // as to_rgb888
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    uint c6;
    if (id.x >= width || id.y >= height) {
        return;
    }
    g_fx = id.x % scale;
    g_fy = id.y % scale;
    c6 = KD_DESC(KD_WIDE) != 0u ? kd_pixel_wide((int)(id.x / scale), (int)(id.y / scale))
                                 : kd_pixel((int)(id.x / scale), (int)(id.y / scale));
    if ((KD_DESC(KD_FLAGS) & KDF_SPLIT) != 0u) {
        // the one-screen picture's source: K in 8 bits, its alpha 0 (not shown over
        // the 3D) or 1 + the 3D's factor x 254 (kd_split)
        out_screen[id.xy] = float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                                   (float)((c6 >> 16) & 255u) / 255.0f, (float)(c6 >> 24) / 255.0f);
        return;
    }
    if ((KD_DESC(KD_FLAGS) & KDF_FINE) != 0u) {
        out_screen[id.xy] = float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                                   (float)((c6 >> 16) & 255u) / 255.0f, 1.0f);
        return;
    }
    out_screen[id.xy] = float4(channel8(c6 & 63u), channel8((c6 >> 8) & 63u), channel8((c6 >> 16) & 63u), 1.0f);
}
