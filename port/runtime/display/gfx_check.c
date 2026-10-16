/* The port's own 2D beside the reference (gfx_check.h). */
#include "gfx_check.h"
#include "../core/events.h"
#include "../../gfx/describe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define FRAMES_WRITTEN 4 /* differing frames written out as pictures */

static KhdaysGfxLatch latch[2];
static uint32_t desc[KHDAYS_DESC_WORDS];
static uint32_t mine[256 * 192];
static u32 frames_checked, frames_differing, pixels_differing, report_at = 600;
static int written;
static double ms_composing;

static int every = -1; /* N, or 0: no check */

int khdays_gfx_check_on(void)
{
    if (every < 0) {
        const char *value = getenv("KHDAYS_GFX_CHECK");
        every = value != NULL ? atoi(value) : 0;
        if (value != NULL && every < 1) {
            every = 1;
        }
    }
    return every != 0;
}

int khdays_gfx_check_at(u32 frame)
{
    return khdays_gfx_check_on() && frame % (u32)every == 0;
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)(v >> 8), f);
}

static void put32(FILE *f, unsigned v)
{
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

/* A 256x192 picture as a 24-bit BMP. */
static void write_bmp(const char *path, const uint32_t *pixels)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    fputc('B', f);
    fputc('M', f);
    put32(f, 54 + 256 * 192 * 3);
    put32(f, 0);
    put32(f, 54);
    put32(f, 40);
    put32(f, 256);
    put32(f, 192);
    put16(f, 1);
    put16(f, 24);
    put32(f, 0);
    put32(f, 256 * 192 * 3);
    put32(f, 2835);
    put32(f, 2835);
    put32(f, 0);
    put32(f, 0);
    for (int y = 191; y >= 0; --y) {
        for (int x = 0; x < 256; ++x) {
            const uint32_t c = pixels[y * 256 + x];
            fputc((int)(c & 0xff), f);
            fputc((int)((c >> 8) & 0xff), f);
            fputc((int)((c >> 16) & 0xff), f);
        }
    }
    fclose(f);
}

void khdays_gfx_check_screen(const KhdaysGpuInput *in, int engine, u32 frame, const uint32_t *reference)
{
    KhdaysGfxMemory memory = {0};
    u32 differing = 0;
    int first = -1;
    LARGE_INTEGER t0, t1, hz;
    QueryPerformanceCounter(&t0);
    khdays_gfx_describe(in->io, in->palette, in->oam, engine, &latch[engine], desc);
    memory.vram = in->vram;
    memory.palette = in->palette;
    for (int bank = 0; bank < 4; ++bank) {
        memory.bank[bank] = in->bank[bank];
    }
    memory.layer3d = engine == 0 ? in->layer3d : NULL;
    khdays_gfx_compose_cpu(desc, &memory, mine);
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&hz);
    ms_composing += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
    for (int i = 0; i < 256 * 192; ++i) {
        if ((mine[i] & 0xffffff) != (reference[i] & 0xffffff)) {
            if (first < 0) {
                first = i;
            }
            ++differing;
        }
    }
    ++frames_checked;
    if (differing != 0) {
        ++frames_differing;
        pixels_differing += differing;
        if (written < FRAMES_WRITTEN) {
            const char *dir = getenv("KHDAYS_SHOT_DIR");
            char path[512];
            ++written;
            fprintf(stderr,
                    "gfx check: frame %u, engine %c: %u pixels differ, the first at %d,%d (reference %06x, "
                    "the port %06x)\n",
                    frame, engine ? 'B' : 'A', differing, first % 256, first / 256, reference[first] & 0xffffff,
                    mine[first] & 0xffffff);
            snprintf(path, sizeof(path), "%s/gfxcheck_%05u_%c_reference.bmp", dir != NULL ? dir : ".", frame,
                     engine ? 'b' : 'a');
            write_bmp(path, reference);
            snprintf(path, sizeof(path), "%s/gfxcheck_%05u_%c_port.bmp", dir != NULL ? dir : ".", frame,
                     engine ? 'b' : 'a');
            write_bmp(path, mine);
        }
    }
    if (khdays_events_vblank_count() >= report_at) {
        report_at = khdays_events_vblank_count() + 600;
        fprintf(stderr,
                "gfx check: %u of %u screens differed (%u pixels) since the last report; %.1f ms to describe and "
                "compose a screen on the CPU\n",
                frames_differing, frames_checked, pixels_differing,
                frames_checked != 0 ? ms_composing / frames_checked : 0.0);
        fflush(stderr);
        frames_checked = frames_differing = pixels_differing = 0;
        ms_composing = 0.0;
    }
}

void khdays_gfx_check_skip(const KhdaysGpuInput *in, int engine)
{
    khdays_gfx_describe_skip(in->io, engine, &latch[engine]);
}
