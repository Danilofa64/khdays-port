/* The text part (text.h). The game draws its text into NitroSystem character canvases as ever, and the
 * SDK's loads (GX_LoadBGnChar, GXS_LoadBGnChar) put a canvas's characters into a BG layer's memory,
 * where its map shows them. The port follows each canvas from the surface that makes it to those
 * loads: what VRAM holds of it is an element's image, drawn at the internal resolution; where the map
 * shows it, the element's place; those map cells are left out of their layer. The game's state is the
 * same whatever the part's mode. */
#include "text.h"
#include "bg.h"
#include "elements.h"
#include "parts.h"
#include "../gpu/gpu2d.h"
#include "../runtime/core/io.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/vram.h"
#include "../runtime/core/window.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- The game's structures, as the DS lays them out ----------------------------- */

typedef struct DsCanvas { /* NNSG2dCharCanvas */
    uint32_t char_base;   /* u8 *: the canvas's characters */
    int32_t area_width;   /* in characters */
    int32_t area_height;
    uint8_t dst_bpp;      /* 4 or 8 */
    uint8_t reserved[3];
    uint32_t param;       /* characters per row of the canvas */
    uint32_t vtable;
} DsCanvas;

typedef struct DsFrame { /* TileTextRenderer_Init's Frame */
    uint16_t x, y, width, height, char_base, palette, field_0c, field_0e;
} DsFrame;

/* a TileTextRenderer's canvas at +0x04; a tile surface's list object's at +0x08 */
#define RENDERER_CANVAS 0x04u
#define SURFACE_OBJECT_CANVAS 0x08u

/* --- The canvases ------------------------------------------------------------------ */

typedef struct Canvas {
    KhdaysRef cc;          /* the NNSG2dCharCanvas */
    KhdaysRef buffer;      /* its characters' buffer */
    int area_w, area_h, bpp, param;
    uint32_t bytes;
    int engine, bg;        /* the layer its transfers went to */
    uint32_t vram;         /* where its first character is in the engine's BG memory */
    int pending;           /* it was transferred since the last VBlank */
    int in_vram;           /* VRAM holds what its transfers put there */
    uint8_t *seen;         /* what VRAM held of it at the VBlank after its last transfer */
    uint32_t version;      /* counts the changes of `seen` */
    uint16_t *cells;       /* for each of the image's cells, the map entry over it (0xffff: not the canvas's) */
    int image, image_scale;
    uint32_t image_version;
    int said_shown;        /* the trace: 1 reported where it is shown, 2 reported that it is not */
    int said_obj;          /* the trace: reported that it was loaded into the sprites' characters */
    char id[48];
} Canvas;

static Canvas *canvases;
static int canvas_count, canvas_room;
static int trace = -1;

static int tracing(void)
{
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_TEXT") != NULL;
    }
    return trace;
}

static void say(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "ui text: %s\n", line);
    khdays_log("ui text: %s", line);
}

static Canvas *find(KhdaysRef cc)
{
    for (int i = 0; i < canvas_count; ++i) {
        if (canvases[i].cc == cc) {
            return &canvases[i];
        }
    }
    return NULL;
}

static void forget(Canvas *c)
{
    free(c->seen);
    free(c->cells);
    khdays_ui_image_put(c->image);
    *c = canvases[--canvas_count];
}

/* The canvas for the game's structure at `cc_host`: made the first time, and
 * made again when its buffer or its size changed. */
static Canvas *canvas_of(const void *cc_host, const char *made_by)
{
    const DsCanvas *cc = (const DsCanvas *)cc_host;
    const KhdaysRef ref = KHDAYS_REF(cc_host);
    const int bpp = cc->dst_bpp == 8 ? 8 : 4;
    Canvas *c = find(ref);
    if (c != NULL && (c->buffer != cc->char_base || c->area_w != cc->area_width || c->area_h != cc->area_height ||
                      c->bpp != bpp || c->param != (int)cc->param)) {
        forget(c);
        c = NULL;
    }
    if (c == NULL) {
        if (canvas_count == canvas_room) {
            const int room = canvas_room ? canvas_room * 2 : 16;
            Canvas *grown = (Canvas *)realloc(canvases, (size_t)room * sizeof(Canvas));
            if (grown == NULL) {
                return NULL;
            }
            canvases = grown;
            canvas_room = room;
        }
        c = &canvases[canvas_count++];
        memset(c, 0, sizeof(*c));
        c->cc = ref;
        c->buffer = cc->char_base;
        c->area_w = cc->area_width > 0 ? cc->area_width : 0;
        c->area_h = cc->area_height > 0 ? cc->area_height : 0;
        c->bpp = bpp;
        c->param = (int)cc->param;
        c->bytes = (uint32_t)c->param * (uint32_t)c->area_h * (uint32_t)(8 * bpp);
        c->image = -1;
        snprintf(c->id, sizeof(c->id), "text.canvas");
        if (tracing()) {
            say("canvas %08x (%dx%d characters, %d bits, buffer %08x) made by %s", c->cc, c->area_w, c->area_h,
                c->bpp, c->buffer, made_by);
        }
    }
    return c;
}

/* --- Following the game's text surfaces ------------------------------------------- */

void khdays_ui_text_renderer(const void *renderer, int layer, const void *resource, const void *frame, int result)
{
    const DsFrame *f = (const DsFrame *)frame;
    Canvas *c = canvas_of((const uint8_t *)renderer + RENDERER_CANVAS, "TileTextRenderer_Init");
    (void)resource;
    (void)result;
    if (c != NULL && tracing()) {
        say("renderer %08x: canvas %08x for engine %c's BG%d, cells %u,%u %ux%u from tile %u, palette %u",
            KHDAYS_REF(renderer), c->cc, layer >= 4 ? 'B' : 'A', layer & 3, f->x, f->y, f->width, f->height,
            f->char_base, f->palette);
    }
}

void khdays_ui_text_renderer_gone(const void *renderer, int result)
{
    Canvas *c = find(KHDAYS_REF((const uint8_t *)renderer + RENDERER_CANVAS));
    (void)result;
    if (c != NULL) {
        if (tracing()) {
            say("renderer %08x let go (canvas %08x)", KHDAYS_REF(renderer), c->cc);
        }
        forget(c);
    }
}

void khdays_ui_text_surface_canvas(const void *surface, int current, const void *canvas_object)
{
    (void)surface;
    (void)current;
    if (canvas_object != NULL) {
        (void)canvas_of((const uint8_t *)canvas_object + SURFACE_OBJECT_CANVAS, "TileSurface_AddCanvas");
    }
}

/* A tile surface's canvases are let go with their surface (TileSurface_Release,
 * FreeAllListNodeSubBuffers: each canvas's buffer, then its object), which
 * tells nothing else: the canvas's structure, freed, still names its buffer
 * until the memory is used again. The memory a file is read into next can be
 * that buffer, and a load of the file's characters would then pass for the
 * canvas's: on the tutorial list (ov025), the localised heading's characters
 * (UI/cm/cmb_&.p2's) are read where the camp menu's canvas of 32x2 characters
 * was, and were drawn as its text, torn. A canvas whose buffer is freed is gone. */
void khdays_ui_text_freed(const void *heap, const void *block)
{
    const KhdaysRef ref = KHDAYS_REF(block);
    (void)heap;
    for (int i = 0; i < canvas_count;) {
        if (canvases[i].buffer == ref) {
            if (tracing()) {
                say("canvas %08x let go: its buffer %08x freed", canvases[i].cc, ref);
            }
            forget(&canvases[i]); /* the last one moved into its place */
        } else {
            ++i;
        }
    }
}

/* Follows a load of `size` bytes from `src` into the characters of engine
 * `engine`'s BG `bg`, at `offset` from their base (G2_GetBGnCharPtr's, as the
 * registers say now). */
static void chars_loaded(int engine, int bg, const void *src_host, uint32_t offset, uint32_t size)
{
    const KhdaysRef src = KHDAYS_REF(src_host);
    const uint32_t to = khdays_bg_chars_base(engine, bg) + offset; /* where the load wrote */
    Canvas *mine = NULL;
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (mine == NULL && src >= c->buffer && src < c->buffer + c->bytes &&
            ((const DsCanvas *)KHDAYS_W(c->cc))->char_base == c->buffer) {
            mine = c; /* (a canvas that was let go, whose memory is used again, no longer names it) */
        }
    }
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (c != mine && (c->in_vram || c->pending) && c->engine == engine && to < c->vram + c->bytes &&
            c->vram < to + size) {
            if (tracing()) {
                say("canvas %08x's characters (engine %c's BG memory +%u) written over by a load from %08x", c->cc,
                    engine ? 'B' : 'A', c->vram, src);
            }
            c->in_vram = c->pending = 0;
        }
    }
    if (mine != NULL) {
        const uint32_t at = src - mine->buffer;
        if (tracing() && (!(mine->in_vram || mine->pending) || mine->engine != engine || mine->bg != bg ||
                          mine->vram != to - at)) {
            say("canvas %08x loaded: engine %c's BG%d characters, from +%u, %u bytes, to +%u (BG memory +%u)",
                mine->cc, engine ? 'B' : 'A', bg, at, size, offset, to);
        }
        mine->engine = engine;
        mine->bg = bg;
        mine->vram = to - at;
        mine->pending = 1;
    }
}

void khdays_ui_text_chars_a0(const void *src, unsigned int offset, unsigned int size) { chars_loaded(0, 0, src, offset, size); }
void khdays_ui_text_chars_a1(const void *src, unsigned int offset, unsigned int size) { chars_loaded(0, 1, src, offset, size); }
void khdays_ui_text_chars_a2(const void *src, unsigned int offset, unsigned int size) { chars_loaded(0, 2, src, offset, size); }
void khdays_ui_text_chars_a3(const void *src, unsigned int offset, unsigned int size) { chars_loaded(0, 3, src, offset, size); }
void khdays_ui_text_chars_b0(const void *src, unsigned int offset, unsigned int size) { chars_loaded(1, 0, src, offset, size); }
void khdays_ui_text_chars_b1(const void *src, unsigned int offset, unsigned int size) { chars_loaded(1, 1, src, offset, size); }
void khdays_ui_text_chars_b2(const void *src, unsigned int offset, unsigned int size) { chars_loaded(1, 2, src, offset, size); }
void khdays_ui_text_chars_b3(const void *src, unsigned int offset, unsigned int size) { chars_loaded(1, 3, src, offset, size); }

/* A load into the sprites' characters: only reported for now (a canvas shown
 * by sprites is not an element yet). */
static void obj_loaded(int engine, const void *src_host, uint32_t offset, uint32_t size)
{
    const KhdaysRef src = KHDAYS_REF(src_host);
    if (!tracing()) {
        return;
    }
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        if (src >= c->buffer && src < c->buffer + c->bytes) {
            if (!c->said_obj) {
                say("canvas %08x loaded into engine %c's sprite characters: from +%u, %u bytes, to +%u", c->cc,
                    engine ? 'B' : 'A', src - c->buffer, size, offset);
                c->said_obj = 1;
            }
            return;
        }
    }
}

void khdays_ui_text_obj_a(const void *src, uint32_t offset, uint32_t size) { obj_loaded(0, src, offset, size); }
void khdays_ui_text_obj_b(const void *src, uint32_t offset, uint32_t size) { obj_loaded(1, src, offset, size); }

/* --- The elements -------------------------------------------------------------------- */

/* Compares `seen` with what VRAM holds (a bank not mapped reads as zeros) and,
 * with `take`, copies VRAM's bytes into it. Returns 1 when they were the
 * same. */
static int vram_holds(Canvas *c, int take)
{
    int same = 1;
    for (uint32_t off = 0; off < c->bytes;) {
        const uint32_t a = c->vram + off;
        const uint32_t room = KHDAYS_VRAM_PAGE - a % KHDAYS_VRAM_PAGE;
        const uint32_t n = room < c->bytes - off ? room : c->bytes - off;
        const uint8_t *p = khdays_bg_memory(c->engine, a);
        if (p != NULL ? memcmp(c->seen + off, p, n) != 0 : 0) {
            same = 0;
            if (!take) {
                return 0;
            }
            memcpy(c->seen + off, p, n);
        } else if (p == NULL) {
            for (uint32_t k = 0; k < n; ++k) {
                if (c->seen[off + k] != 0) {
                    same = 0;
                    if (!take) {
                        return 0;
                    }
                    memset(c->seen + off, 0, n);
                    break;
                }
            }
        }
        off += n;
    }
    return same;
}

/* The canvas's character that the map's `entry` shows, as its index among the
 * canvas's characters, or -1 if it is not one of the canvas's. */
static int canvas_char(const Canvas *c, const KhdaysBgLayer *l, uint32_t entry)
{
    const uint32_t char_size = (uint32_t)(8 * c->bpp);
    const uint32_t at = l->chars + (entry & 0x3ffu) * char_size;
    if (at < c->vram || at >= c->vram + c->bytes) {
        return -1;
    }
    return (int)((at - c->vram) / char_size);
}

typedef struct Origin {
    int cx, cy, cells;
} Origin;

static Origin *origins;
static int origin_room;

/* Where the screen shows the canvas: the cell where its first character
 * would be, as most of the screen's cells showing its characters agree (each
 * character in its row and column of the canvas, the way Tilemap_FillRect
 * lays a canvas out). Returns 0 when the screen shows none of it. */
static int shown_at(const Canvas *c, const KhdaysBgLayer *l, int *ox, int *oy)
{
    int count = 0, best = -1;
    const int x0 = l->hofs >> 3, y0 = l->vofs >> 3;
    const int x1 = (l->hofs + KHDAYS_SCREEN_W - 1) >> 3, y1 = (l->vofs + KHDAYS_SCREEN_H - 1) >> 3;
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            const int k = canvas_char(c, l, khdays_bg_map_entry(c->engine, l, cx, cy));
            int col, row, x, y, i;
            if (k < 0 || (col = k % c->param) >= c->area_w) {
                continue;
            }
            row = k / c->param;
            x = (((cx - col) % l->wt) + l->wt) % l->wt;
            y = (((cy - row) % l->ht) + l->ht) % l->ht;
            for (i = 0; i < count && (origins[i].cx != x || origins[i].cy != y); ++i) {
            }
            if (i == count) {
                if (count == origin_room) {
                    const int room = origin_room ? origin_room * 2 : 16;
                    Origin *grown = (Origin *)realloc(origins, (size_t)room * sizeof(Origin));
                    if (grown == NULL) {
                        continue;
                    }
                    origins = grown;
                    origin_room = room;
                }
                origins[count].cx = x;
                origins[count].cy = y;
                origins[count].cells = 0;
                ++count;
            }
            if (++origins[i].cells > (best >= 0 ? origins[best].cells : 0)) {
                best = i;
            }
        }
    }
    if (best < 0) {
        return 0;
    }
    *ox = origins[best].cx;
    *oy = origins[best].cy;
    return 1;
}

/* The canvas's image at the scale: each cell where the map shows one of its
 * characters (at its row and column from the origin) gets what VRAM holds of
 * it, with that cell's palette and flips; every other cell is transparent.
 * It is made again when VRAM, the cells or the scale changed. */
static void make_image(Canvas *c, const KhdaysBgLayer *l, int ox, int oy, int scale)
{
    const int w = c->area_w * 8, h = c->area_h * 8;
    const uint32_t char_size = (uint32_t)(8 * c->bpp);
    const size_t ncells = (size_t)c->area_w * (size_t)c->area_h;
    int changed = 0;
    uint32_t *texels;
    if (c->cells == NULL) {
        c->cells = (uint16_t *)malloc(ncells * sizeof(uint16_t));
        if (c->cells == NULL) {
            return;
        }
        memset(c->cells, 0xff, ncells * sizeof(uint16_t));
        changed = 1;
    }
    if (c->image < 0 || c->image_scale != scale) {
        khdays_ui_image_put(c->image);
        c->image = khdays_ui_image_get(w * scale, h * scale);
        c->image_scale = scale;
        changed = 1;
    }
    if (c->image < 0) {
        return;
    }
    for (int row = 0; row < c->area_h; ++row) {
        for (int col = 0; col < c->area_w; ++col) {
            const uint32_t entry = khdays_bg_map_entry(c->engine, l, ox + col, oy + row);
            const uint16_t cell = canvas_char(c, l, entry) == row * c->param + col ? (uint16_t)(entry & 0xfc00u) : 0xffffu;
            if (c->cells[row * c->area_w + col] != cell) {
                c->cells[row * c->area_w + col] = cell;
                changed = 1;
            }
        }
    }
    if (!changed && c->image_version == c->version) {
        return;
    }
    texels = khdays_ui_image_begin(c->image);
    if (texels == NULL) {
        return;
    }
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            const uint16_t cell = c->cells[(py / 8) * c->area_w + px / 8];
            uint32_t t = 0;
            if (cell != 0xffffu) {
                const uint8_t *ch = c->seen + ((uint32_t)(py / 8) * (uint32_t)c->param + (uint32_t)(px / 8)) * char_size;
                const int fx = (cell & 0x400u) ? 7 - (px & 7) : px & 7, fy = (cell & 0x800u) ? 7 - (py & 7) : py & 7;
                const uint32_t pal = (uint32_t)cell >> 12;
                if (c->bpp == 4) {
                    const uint32_t v = (ch[fy * 4 + fx / 2] >> ((fx & 1) * 4)) & 15u;
                    t = v != 0 ? KET_OPAQUE | (pal * 16u + v) : 0u;
                } else {
                    const uint32_t v = ch[fy * 8 + fx];
                    t = v != 0 ? KET_OPAQUE | KET_EXTENDED | pal << KET_EXT_PAL_SHIFT | v : 0u;
                }
            }
            for (int sy = 0; sy < scale; ++sy) {
                uint32_t *out = texels + (size_t)(py * scale + sy) * (size_t)(w * scale) + (size_t)px * (size_t)scale;
                for (int sx = 0; sx < scale; ++sx) {
                    out[sx] = t;
                }
            }
        }
    }
    khdays_ui_image_end(c->image);
    c->image_version = c->version;
}

static void emit(void)
{
    const int scale = khdays_ui_scale();
    for (int i = 0; i < canvas_count; ++i) {
        Canvas *c = &canvases[i];
        KhdaysUiElement e;
        KhdaysBgLayer l;
        int ox, oy, lw, lh, x, y;
        if (!c->in_vram && !c->pending) {
            if (((const DsCanvas *)KHDAYS_W(c->cc))->char_base != c->buffer) {
                forget(c); /* let go, and no longer in VRAM */
                --i;
            }
            continue;
        }
        if (c->bytes == 0 || c->area_w == 0) {
            continue;
        }
        if (c->seen == NULL && (c->seen = (uint8_t *)calloc(c->bytes, 1)) == NULL) {
            continue;
        }
        if (c->pending) {
            /* what its transfers put in VRAM */
            if (!vram_holds(c, 1)) {
                ++c->version;
            }
            c->pending = 0;
            c->in_vram = 1;
        } else if (!vram_holds(c, 0)) {
            if (tracing()) {
                say("canvas %08x's characters (engine %c's BG memory +%u) written over, not by a transfer", c->cc,
                    c->engine ? 'B' : 'A', c->vram);
            }
            c->in_vram = 0;
            continue;
        }
        if (!khdays_bg_text_layer(c->engine, c->bg, &l) || l.colors256 != (c->bpp == 8) || !shown_at(c, &l, &ox, &oy)) {
            if (tracing() && c->said_shown != 2) {
                say("canvas %08x (engine %c's BG%d, BG memory +%u) not on the screen", c->cc, c->engine ? 'B' : 'A',
                    c->bg, c->vram);
                c->said_shown = 2;
            }
            continue;
        }
        if (tracing() && c->said_shown != 1) {
            say("canvas %08x shown on engine %c's BG%d from cell %d,%d (%dx%d characters, %d bits)", c->cc,
                c->engine ? 'B' : 'A', c->bg, ox, oy, c->area_w, c->area_h, c->bpp);
            c->said_shown = 1;
        }
        make_image(c, &l, ox, oy, scale);
        if (c->image < 0) {
            continue;
        }
        if (strcmp(c->id, "text.canvas") == 0) { /* named after where it is first shown */
            snprintf(c->id, sizeof(c->id), "text.%c%d.%d.%d", c->engine ? 'b' : 'a', c->bg, ox, oy);
        }
        /* its cells on the layer, where the layer's scroll puts them, and
         * again one layer's size further, where the layer wraps onto the
         * screen */
        lw = l.wt * 8;
        lh = l.ht * 8;
        x = ((ox * 8 - l.hofs) % lw + lw) % lw;
        y = ((oy * 8 - l.vofs) % lh + lh) % lh;
        e.id = c->id;
        e.screen = c->engine;
        e.slot = (uint32_t)c->bg;
        e.w = (float)(c->area_w * 8);
        e.h = (float)(c->area_h * 8);
        e.image = c->image;
        for (int ex = x - lw; ex < KHDAYS_SCREEN_W; ex += lw) {
            for (int ey = y - lh; ey < KHDAYS_SCREEN_H; ey += lh) {
                if (ex + c->area_w * 8 > 0 && ey + c->area_h * 8 > 0) {
                    e.x = (float)ex;
                    e.y = (float)ey;
                    khdays_ui_emit(KHDAYS_UI_TEXT, &e);
                }
            }
        }
        /* leave out the game's drawing of it: the cells its characters are shown in */
        for (int row = 0; row < c->area_h; ++row) {
            for (int col = 0; col < c->area_w; ++col) {
                if (c->cells[row * c->area_w + col] != 0xffffu) {
                    khdays_ui_left_out_cell(KHDAYS_UI_TEXT, c->engine, c->bg, (ox + col) % l.wt, (oy + row) % l.ht);
                }
            }
        }
    }
}

void khdays_ui_text_init(void)
{
    khdays_ui_register(KHDAYS_UI_TEXT, emit);
}
