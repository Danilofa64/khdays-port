/* The game's movies (video.h). */
#include "video.h"
#include "../menus/newgame.h"
#include "../core/window.h"
#include "../../gfx/frame.h"
#include "mobiclip_frame_core.h" /* the decomp's libs/mobiclip/video/portable */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static LARGE_INTEGER decode_start, blit_start;
static LONGLONG decode_ticks, blit_ticks; /* QueryPerformanceCounter's */
static u32 frames;

/* KHDAYS_TRACE_VIDEO: each blit destination once (the front buffers the frame alarm hands the
 * decoder). */
static int trace = -1;
static u32 *seen;
static int nseen;

void khdays_video_decode_begin(void)
{
    QueryPerformanceCounter(&decode_start);
}

/* MobiClipDecoderState as the game keeps it: the decoder's layout, its
 * pointers 4-byte references. */
typedef struct {
    u32 pBitstream;
    u32 nWidth, nHeight;
    u32 apLuma[6], apChroma[6];
    u32 apCoefficientTables[2];
    u32 pClampTable;
    u32 bFormatVariant;
    u8 aPredictionModes[40];
    u32 aQuantScan8x8[64], aQuantScan4x4[16];
    int aTransformWorkspace[128];
    u32 nQuantizer;
    u32 pCoefficientTable;
    int aPredictedMotion[2];
    int aMotion[18][2];
} GameDecoderState;
_Static_assert(sizeof(GameDecoderState) == 0x454, "the game's MobiClipDecoderState is 0x454 bytes");

static void *host(u32 reference)
{
    return reference ? KHDAYS_W(reference) : NULL;
}

static u32 reference(const void *host_address)
{
    return KHDAYS_REF(host_address);
}

int khdays_video_decode(void *state)
{
    GameDecoderState *g = (GameDecoderState *)state;
    MobiClipDecoderState s;
    int consumed;
    s.pBitstream = (const unsigned char *)host(g->pBitstream);
    s.nWidth = g->nWidth;
    s.nHeight = g->nHeight;
    for (int i = 0; i < 6; ++i) {
        s.apLuma[i] = (unsigned char *)host(g->apLuma[i]);
        s.apChroma[i] = (unsigned char *)host(g->apChroma[i]);
    }
    for (int i = 0; i < 2; ++i) {
        s.apCoefficientTables[i] = (const unsigned char *)host(g->apCoefficientTables[i]);
    }
    s.pClampTable = (const unsigned char *)host(g->pClampTable);
    s.bFormatVariant = g->bFormatVariant;
    memcpy(s.aPredictionModes, g->aPredictionModes, sizeof(s.aPredictionModes));
    memcpy(s.aQuantScan8x8, g->aQuantScan8x8, sizeof(s.aQuantScan8x8));
    memcpy(s.aQuantScan4x4, g->aQuantScan4x4, sizeof(s.aQuantScan4x4));
    memcpy(s.aTransformWorkspace, g->aTransformWorkspace, sizeof(s.aTransformWorkspace));
    s.nQuantizer = g->nQuantizer;
    s.pCoefficientTable = (const unsigned char *)host(g->pCoefficientTable);
    memcpy(s.aPredictedMotion, g->aPredictedMotion, sizeof(s.aPredictedMotion));
    memcpy(s.aMotion, g->aMotion, sizeof(s.aMotion));
    consumed = MobiClip_DecodeFrameCore(&s);
    /* what the decoder records in the state (mobiclip_frame_core.h) */
    g->bFormatVariant = s.bFormatVariant;
    memcpy(g->aPredictionModes, s.aPredictionModes, sizeof(s.aPredictionModes));
    memcpy(g->aQuantScan8x8, s.aQuantScan8x8, sizeof(s.aQuantScan8x8));
    memcpy(g->aQuantScan4x4, s.aQuantScan4x4, sizeof(s.aQuantScan4x4));
    memcpy(g->aTransformWorkspace, s.aTransformWorkspace, sizeof(s.aTransformWorkspace));
    g->nQuantizer = s.nQuantizer;
    g->pCoefficientTable = reference(s.pCoefficientTable);
    memcpy(g->aPredictedMotion, s.aPredictedMotion, sizeof(s.aPredictedMotion));
    memcpy(g->aMotion, s.aMotion, sizeof(s.aMotion));
    return consumed;
}

void khdays_video_decode_end(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    decode_ticks += now.QuadPart - decode_start.QuadPart;
    ++frames;
}

void khdays_video_blit_begin(u32 destination, u32 stride, u32 height)
{
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_VIDEO") != NULL;
    }
    if (trace) {
        int known = 0;
        for (int i = 0; i < nseen; ++i) {
            known |= seen[i] == destination;
        }
        if (!known) {
            u32 *more = (u32 *)realloc(seen, sizeof(*seen) * (size_t)(nseen + 1));
            if (more == NULL) {
                return; /* only a trace's list: said no more */
            }
            seen = more;
            seen[nseen++] = destination;
            fprintf(stderr, "video: a frame blitted to 0x%08x-0x%08x (%u rows of %u bytes)\n", destination,
                    destination + stride * height, height, stride);
        }
    }
    QueryPerformanceCounter(&blit_start);
}

void khdays_video_blit_end(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    blit_ticks += now.QuadPart - blit_start.QuadPart;
}

static u32 rd(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

/* --- The movie's picture (video.h khdays_video_frame_blitted) ------------ */

#include "../mods/textures.h"
#include "video_mod.h"
#include "../core/vram.h"
#include "../../host/app/host.h"

typedef struct {
    int bank;
    u32 offset;  /* where the blit's destination is: the bank and the byte in it */
    u32 bytes;   /* stride x rows */
    u8 *pixels;  /* what the blit wrote there */
    int picture; /* the changing replacement drawn instead */
} KeptFrame;

static KeptFrame *kept;
static int nkept;

/* The VRAM bank and the byte in it an ARM9 address of the 2D's VRAM reaches
 * now (its area's 16 KB page, as the display hardware sees it). */
static int vram_place(u32 address, int *bank, u32 *offset)
{
    static const struct {
        u32 base, size;
        int area;
    } areas[] = {
        {0x06000000u, 0x80000u, KHDAYS_AREA_ABG},
        {0x06200000u, 0x20000u, KHDAYS_AREA_BBG},
        {0x06400000u, 0x40000u, KHDAYS_AREA_AOBJ},
        {0x06600000u, 0x20000u, KHDAYS_AREA_BOBJ},
    };
    const KhdaysVramPages *pages = khdays_vram_pages();
    for (size_t i = 0; i < sizeof(areas) / sizeof(areas[0]); ++i) {
        if (address >= areas[i].base && address < areas[i].base + areas[i].size) {
            const u32 at = address - areas[i].base;
            const u8 *p = pages->page[areas[i].area][at / KHDAYS_VRAM_PAGE];
            if (p == NULL) {
                return 0;
            }
            p += at % KHDAYS_VRAM_PAGE;
            for (int b = 0; b < KHDAYS_VRAM_BANKS; ++b) {
                const u8 *base = khdays_vram_bank(b);
                if (p >= base && p < base + khdays_vram_bank_size[b]) {
                    *bank = b;
                    *offset = (u32)(p - base);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static u32 clamp8(s32 v)
{
    return v < 0 ? 0u : v > 255 ? 255u : (u32)v;
}

void khdays_video_frame_blitted(u32 request)
{
    const u8 *luma = (const u8 *)KHDAYS_W(rd(request + 0x00)), *chroma = (const u8 *)KHDAYS_W(rd(request + 0x04));
    const u32 destination = rd(request + 0x08), stride = rd(request + 0x0c);
    const u32 width = rd(request + 0x10), rows = rd(request + 0x14);
    KeptFrame *k = NULL;
    int bank;
    u32 offset;
    uint32_t *texels;
    if (khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_DS) {
        return;
    }
    if (!vram_place(destination, &bank, &offset) || offset + stride * rows > khdays_vram_bank_size[bank]) {
        return; /* not in one bank the 2D shows */
    }
    for (int i = 0; i < nkept && k == NULL; ++i) {
        if (kept[i].bank == bank && kept[i].offset == offset && kept[i].bytes == stride * rows) {
            k = &kept[i];
        }
    }
    if (k == NULL) {
        char name[48];
        const int picture =
            (snprintf(name, sizeof(name), "movie@%08x", destination),
             khdays_textures_add_changing(name, (int)width, (int)rows,
                                          KHDAYS_REPLACEMENT_BITMAP | (stride / 32u) << 8 | rows << 16));
        if (picture < 0) {
            return;
        }
        {
            KeptFrame *more = (KeptFrame *)realloc(kept, sizeof(*kept) * (size_t)(nkept + 1));
            if (more == NULL) {
                return; /* the frame not kept: the next is tried as this one was */
            }
            kept = more;
        }
        k = &kept[nkept++];
        k->bank = bank;
        k->offset = offset;
        k->bytes = stride * rows;
        k->pixels = (u8 *)malloc(k->bytes);
        k->picture = picture;
        if (k->pixels == NULL) {
            --nkept; /* not kept: the next frame is tried as this one was */
            return;
        }
    }
    memcpy(k->pixels, khdays_vram_bank(bank) + offset, k->bytes);
    /* the game's frame index (BlitFrame's decoder, +0x9c, before its count
     * moves): a mod's movie shows its picture at that time instead */
    khdays_video_mod_frame(rd(request - 0x3c + 0x9c));
    if (khdays_video_mod_picture() >= 0) {
        return;
    }
    texels = khdays_textures_change_begin(k->picture);
    for (u32 y = 0; y < rows; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const s32 l = luma[y * 256u + x];
            const s32 co = chroma[(y / 2u) * 256u + x / 2u] - 0x80;
            const s32 cg = chroma[(y / 2u) * 256u + 0x80u + x / 2u] - 0x80;
            texels[y * width + x] = clamp8(l + co - cg) | clamp8(l + cg) << 8 | clamp8(l - co - cg) << 16 | 0xff000000u;
        }
    }
    khdays_textures_change_end(k->picture);
}

int khdays_video_unit(int bank, u32 offset, const u8 *bytes, u32 *entry)
{
    for (int i = 0; i < nkept; ++i) {
        const KeptFrame *k = &kept[i];
        if (k->bank == bank && offset >= k->offset && offset - k->offset < k->bytes) {
            const u32 at = offset - k->offset;
            if (at + 32u > k->bytes || memcmp(bytes, k->pixels + at, 32) != 0) {
                return 0;
            }
            const int mod_picture = khdays_video_mod_picture();
            *entry = (u32)((mod_picture >= 0 ? mod_picture : k->picture) + 1) << 20 | at / 32u;
            return 1;
        }
    }
    return 0;
}

int khdays_video_kept(void)
{
    return nkept > 0 && !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_DS);
}

void khdays_video_second(void)
{
    LARGE_INTEGER f;
    if (frames == 0) {
        return;
    }
    QueryPerformanceFrequency(&f);
    fprintf(stderr, "video: %u frames, decode %.1f ms, blit %.1f ms, in the last second\n", frames,
            (double)decode_ticks * 1000.0 / (double)f.QuadPart, (double)blit_ticks * 1000.0 / (double)f.QuadPart);
    frames = 0;
    decode_ticks = blit_ticks = 0;
}

int khdays_overlay_loaded(int id); /* overlays.c */

void khdays_video_subtitles_out(const u8 *registers, int engine, u32 *desc)
{
    enum { OV_OPENING = 12, OV_MOBICLIP = 24 };
    const u8 *e = registers + (engine ? 0x1000 : 0);
    const u16 bg0 = (u16)(e[8] | e[9] << 8), bg1 = (u16)(e[10] | e[11] << 8);
    const u16 bg2 = (u16)(e[12] | e[13] << 8), bg3 = (u16)(e[14] | e[15] << 8);
    if (khdays_subtitles_shown() || (e[0] & 7) != 3 ||
        !(khdays_overlay_loaded(OV_MOBICLIP) || khdays_overlay_loaded(OV_OPENING))) {
        return;
    }
    /* the set-up's writes, the bits it keeps (0x43: mosaic, priority) aside */
    if ((bg3 & ~0x40u) == 0x4287u && (bg0 & ~0x40u) == 0x0c00u && (bg1 & ~0x40u) == 0x0d01u &&
        (bg2 & ~0x40u) == 0x0e02u) {
        desc[KD_FLAGS] &= ~(7u << KDF_BG_ON_SHIFT);
    }
}
