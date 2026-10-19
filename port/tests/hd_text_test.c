/* Synthetic operation/VRAM tests; no game assets. Includes the implementation
 * to exercise canvas lifetime and transfers with a small fake DS memory. */
#include "../ui/text.c"

static unsigned char ram[65536], vram[65536];
unsigned char *khdays_window = ram;
int khdays_game_ram_ref(unsigned int reference)
{
    return reference > 0 && reference < sizeof(ram);
}
static const char *font_origin;
static uint32_t font_origin_info, font_origin_offset;
static int font_probes;
static int metric_override, text_mode = KHDAYS_UI_ON;
static int glyph_width_override = -1;
static int large_geometry;
static unsigned int available_glyphs = 1u;
static int baseline_shift;
int khdays_hd_font_baseline(const char *path, int index, int days_baseline)
{
    (void)path;
    (void)index;
    (void)days_baseline;
    return baseline_shift;
}
double khdays_hd_font_baseline_f(const char *path, int index, int days_baseline)
{
    return khdays_hd_font_baseline(path, index, days_baseline);
}
int khdays_ui_mode(int part)
{
    (void)part;
    return text_mode;
}
static int mapping_ready = 1, mapping_count;
static uint32_t mapped_codes[4];
int khdays_hd_font_map_ready(const char *path)
{
    (void)path;
    return mapping_ready;
}
void khdays_hd_font_bind_charmap(const char *path, const uint32_t *codes, int count)
{
    (void)path;
    mapping_count = count;
    if (count <= 4)
        memcpy(mapped_codes, codes, (size_t)count * sizeof(uint32_t));
}
int khdays_hd_font_metric(const char *path, int index, int field, int fallback)
{
    if (field == KHDF_SIZED)
        return large_geometry;
    if (path && index == 0 && field == KHDF_WIDTH && glyph_width_override >= 0)
        return glyph_width_override;
    static const int metrics[] = {-2, 13, 11, 17};
    return path && metric_override && index <= 0 && field >= 0 && field < 4 ? metrics[field] : fallback;
}
int khdays_hd_font_geometry(const char *path, int index, int days_w, int days_h, int ink_w, int *out)
{
    (void)path;
    (void)index;
    (void)days_w;
    (void)days_h;
    (void)ink_w;
    if (!large_geometry)
        return 0;
    out[0] = 18;
    out[1] = 24;
    out[2] = 18;
    return 1;
}
int khdays_hd_font_kerning(const char *path, unsigned int left, unsigned int right)
{
    return path && metric_override && left == 'A' && right == 'V' ? -2 : 0;
}
double khdays_hd_font_metric_f(const char *path, int index, int field, double fallback)
{
    return khdays_hd_font_metric(path, index, field, (int)fallback);
}
double khdays_hd_font_kerning_f(const char *path, unsigned int left, unsigned int right)
{
    return khdays_hd_font_kerning(path, left, right);
}
double khdays_hd_font_extent_f(const char *path, int index, int left, int width, int advance)
{
    return khdays_hd_font_extent(path, index, left, width, advance);
}
int khdays_hd_font_extent(const char *path, int index, int left, int width, int advance)
{
    (void)path;
    (void)index;
    (void)left;
    (void)width;
    return advance;
}
void khdays_origin_want(void) {}
int khdays_origin_of_ram(uint32_t at, const char **name, uint32_t *offset, uint32_t *run)
{
    if (!font_origin || at != font_origin_info)
        return 0;
    *name = font_origin;
    *offset = font_origin_offset;
    *run = 32;
    return 1;
}
static uint32_t *test_images[32];
static int image_ends, paint_calls, failures;
static int image_requests, test_scale = 1;
static int test_hofs, test_vofs, test_colors256;
static int test_map_bg = -1, test_bg_colors256[4] = {-1, -1, -1, -1};
static size_t image_bytes[32], live_bytes, peak_bytes;
static uint16_t map_cells[1024] = {1, 2};
static int map_width = 2, map_height = 1;
static KhdaysUiElement submissions[128];
static int submission_count;
static unsigned char suppressed[32 * 32];
static uint32_t tint = 0xff123456u;
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

KhdaysRef khdays_reference(const void *p)
{
    return (KhdaysRef)((const unsigned char *)p - ram);
}
void khdays_log(const char *format, ...)
{
    (void)format;
}
int khdays_hd_font_has(const char *path)
{
    ++font_probes;
    return path && strcmp(path, "text/missing.nftr") != 0;
}
int khdays_hd_font_glyph_available(const char *path, int index)
{
    (void)path;
    return index >= 0 && index < 32 && (available_glyphs & (1u << index)) != 0;
}
int khdays_ui_scale(void)
{
    return test_scale;
}
void khdays_ui_register(int part, void (*fn)(void))
{
    (void)part;
    (void)fn;
}
void khdays_ui_emit(int part, const KhdaysUiElement *e)
{
    (void)part;
    if (submission_count < 128)
        submissions[submission_count++] = *e;
}
void khdays_ui_emit_following(int part, const KhdaysUiElement *e, const KhdaysUiElement *anchor)
{
    (void)anchor;
    khdays_ui_emit(part, e);
}
uint32_t khdays_ui_palette_hash(int engine)
{
    (void)engine;
    return tint;
}
void khdays_ui_left_out_cell(int part, int screen, int bg, int x, int y)
{
    (void)part;
    (void)screen;
    (void)bg;
    if (x >= 0 && x < 32 && y >= 0 && y < 32)
        suppressed[y * 32 + x] = 1;
}
uint32_t khdays_ui_palette_colour(int engine, uint32_t slot, uint32_t indexed)
{
    (void)engine;
    (void)slot;
    return tint ^ ((indexed & 0xf0u) << 8);
}
const uint8_t *khdays_bg_memory(int engine, uint32_t at)
{
    (void)engine;
    return at < sizeof(vram) ? vram + at : NULL;
}
uint32_t khdays_bg_chars_base(int engine, int bg)
{
    (void)engine;
    (void)bg;
    return 0;
}
int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *l)
{
    (void)engine;
    memset(l, 0, sizeof(*l));
    l->map = (uint32_t)bg;
    l->wt = l->ht = 32;
    l->hofs = test_hofs;
    l->vofs = test_vofs;
    l->colors256 = test_bg_colors256[bg] < 0 ? test_colors256 : test_bg_colors256[bg];
    return 1;
}
uint32_t khdays_bg_map_entry(int engine, const KhdaysBgLayer *l, int x, int y)
{
    (void)engine;
    if (test_map_bg >= 0 && l->map != (uint32_t)test_map_bg)
        return 0;
    x = (x % 32 + 32) % 32;
    y = (y % 32 + 32) % 32;
    return y >= 0 && y < map_height && x >= 0 && x < map_width ? map_cells[y * map_width + x] : 0;
}
int khdays_ui_image_get(int w, int h)
{
    ++image_requests;
    const size_t bytes = (size_t)w * h * sizeof(uint32_t);
    if (bytes + live_bytes > 96u * 1024u * 1024u)
        return -1;
    for (int i = 0; i < 32; ++i)
        if (!test_images[i]) {
            test_images[i] = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
            image_bytes[i] = bytes;
            live_bytes += bytes;
            if (live_bytes > peak_bytes)
                peak_bytes = live_bytes;
            return i;
        }
    return -1;
}
void khdays_ui_image_put(int image)
{
    if (image >= 0) {
        free(test_images[image]);
        live_bytes -= image_bytes[image];
        image_bytes[image] = 0;
        test_images[image] = NULL;
    }
}
uint32_t *khdays_ui_image_begin(int image)
{
    return image >= 0 ? test_images[image] : NULL;
}
void khdays_ui_image_end(int image)
{
    (void)image;
    ++image_ends;
}
int khdays_hd_font_paint_clipped(const char *path, int index, double x, double y, int w, int h, uint32_t colour,
                                 uint32_t *out, int width, int height, int scale, int l, int t, int r, int b)
{
    (void)path;
    (void)index;
    (void)height;
    ++paint_calls;
    /* A replacement on the RIGHT half: original ink is on the LEFT half. */
    for (int py = t * scale; py < b * scale; ++py)
        for (int px = l * scale; px < r * scale; ++px)
            if (px >= (x + w / 2) * scale && px < (x + w) * scale && py >= y * scale && py < (y + h) * scale)
                out[(size_t)py * width + px] = colour;
    return 1;
}

static DsCanvas *cc;
static Canvas *c;
static uint32_t *font, *glyph;
static KhdaysBgLayer layer;

static uint32_t primary_hd_pixel(size_t offset)
{
    return c->hd_image >= 0 ? test_images[c->hd_image][offset] : 0;
}

static void setup(int bpp)
{
    while (canvas_count)
        forget(&canvases[0]);
    memset(ram, 0, sizeof(ram));
    memset(vram, 0, sizeof(vram));
    memset(map_cells, 0, sizeof(map_cells));
    memset(suppressed, 0, sizeof(suppressed));
    submission_count = 0;
    test_hofs = test_vofs = 0;
    test_map_bg = -1;
    for (int bg = 0; bg < 4; ++bg)
        test_bg_colors256[bg] = -1;
    test_scale = 1;
    test_colors256 = bpp == 8;
    metric_override = 0;
    mapping_ready = 1;
    mapping_count = 0;
    glyph_width_override = -1;
    large_geometry = 0;
    available_glyphs = 1u;
    baseline_shift = 0;
    text_mode = KHDAYS_UI_ON;
    font_origin = NULL;
    font_origin_info = font_origin_offset = 0;
    map_width = 2;
    map_height = 1;
    map_cells[0] = 1;
    map_cells[1] = 2;
    cc = (DsCanvas *)(ram + 16);
    cc->char_base = 1024;
    cc->area_width = 2;
    cc->area_height = 1;
    cc->param = 2;
    cc->dst_bpp = (uint8_t)bpp;
    font = (uint32_t *)(ram + 64);
    font[0] = 128;
    *(uint32_t *)(ram + 128 + 8) = 256;
    ram[256] = 8;
    ram[257] = 8;
    ram[258] = 8;
    glyph = (uint32_t *)(ram + 600);
    glyph[0] = 512;
    glyph[1] = 264;
    ram[513] = 4; /* original glyphWidth intentionally less than the PNG cell */
    binding_count = 0;
    bind_font(font, "text/synthetic.nftr");
    c = canvas_of(cc, "test");
    khdays_bg_text_layer(0, 0, &layer);
    layer.colors256 = bpp == 8;
    if (bpp == 8) {
        map_cells[0] = 1;
        map_cells[1] = 2;
    }
}

static void draw_xy(int x, int y)
{
    khdays_ui_text_glyph_before(cc, font, x, y, 1, glyph);
    for (int py = y; py < y + 8; ++py)
        for (int px = x; px < x + 4; ++px)
            put_pixel(c, ram + 1024, px, py, 1);
    khdays_ui_text_glyph(cc, font, x, y, 1, glyph);
}
static void draw(int x)
{
    draw_xy(x, 0);
}
static void upload(uint32_t at, uint32_t size)
{
    const uint32_t dest = 8u * c->bpp;
    memcpy(vram + dest + at, ram + 1024 + at, size);
    chars_loaded(0, 0, ram + 1024 + at, dest + at, size);
}
static void render(void)
{
    observe_vram(c);
    make_image(c, &layer, 0, 0, 1);
    build_hd(c, 1);
    make_image(c, &layer, 0, 0, 1);
}

static void scroll_text(int amount)
{
    const void *window = (const uint8_t *)cc - RENDERER_CANVAS;
    khdays_ui_text_scroll_before(window, amount);
    const int height = c->area_h * 8;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < c->area_w * 8; ++x)
            put_pixel(c, ram + 1024, x, y, y + amount < height ? pixel(c, ram + 1024, x, y + amount) : 0);
    khdays_ui_text_clear_area(cc, 0, 0, height - amount, c->area_w * 8, amount);
    khdays_ui_text_memory_clear(ram + 1024 + c->bytes - 1, 1);
    khdays_ui_text_scroll(window, amount);
}
static int displayed_at(int x, int y)
{
    for (int i = 0; i < c->display_count; ++i) {
        const GlyphOp *a = &c->display_ops[i];
        if (x >= a->clip_l && x < a->clip_r && y >= a->clip_t && y < a->clip_b)
            return 1;
    }
    return 0;
}

static int submitted_hd_pixel(int x, int y, uint32_t colour)
{
    for (int i = 0; i < submission_count; ++i) {
        const KhdaysUiElement *e = &submissions[i];
        if (!(e->slot & KES_DIRECT) || x < e->x || y < e->y || x >= e->x + e->w || y >= e->y + e->h)
            continue;
        const int sx = (x - (int)e->x) * test_scale, sy = (y - (int)e->y) * test_scale;
        if (test_images[e->image][sy * (int)e->w * test_scale + sx] == colour)
            return 1;
    }
    return 0;
}

/* The game's header layout: one 8x24 canvas, three 8x2 strips, selected
   indent two cells instead of one. Only tilemap entries change on selection. */
static void command_menu_selections(int primary_row)
{
    setup(4);
    cc->area_width = cc->param = 8;
    cc->area_height = 24;
    c = canvas_of(cc, "command row test");
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 8; ++col)
            draw_xy(col * 8, (row == 0 ? primary_row : row + 2) * 16 + 3);
    memcpy(vram + 0xd0 * 32, ram + 1024, c->bytes);
    chars_loaded(0, 1, ram + 1024, 0xd0 * 32, c->bytes);
    CHECK(c->display_count == 24);
    khdays_ui_text_freed(NULL, ram + 1024);
    CHECK(c->detached);
    memset(ram + 1024, 0xee, 6144);
    map_width = map_height = 32;
    uint16_t items_entries[16] = {0};
    for (int cycle = 0; cycle < 300; ++cycle) {
        const int selected = cycle % 3;
        memset(map_cells, 0, sizeof(map_cells));
        for (int row = 0; row < 3; ++row) {
            const int indent = row == selected ? 2 : 1;
            const int dst_y = 18 + row * 2;
            const int tile = 0xd0 + (row == 0 ? primary_row : row + 2) * 16;
            for (int dy = 0; dy < 2; ++dy)
                for (int col = 0; col < 8; ++col)
                    map_cells[(dst_y + dy) * 32 + indent + col] = (uint16_t)((tile + dy * 8 + col) | 0xf000);
        }
        for (int dy = 0; dy < 2; ++dy) {
            for (int col = 0; col < 8; ++col) {
                const uint16_t value = map_cells[(22 + dy) * 32 + 1 + col];
                if (selected == 0)
                    items_entries[dy * 8 + col] = value;
                if (selected == 1)
                    CHECK(items_entries[dy * 8 + col] == value);
            }
        }
        memset(suppressed, 0, sizeof(suppressed));
        submission_count = 0;
        emit();
        CHECK(c->extra != NULL);
        int ox = -1, oy = -1;
        CHECK(shown_at(c, &layer, &ox, &oy));
        for (int row = 0; row < 3; ++row) {
            const int indent = row == selected ? 2 : 1;
            const int source_y = (row == 0 ? primary_row : row + 2) * 2;
            const int origin_y = 18 + row * 2 - source_y;
            for (int dy = 0; dy < 2; ++dy)
                for (int col = 0; col < 8; ++col) {
                    const int x = indent + col, y = 18 + row * 2 + dy;
                    const int k = (source_y + dy) * 8 + col;
                    CHECK(canvas_char(c, &layer, map_cells[y * 32 + x]) == k);
                    CHECK(suppressed[y * 32 + x]);
                    if (ox == indent && oy == origin_y) {
                        CHECK(c->cells[k] == 0xf000 && c->hd_image >= 0);
                    } else {
                        CHECK(c->extra && c->extra->source[y * EXTRA_COLS + x] == k);
                        CHECK(c->extra && c->extra->cells[y * EXTRA_COLS + x] == 0xf000);
                        CHECK(c->extra && c->extra->hd_image >= 0);
                    }
                }
            CHECK(!submitted_hd_pixel(indent * 8 + 1, (18 + row * 2) * 8 + 3, tint ^ 0xf000u));
            CHECK(submitted_hd_pixel(indent * 8 + 7, (18 + row * 2) * 8 + 3, tint ^ 0xf000u));
        }
        CHECK(c->display_count == 24);
        CHECK(c->engine == 0 && c->bg == 1 && c->vram == 0x1a00);
        const int stable_uploads = image_ends, stable_paints = paint_calls;
        submission_count = 0;
        emit();
        CHECK(image_ends == stable_uploads && paint_calls == stable_paints);
    }
    /* Empty selections remove references while published HD glyphs still exist. */
    uint16_t saved_map[1024];
    memcpy(saved_map, map_cells, sizeof(saved_map));
    memset(map_cells, 0, sizeof(map_cells));
    submission_count = 0;
    memset(suppressed, 0, sizeof(suppressed));
    emit();
    CHECK(submission_count == 0 && c->extra == NULL && c->display_count == 24);
    for (int i = 0; i < 1024; ++i)
        CHECK(!suppressed[i]);
    memcpy(map_cells, saved_map, sizeof(saved_map));
    emit();
    /* Palette changes must reach every placement, including shared row origins. */
    tint ^= 0x00101010u;
    submission_count = 0;
    emit();
    for (int row = 0; row < 3; ++row)
        CHECK(submitted_hd_pixel((row == 2 ? 2 : 1) * 8 + 7, (18 + row * 2) * 8 + 3, tint ^ 0xf000u));
    /* Overwriting Magic must clear it without losing Talk or unchanged Items. */
    memset(vram + c->vram + 48 * 32, 0, 16 * 32);
    submission_count = 0;
    emit();
    CHECK(c->display_count == 16);
    CHECK(submitted_hd_pixel(15, 18 * 8 + 3, tint ^ 0xf000u));
    CHECK(!submitted_hd_pixel(15, 20 * 8 + 3, tint ^ 0xf000u));
    CHECK(submitted_hd_pixel(23, 22 * 8 + 3, tint ^ 0xf000u));
    /* A VRAM overwrite invalidates every cached placement of the same glyphs. */
    memset(vram + c->vram, 0, c->bytes);
    submission_count = 0;
    emit();
    CHECK(c->display_count == 0);
    for (int i = 0; i < submission_count; ++i)
        CHECK(!(submissions[i].slot & KES_DIRECT));
}

int main(void)
{
    /* Native-resolution supplementary DS planes equal the prior expanded planes. */
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        setup(bpp);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                put_pixel(c, ram + 1024, x, y, (unsigned)(x + y * 3) & (bpp == 4 ? 15u : 255u));
        upload(0, c->bytes);
        c->owned[0] = UINT64_C(0x0123456789abcdef);
        for (uint16_t flip = 0; flip <= 0xc00u; flip += 0x400u) {
            uint32_t native[64], expanded[64 * 64];
            const uint16_t cell = flip | 0x3000u;
            for (int hide = 0; hide <= 1; ++hide) {
                mapped_tile(c, 0, cell, 1, 0, hide, native);
                for (int scale = 1; scale <= 8; ++scale) {
                    mapped_tile(c, 0, cell, scale, 0, hide, expanded);
                    for (int y = 0; y < 8 * scale; ++y)
                        for (int x = 0; x < 8 * scale; ++x)
                            CHECK(expanded[y * 8 * scale + x] == native[(y / scale) * 8 + x / scale]);
                }
            }
        }
    }
    /* Block ownership masking equals the old per-output-pixel test, including padding. */
    for (int scale = 1; scale <= 8; ++scale) {
        for (uint16_t flip = 0; flip <= 0xc00u; flip += 0x400u) {
            const uint64_t masks[] = {0, UINT64_MAX, UINT64_C(0x0123456789abcdef), 1, UINT64_C(1) << 63};
            for (int pattern = 0; pattern < 5; ++pattern) {
                uint32_t actual[70 * 66], expected[70 * 66];
                for (int i = 0; i < 70 * 66; ++i)
                    actual[i] = expected[i] = 0x12345678u + (uint32_t)i;
                for (int y = 0; y < 8 * scale; ++y)
                    for (int x = 0; x < 8 * scale; ++x) {
                        const int sx = flip & 0x400u ? 7 - x / scale : x / scale;
                        const int sy = flip & 0x800u ? 7 - y / scale : y / scale;
                        if (!(masks[pattern] & (UINT64_C(1) << (sy * 8 + sx))))
                            expected[(y + 1) * 70 + x + 1] = 0;
                    }
                mask_tile(actual + 71, 70, scale, flip, masks[pattern]);
                CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
            }
        }
    }
    /* A graphics-only primary placement must not allocate an empty HD image or
       prevent the same canvas's text from appearing at a supplementary placement. */
    setup(4);
    cc->area_width = cc->param = 4;
    c = canvas_of(cc, "graphics primary");
    draw(0);
    upload(0, c->bytes);
    map_width = map_height = 32;
    for (int i = 0; i < 1024; ++i)
        map_cells[i] = 2;
    map_cells[10] = 2;
    map_cells[11] = 3;
    map_cells[12] = 4;
    map_cells[33] = 1;
    emit();
    CHECK(c->hd_ready && c->hd_image < 0 && c->base_hd);
    CHECK(c->extra && c->extra->hd_image >= 0);
    CHECK(submitted_hd_pixel(15, 11, tint));
    const int graphics_uploads = image_ends, graphics_paints = paint_calls;
    submission_count = 0;
    emit();
    CHECK(image_ends == graphics_uploads && paint_calls == graphics_paints);
    map_cells[33] = 2;
    submission_count = 0;
    emit();
    CHECK(c->hd_ready && c->hd_image < 0 && c->extra == NULL);
    CHECK(!submitted_hd_pixel(15, 11, tint));
    setup(4);
    khdays_ui_text_position(font, .35, .25);
    draw(0);
    khdays_ui_text_position(NULL, 0, 0);
    CHECK(c->op_count == 1 && c->ops[0].x == .35 && c->ops[0].y == .25);
    CHECK(c->ops[0].source_x == 0 && c->ops[0].source_y == 0 && c->ops[0].source_w == 4);
    upload(0, c->bytes);
    render();
    const int fractional_paints = paint_calls, fractional_uploads = image_ends;
    render();
    CHECK(paint_calls == fractional_paints && image_ends == fractional_uploads);
    scroll_text(1);
    CHECK(c->ops[0].y == -.75 && c->ops[0].source_y == -1);
    /* A crawl keeps fractional advances across frames without changing the
       game's integer cursor or leaking a prior stream's residual. */
    int crawl_cursor = 0;
    for (int i = 0; i < 100; ++i) {
        khdays_ui_text_crawl_begin(cc, ram + 700, i, crawl_cursor, 20, 0);
        CHECK(fabs(khdays_ui_text_pen_x(crawl_cursor) - i * 3.85) < 1e-10);
        crawl_cursor += khdays_ui_text_crawl_advance(3.85);
        CHECK(crawl_cursor == khdays_ui_text_round((i + 1) * 3.85));
    }
    khdays_ui_text_crawl_begin(cc, ram + 700, 0, 0, 20, 0);
    CHECK(khdays_ui_text_pen_x(0) == 0);
    CHECK(khdays_ui_text_crawl_advance(6) == 6); // Native fallback advance is exact.
    /* HD / original / HD / original / original: stable indices, native ink
       retained in the graphics plane while only HD artwork is suppressed. */
    for (int bpp = 4; bpp <= 8; bpp += 4) {
        setup(bpp);
        forget(c);
        cc->area_width = cc->param = 6;
        c = canvas_of(cc, "mixed original fallback");
        available_glyphs = (1u << 0) | (1u << 2);
        map_width = 6;
        for (int i = 0; i < 6; ++i)
            map_cells[i] = (uint16_t)(i + 1);
        for (int index = 0; index < 5; ++index) {
            glyph[1] = 264 + (uint32_t)index * 8;
            draw(index * 8);
        }
        CHECK(c->op_count == 2 && c->ops[0].glyph == 0 && c->ops[1].glyph == 2);
        upload(0, c->bytes);
        render();
        CHECK(c->display_count == 2);
        for (int index = 0; index < 5; ++index) {
            const int x = index * 8;
            const int native = index == 1 || index >= 3;
            CHECK(pixel(c, c->display_base, x + 1, 0) == (unsigned)native);
            CHECK((test_images[c->image][x + 1] != 0) == native);
            CHECK(primary_hd_pixel(x + 7) == (native ? 0u : tint));
        }
        /* Equal-byte redraws remain native and full clears remove both planes. */
        glyph[1] = 264 + 8;
        draw(8);
        upload(0, c->bytes);
        render();
        CHECK(c->display_count == 2 && test_images[c->image][9] != 0);
        memset(ram + 1024, 0, c->bytes);
        khdays_ui_text_clear(cc, 0);
        upload(0, c->bytes);
        render();
        CHECK(c->display_count == 0 && c->hd_image < 0 && test_images[c->image][9] == 0);
    }
    /* Actual party-portrait geometry: 15x15 8bpp tiles, two overlapping
       name passes near the bottom, copied into an NCGR and uploaded at tile 21. */
    setup(8);
    forget(c);
    cc->area_width = cc->area_height = cc->param = 15;
    c = canvas_of(cc, "portrait regression");
    ram[513] = 10;
    memset(ram + 1024, 0xe1, c->bytes);
    observe_source(c);
    for (int pass = 0; pass < 2; ++pass) {
        for (int letter = 0; letter < 5; ++letter) {
            const int x = 77 + letter * 4 - pass, y = 108 - pass;
            khdays_ui_text_glyph_before(cc, font, x, y, 0xf1 + pass, glyph);
            for (int py = y; py < y + 8; ++py)
                for (int px = x; px < x + 10; ++px)
                    put_pixel(c, ram + 1024, px, py, 0xf1 + pass);
            khdays_ui_text_glyph(cc, font, x, y, 0xf1 + pass, glyph);
        }
    }
    CHECK(c->op_count == 10);
    memcpy(ram + 16384, ram + 1024, c->bytes);
    khdays_ui_text_memory_copy(ram + 1024, ram + 16384, c->bytes, 16384);
    CHECK(canvas_count == 2 && canvases[1].op_count == 10);
    memcpy(vram + 21 * 64, ram + 16384, c->bytes);
    chars_loaded(0, 3, ram + 16384, 21 * 64, c->bytes);
    /* The real upload names BG3 (4bpp), but BG2's 8bpp tilemap
       displays the shared character VRAM. The transfer is not the display. */
    test_map_bg = 2;
    test_bg_colors256[3] = 0;
    map_width = map_height = 32;
    for (int row = 0; row < 15; ++row)
        for (int col = 0; col < 15; ++col)
            map_cells[(row + 8) * 32 + col + 17] = (uint16_t)(0xc000 | (21 + row * 15 + col));
    submission_count = 0;
    emit();
    CHECK(canvases[1].display_count == 10);
    CHECK(canvases[1].bg == 2);
    CHECK(submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    for (int i = 0; i < submission_count; ++i)
        CHECK((submissions[i].slot & KES_NUMBER_MASK) == 2);
    const int portrait_images = image_requests, portrait_uploads = image_ends, portrait_paints = paint_calls;
    for (int frame = 0; frame < 30; ++frame) {
        submission_count = 0;
        emit();
        CHECK(submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    }
    CHECK(image_requests == portrait_images && image_ends == portrait_uploads && paint_calls == portrait_paints);
    /* An eligible upload layer with no referencing cells must also be skipped. */
    test_bg_colors256[3] = 1;
    chars_loaded(0, 3, ram + 16384, 21 * 64, c->bytes);
    submission_count = 0;
    emit();
    CHECK(canvases[1].bg == 2 && submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    /* Removing the map removes the overlay; reassigning it follows the actual
       display layer without another upload or another canvas/image allocation. */
    test_map_bg = 0;
    test_bg_colors256[0] = 0;
    submission_count = 0;
    emit();
    CHECK(submission_count == 0);
    test_map_bg = 1;
    submission_count = 0;
    emit();
    CHECK(canvases[1].bg == 1 && submitted_hd_pixel(17 * 8 + 99, 8 * 8 + 109, tint ^ 0xf000u));
    CHECK(image_requests == portrait_images);
    /* The portrait name is drawn into scratch RAM, copied into an NCGR,
       and later uploaded from that NCGR rather than the original canvas. */
    setup(8);
    draw(0);
    const uint32_t staged_size = c->bytes;
    font_origin = "UI/btl/face.p2@0";
    font_origin_info = 4096;
    font_origin_offset = 96;
    const unsigned int portrait_owner = khdays_ui_text_copy_owner(ram + 1024, ram + 4096, staged_size);
    CHECK(portrait_owner == 4000);
    font_origin = NULL;
    memcpy(ram + 4096, ram + 1024, staged_size);
    khdays_ui_text_memory_copy(ram + 1024, ram + 4096, staged_size, portrait_owner);
    CHECK(canvas_count == 2 && canvases[1].copied && canvases[1].op_count == 1);
    CHECK(pixel(&canvases[1], canvases[1].base, 0, 0) == 0);
    memcpy(ram + 8192, ram + 4096, staged_size);
    khdays_ui_text_memory_copy(ram + 4096, ram + 8192, staged_size, 8192);
    CHECK(canvas_count == 3 && canvases[2].op_count == 1);
    khdays_ui_text_freed(NULL, ram + 4000); // Frees an allocation containing interior character data.
    CHECK(canvas_count == 2 && canvases[1].buffer == 8192);
    memset(ram + 1024, 0, staged_size);
    khdays_ui_text_clear(cc, 0); // Reusing scratch must not change the copied name.
    memcpy(vram + 64, ram + 8192, staged_size);
    chars_loaded(0, 0, ram + 8192, 64, staged_size);
    emit();
    CHECK(canvases[1].display_count == 1 && submitted_hd_pixel(7, 3, tint));
    const int copied_uploads = image_ends, copied_paints = paint_calls;
    for (int frame = 0; frame < 10; ++frame) {
        submission_count = 0;
        emit();
    }
    CHECK(image_ends == copied_uploads && paint_calls == copied_paints);
    /* A second full copy of empty scratch removes the name, even with identical bytes. */
    memcpy(ram + 8192, ram + 1024, staged_size);
    khdays_ui_text_memory_copy(ram + 1024, ram + 8192, staged_size, 8192);
    CHECK(canvases[1].op_count == 0 && canvases[1].display_count == 1);
    memcpy(vram + 64, ram + 8192, staged_size);
    chars_loaded(0, 0, ram + 8192, 64, staged_size);
    submission_count = 0;
    emit();
    CHECK(canvases[1].display_count == 0 && !submitted_hd_pixel(7, 3, tint));
    /* VRAM copies are published by the BG loader, never registered as RAM staging. */
    draw(0);
    const int ram_canvases = canvas_count;
    khdays_ui_text_memory_copy(ram + 1024, vram + 64, staged_size, 0);
    CHECK(canvas_count == ram_canvases);
    /* Repeated copies reuse the staging record and have a fixed budget. */
    for (int pass = 0; pass < 50; ++pass) {
        memcpy(ram + 8192, ram + 1024, staged_size);
        khdays_ui_text_memory_copy(ram + 1024, ram + 8192, staged_size, 8192);
    }
    CHECK(canvas_count == ram_canvases && canvases[1].op_count == 1);
    khdays_ui_text_freed(NULL, ram + 8192);
    CHECK(canvases[1].detached); // Already published VRAM survives until overwritten.
    for (int i = 0; i < 24; ++i) {
        const int dst = 16384 + i * (int)staged_size;
        memcpy(ram + dst, ram + 1024, staged_size);
        khdays_ui_text_memory_copy(ram + 1024, ram + dst, staged_size, (unsigned int)dst);
    }
    CHECK(canvas_count == 17); // One real canvas, at most sixteen copied records.
    setup(4);
    mapping_ready = 0;
    *(uint32_t *)(ram + 252) = 32;  // CGLP: two original glyphs.
    *(uint32_t *)(ram + 144) = 768; // FINF.pMap
    uint16_t *original_map = (uint16_t *)(ram + 768);
    original_map[0] = 65;
    original_map[1] = 66;
    original_map[6] = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapping_count == 2 && mapped_codes[0] == 65 && mapped_codes[1] == 66);
    original_map[1] = 67;
    original_map[2] = 1;
    original_map[6] = 1;
    original_map[7] = 0xffff;
    original_map[8] = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapped_codes[0] == 67 && mapped_codes[1] == 65);
    original_map[2] = 2;
    original_map[6] = 2;
    original_map[7] = 0xff21;
    original_map[8] = 1;
    original_map[9] = 0x2019;
    original_map[10] = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapped_codes[0] == 0x2019 && mapped_codes[1] == 0xff21);
    *(uint32_t *)(ram + 776) = 768;
    mapping_count = 0;
    bind_charmap("text/synthetic.nftr", font);
    CHECK(mapping_count == 0); // A malformed cycle must not register a partial map.
    setup(8);
    cc->area_height = 4;
    c = canvas_of(cc, "baseline rows");
    map_height = 4;
    for (int k = 0; k < 8; ++k)
        map_cells[k] = (uint16_t)(k + 1);
    baseline_shift = -3;
    draw_xy(0, 1);  // Cropped at the actual canvas top, not the old Days cell top.
    draw_xy(8, 17); // Right-aligned second line retains the same baseline translation.
    CHECK(c->op_count == 2 && c->ops[0].y == -2 && c->ops[1].y == 14);
    CHECK(c->ops[0].clip_t == 0 && c->ops[1].clip_t == 14);
    draw_xy(0, 1);
    CHECK(c->op_count == 2); // Reissuing the source position replaces the shifted operation.
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(4) == tint);
    CHECK(primary_hd_pixel(6 * 16 + 4) == 0);
    CHECK(primary_hd_pixel(14 * 16 + 12) == tint);
    CHECK(primary_hd_pixel(22 * 16 + 12) == 0);
    uint32_t baseline_tile[64];
    mapped_tile(c, 3, 0, 1, 1, 1, baseline_tile);
    CHECK(baseline_tile[6 * 8 + 4] == tint && baseline_tile[5 * 8 + 4] == 0);
    khdays_ui_text_clear_area(cc, 0, 8, 14, 8, 2);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(14 * 16 + 12) == 0);
    CHECK(primary_hd_pixel(16 * 16 + 12) == tint);
    scroll_text(8);
    CHECK(c->ops[0].source_y == 9 && c->ops[0].y == 6);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(8 * 16 + 12) == tint);
    baseline_shift = 3;
    draw_xy(0, 20);
    CHECK(c->ops[c->op_count - 1].y == 23);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(23 * 16 + 4) == tint);
    /* Initial mixed-canvas population: artwork overlaps neighbouring storage,
       but graphics uploaded there do not overwrite the original glyph draw. */
    setup(8);
    large_geometry = 1;
    cc->area_width = cc->param = 20;
    cc->area_height = 32;
    c = canvas_of(cc, "initial panel population");
    map_width = map_height = 32;
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 20; ++col)
            map_cells[row * 32 + col] = (uint16_t)(256 + row * 20 + col);
    for (int row = 0; row < 8; ++row) {
        draw_xy(16, row * 16 + 3); // Name and right-aligned quantity in the same canvas.
        draw_xy(112, row * 16 + 3);
        put_pixel(c, ram + 1024, 24, row * 16 + 5, 7); // Late source graphics inside PNG overhang.
        put_pixel(c, ram + 1024, 120, row * 16 + 5, 7);
    }
    observe_source(c);
    CHECK(c->op_count == 16);
    memcpy(vram + 0x4000, ram + 1024, c->bytes);
    chars_loaded(1, 2, ram + 1024, 0x4000, c->bytes);
    CHECK(c->display_count == 16);
    for (int row = 0; row < 8; ++row) {
        put_pixel(c, vram + 0x4000, 25, row * 16 + 5, 6); // Late VRAM graphics, same condition.
        put_pixel(c, vram + 0x4000, 121, row * 16 + 5, 6);
    }
    emit();
    CHECK(c->display_count == 16);
    for (int row = 0; row < 8; ++row) {
        CHECK(submitted_hd_pixel(30, row * 16 + 8, tint));
        CHECK(submitted_hd_pixel(126, row * 16 + 8, tint));
    }
    const int initial_paints = paint_calls, initial_uploads = image_ends;
    for (int frame = 0; frame < 10; ++frame) {
        submission_count = 0;
        emit();
    }
    CHECK(paint_calls == initial_paints && image_ends == initial_uploads);
    /* Scrolling's redraw sees the already-populated graphics bytes. This used
       to make missing first-frame text suddenly appear without changing PNGs. */
    for (int row = 0; row < 8; ++row) {
        draw_xy(16, row * 16 + 3);
        draw_xy(112, row * 16 + 3);
    }
    memcpy(vram + 0x4000, ram + 1024, c->bytes);
    chars_loaded(1, 2, ram + 1024, 0x4000, c->bytes);
    submission_count = 0;
    emit();
    CHECK(c->op_count == 16 && c->display_count == 16);
    for (int row = 0; row < 8; ++row) {
        CHECK(submitted_hd_pixel(30, row * 16 + 8, tint));
        CHECK(submitted_hd_pixel(126, row * 16 + 8, tint));
    }
    /* Overwriting the native footprint still retires the complete glyph. */
    put_pixel(c, vram + 0x4000, 16, 3, 0);
    submission_count = 0;
    emit();
    CHECK(c->display_count == 15);
    CHECK(!submitted_hd_pixel(30, 8, tint));
    setup(4);
    large_geometry = 1;
    cc->area_width = cc->param = 4;
    cc->area_height = 4;
    c = canvas_of(cc, "large art");
    map_width = map_height = 4;
    khdays_bg_text_layer(0, 0, &layer);
    for (int k = 0; k < 16; ++k)
        map_cells[k] = (uint16_t)(k + 1);
    draw(0);
    CHECK(c->ops[0].w == 18 && c->ops[0].h == 24 && c->ops[0].clip_b == 24);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(23 * 32 + 17) == tint);
    uint32_t large_tile[64];
    mapped_tile(c, 10, map_cells[10], 1, 1, 1, large_tile);
    CHECK(large_tile[7 * 8 + 1] == tint);
    khdays_ui_text_clear_area(cc, 0, 16, 16, 2, 8);
    CHECK(displayed_at(17, 23)); // Source clears stay unpublished until upload.
    upload(10 * 32, 32);
    render();
    CHECK(primary_hd_pixel(23 * 32 + 17) == 0);
    CHECK(primary_hd_pixel(23 * 32 + 15) == tint);
    scroll_text(5);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(18 * 32 + 15) == tint);
    CHECK(primary_hd_pixel(23 * 32 + 15) == 0);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    setup(4);
    metric_override = 1;
    CHECK(khdays_ui_text_metric(font, 0, KHDF_LEFT, 0) == -2);
    CHECK(khdays_ui_text_metric(font, 0, KHDF_WIDTH, 8) == 13);
    CHECK(khdays_ui_text_metric(font, 0, KHDF_ADVANCE, 8) == 11);
    CHECK(khdays_ui_text_metric(font, -1, KHDF_LINE_FEED, 8) == 17);
    CHECK(khdays_ui_text_kerning(font, 'A', 'V') == -2);
    text_mode = KHDAYS_UI_OFF;
    CHECK(khdays_ui_text_metric(font, 0, KHDF_ADVANCE, 8) == 8);
    CHECK(khdays_ui_text_kerning(font, 'A', 'V') == 0);
    setup(4);
    glyph_width_override = 6;
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1 && c->display_ops[0].paint_w == 6);
    CHECK(primary_hd_pixel(4) == tint && primary_hd_pixel(5) == tint);
    CHECK(primary_hd_pixel(6) == 0 && primary_hd_pixel(7) == 0);
    uint32_t mapped[64];
    mapped_tile(c, 0, map_cells[0], 1, 1, 1, mapped);
    CHECK(mapped[4] == tint && mapped[5] == tint && mapped[6] == 0 && mapped[7] == 0);
    glyph_width_override = 0;
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1 && c->display_ops[0].paint_w == 0);
    CHECK(c->hd_ready && c->hd_image < 0 && c->base_hd);
    for (int px = 0; px < 16 * 8; ++px)
        CHECK(test_images[c->image][px] == 0);
    CHECK(pixel(c, c->seen, 0, 0) == 1 && pixel(c, c->display_base, 0, 0) == 0);
    mapped_tile(c, 0, map_cells[0], 1, 1, 1, mapped);
    for (int px = 0; px < 64; ++px)
        CHECK(mapped[px] == 0);
    mapped_tile(c, 0, map_cells[0], 1, 0, 1, mapped);
    for (int px = 0; px < 64; ++px)
        CHECK(mapped[px] == 0); /* Zero PNG width must not expose original DS ink. */
    setup(4);
    bind_font(font, "/text/synthetic.NFTR");
    CHECK(font_path(KHDAYS_REF(font)) && strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
    bind_font(font, "text/synthetic.nftr.png");
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/synthetic.nftr") == 0);
    bind_font(font, "/text/synthetic.nftr");
    const char *bound = font_path(KHDAYS_REF(font));
    CHECK(bound && strcmp(bound, "text/synthetic.nftr") == 0);
    khdays_ui_text_font_init(font, NULL);
    CHECK(font_path(KHDAYS_REF(font)) != NULL);
    uint32_t *copied_font = (uint32_t *)(ram + 700);
    copied_font[0] = font[0];
    CHECK(font_path(KHDAYS_REF(copied_font)) != NULL);
    const uint32_t saved_info = font[0];
    font[0] = 900; /* Reusing an object for another NFTR cannot retain the old path. */
    CHECK(font_path(KHDAYS_REF(font)) == NULL);
    font[0] = saved_info;
    binding_count = 0;
    font_origin = "/text/synthetic.nftr";
    font_origin_info = saved_info;
    khdays_ui_text_font_init(font, ram + saved_info);
    CHECK(font_path(KHDAYS_REF(font)) != NULL);
    CHECK(font_path(KHDAYS_REF(copied_font)) != NULL);
    font_origin = "text/second.nftr";
    khdays_ui_text_font_init(font, ram + saved_info);
    CHECK(strcmp(font_path(KHDAYS_REF(font)), "text/second.nftr") == 0);
    khdays_ui_text_freed(NULL, ram + saved_info);
    font_origin = NULL;
    CHECK(font_path(KHDAYS_REF(copied_font)) == NULL);
    font_origin = "/text/missing.nftr";
    const int probes_before = font_probes;
    for (int i = 0; i < 100; ++i)
        CHECK(font_path(KHDAYS_REF(copied_font)) == NULL);
    CHECK(font_probes == probes_before + 1); /* Missing atlases are cached by identity too. */
    setup(4);
    ram[513] = 0; /* PNG artwork is independent of a zero original glyphWidth. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1);
    CHECK(c->hd_image >= 0 && primary_hd_pixel(7) == tint);
    setup(4);
    draw(0);
    glyph[1] += 8; /* Adjacent DS fallback overlaps only the end of the HD cell. */
    draw(6);
    upload(0, c->bytes);
    render();
    CHECK(displayed_at(4, 0));
    CHECK(c->hd_image >= 0 && primary_hd_pixel(4) == tint);
    glyph[1] -= 8;
    setup(4);
    cc->area_height = 3;
    c = canvas_of(cc, "scroll test");
    map_height = 3;
    for (int i = 0; i < 6; ++i)
        map_cells[i] = (uint16_t)(i + 1);
    put_pixel(c, ram + 1024, 15, 18, 3);
    draw_xy(0, 8);
    draw_xy(8, 16);
    upload(0, c->bytes);
    render();
    scroll_text(5);
    CHECK(c->op_count == 2 && c->ops[0].y == 3 && c->ops[1].y == 11);
    CHECK(pixel(c, c->base, 15, 13) == 3);
    CHECK(displayed_at(7, 8)); /* publication still follows the transfer */
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(3 * 16 + 7) == tint);
    CHECK(primary_hd_pixel(11 * 16 + 15) == tint);
    CHECK(test_images[c->image][13 * 16 + 15] == (KET_OPAQUE | 3));
    scroll_text(8);
    upload(0, c->bytes);
    render();
    CHECK(c->op_count == 2); /* the first glyph survives as a clipped top fragment */
    CHECK(primary_hd_pixel(3 * 16 + 15) == tint);
    scroll_text(24);
    CHECK(c->op_count == 0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0 && c->hd_image < 0);
    setup(4);
    /* Mixed graphics survive even where the original DS ink overwrites them. */
    put_pixel(c, ram + 1024, 0, 0, 3);
    put_pixel(c, ram + 1024, 7, 7, 2);
    draw(0);
    CHECK(c->op_count == 1);
    CHECK(c->display_count == 0);
    CHECK(pixel(c, c->base, 0, 0) == 3);
    CHECK(pixel(c, ram + 1024, 0, 0) == 1);
    upload(0, c->bytes);
    render();
    CHECK(test_images[c->image][0] == (KET_OPAQUE | 3));
    CHECK(test_images[c->image][7 * 16 + 7] == (KET_OPAQUE | 2));
    CHECK(primary_hd_pixel(0) == 0);
    CHECK(primary_hd_pixel(7) == tint);
    CHECK(test_images[c->image][1] == 0); /* no original silhouette leaks */
    int paints = paint_calls, ends = image_ends;
    render();
    CHECK(paint_calls == paints);
    CHECK(image_ends == ends);
    /* Clears affect the source now, and the displayed text only on upload. */
    for (int y = 0; y < 8; ++y)
        for (int x = 6; x < 8; ++x)
            put_pixel(c, ram + 1024, x, y, 0);
    khdays_ui_text_clear_area(cc, 0, 6, 0, 2, 8);
    CHECK(displayed_at(7, 0));
    upload(0, 32);
    render();
    CHECK(!displayed_at(7, 0));
    CHECK(displayed_at(4, 0));
    /* Each mapped tile controls palette, clipping and flips independently. */
    map_cells[0] |= 0x1400;
    render();
    CHECK(primary_hd_pixel(3) == (tint ^ 0x1000u));
    map_cells[0] = 0;
    render();
    for (int x = 0; x < 8; ++x)
        CHECK(primary_hd_pixel(x) == 0);
    map_cells[0] = 1;
    render();
    CHECK(primary_hd_pixel(4) == tint);
    /* Direct blanking retires the entire replacement, including PNG-only ink. */
    memset(ram + 1024, 0, c->bytes);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    CHECK(c->hd_image < 0);
    /* Full clear even when DS bytes are unchanged removes transparent-source glyphs. */
    draw(0);
    upload(0, c->bytes);
    render();
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(!c->display_count);
    /* Thousands of menu rebuilds retain bounded active commands. */
    for (int i = 0; i < 1500; ++i) {
        memset(ram + 1024, 0, c->bytes);
        khdays_ui_text_clear(cc, 0);
        draw(0);
        draw(8);
        upload(0, c->bytes);
        CHECK(c->op_count == 2);
        CHECK(c->display_count == 2);
        CHECK(c->op_room <= 64);
        render();
    }
    render();
    int unchanged_uploads = image_ends;
    upload(0, c->bytes);
    render();
    CHECK(image_ends == unchanged_uploads);
    /* An unhooked VRAM erase must remove PNG ink outside the original silhouette. */
    memset(vram + 32, 0, 32);
    observe_vram(c);
    render();
    CHECK(!displayed_at(7, 0));
    CHECK(displayed_at(15, 0));
    /* Partial uploads preserve an old glyph on the tile not uploaded. */
    draw(0);
    upload(0, 32);
    render();
    CHECK(displayed_at(7, 0));
    CHECK(displayed_at(15, 0));
    memcpy(vram + 32, ram + 4096, 32);
    chars_loaded(0, 0, ram + 4096, 32, 32);
    render();
    CHECK(!displayed_at(7, 0));
    CHECK(displayed_at(15, 0));
    khdays_ui_text_freed(NULL, ram + 1024);
    CHECK(c->detached);
    CHECK(!c->source && !c->base && !c->ops);
    render();
    CHECK(displayed_at(15, 0));
    memset(vram + 32, 0, 64);
    chars_loaded(0, 0, ram + 4096, 32, 64);
    emit();
    CHECK(canvas_count == 0);
    setup(8);
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(7) == tint);
    CHECK(test_images[c->image][0] == 0);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    setup(4);
    /* A glyph may have an entirely empty original bitmap. PNG still renders. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    CHECK(primary_hd_pixel(7) == tint);
    khdays_ui_text_clear(cc, 0);
    upload(0, c->bytes);
    render();
    CHECK(!c->display_count);
    /* SDK fills carry a write event even for a PNG over an empty DS bitmap. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 1);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_memory_clear(ram + 1024, c->bytes);
    CHECK(c->op_count == 0 && c->display_count == 1);
    upload(0, c->bytes);
    render();
    CHECK(c->display_count == 0);
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    upload(0, c->bytes);
    render();
    memset(vram + 32, 0, c->bytes);
    khdays_ui_text_memory_clear(vram + 32, c->bytes);
    render();
    CHECK(c->display_count == 0 && c->hd_image < 0);
    /* Missing glyph fallback copies equal-valued DS pixels as well as deltas. */
    draw(0);
    upload(0, c->bytes);
    render();
    glyph[1] += 8; // synthetic glyph index 1 has no PNG override
    draw(0);
    upload(0, c->bytes);
    render();
    CHECK(!c->display_count);
    CHECK(test_images[c->image][1] == (KET_OPAQUE | 1));
    glyph[1] -= 8;
    /* Two canvases sharing a partial tile must have disjoint transfer ownership. */
    setup(4);
    draw(0);
    upload(0, c->bytes);
    render();
    Canvas *old = c;
    DsCanvas *other = (DsCanvas *)(ram + 2048);
    *other = *cc;
    other->char_base = 8192;
    memset(ram + 8192, 0x55, 64);
    Canvas *newer = canvas_of(other, "overlap test");
    memcpy(vram + 33, ram + 8193, 1);
    chars_loaded(0, 0, ram + 8193, 33, 1);
    CHECK((old->owned[0] & 12u) == 0);
    CHECK(newer->owned[0] == 12u);
    c = old;
    render();
    CHECK(test_images[c->image][2] == 0);
    CHECK(primary_hd_pixel(7) == tint);
    c = newer;
    render();
    CHECK(test_images[c->image][2] == (KET_OPAQUE | 5));
    CHECK(test_images[c->image][7] == 0);
    /* A changed palette updates the PNG tint without source glyph inspection. */
    c = old;
    tint = 0xffabcdefu;
    render();
    CHECK(primary_hd_pixel(7) == tint);
    int cached_paints = paint_calls;
    render();
    CHECK(paint_calls == cached_paints);
    setup(4);
    /* First upload of a few bytes preserves the rest of the native tile. */
    memset(vram + 32, 0x22, 64);
    ram[1024] = 0x11;
    upload(0, 1);
    render();
    CHECK(test_images[c->image][0] == (KET_OPAQUE | 1));
    CHECK(test_images[c->image][7] == (KET_OPAQUE | 2));
    setup(4);
    cc->area_width = cc->param = 16;
    cc->area_height = 8;
    c = canvas_of(cc, "text-heavy stress");
    map_width = 16;
    map_height = 8;
    for (int i = 0; i < map_width * map_height; ++i)
        map_cells[i] = (uint16_t)(i + 1);
    for (int cycle = 0; cycle < 100; ++cycle) {
        memset(ram + 1024, 0, c->bytes);
        khdays_ui_text_clear(cc, 0);
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 16; ++col)
                draw_xy(col * 8, row * 8);
        upload(0, c->bytes);
        render();
        CHECK(c->op_count == 128 && c->display_count == 128);
        CHECK(c->op_room == 128 && c->display_room == 128);
        const int stable = image_ends;
        upload(0, c->bytes);
        render();
        CHECK(image_ends == stable);
    }
    command_menu_selections(1);
    command_menu_selections(2);
    /* Blank tile 0 repeated over a screen must not allocate one canvas per cell. */
    setup(4);
    cc->area_width = cc->param = 32;
    cc->area_height = 24;
    c = canvas_of(cc, "repeated background tile");
    memcpy(vram, ram + 1024, c->bytes);
    chars_loaded(0, 1, ram + 1024, 0, c->bytes);
    map_width = map_height = 32;
    memset(map_cells, 0, sizeof(map_cells));
    test_scale = 4;
    image_requests = 0;
    peak_bytes = live_bytes;
    submission_count = 0;
    emit();
    CHECK(image_requests <= 4);
    CHECK(peak_bytes <= 8u * 1024u * 1024u);
    CHECK(submission_count <= 10);
    const int blank_requests = image_requests, blank_uploads = image_ends;
    for (int frame = 0; frame < 10; ++frame) {
        submission_count = 0;
        emit();
    }
    CHECK(image_requests == blank_requests && image_ends == blank_uploads);
    /* Even a PNG over blank tile 0 repeated 768 times needs only four images. */
    khdays_ui_text_glyph_before(cc, font, 0, 0, 1, glyph);
    khdays_ui_text_glyph(cc, font, 0, 0, 1, glyph);
    chars_loaded(0, 1, ram + 1024, 0, c->bytes);
    image_requests = 0;
    submission_count = 0;
    const int repeated_paints = paint_calls;
    emit();
    CHECK(paint_calls - repeated_paints <= 2); /* One primary tile and one supplementary tile, not 768 paints. */
    CHECK(image_requests <= 4);
    CHECK(peak_bytes <= 16u * 1024u * 1024u);
    CHECK(submission_count <= 10);
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 32; ++col)
            CHECK(submitted_hd_pixel(col * 8 + 7, row * 8 + 3, tint));
    const int stable_requests = image_requests, stable_uploads = image_ends, stable_paints = paint_calls;
    for (int frame = 0; frame < 100; ++frame) {
        submission_count = 0;
        emit();
    }
    CHECK(image_requests == stable_requests && image_ends == stable_uploads && paint_calls == stable_paints);
    map_cells[33] = 0x1400; /* supplementary copy: palette 1 and horizontal flip */
    submission_count = 0;
    emit();
    CHECK(submitted_hd_pixel(9, 11, tint ^ 0x1000u));
    CHECK(!submitted_hd_pixel(15, 11, tint ^ 0x1000u));
    map_cells[33] = 0;
    submission_count = 0;
    emit();
    CHECK(submitted_hd_pixel(15, 11, tint));
    test_scale = 8;
    image_requests = 0;
    peak_bytes = 0;
    submission_count = 0;
    const int scale_paints = paint_calls;
    emit();
    CHECK(paint_calls - scale_paints <= 2);
    CHECK(image_requests == 4);
    CHECK(c->extra && image_bytes[c->extra->image] == EXTRA_CELLS * 64 * sizeof(uint32_t));
    CHECK(peak_bytes <= 64u * 1024u * 1024u);
    for (int frame = 0; frame < 32; ++frame) {
        test_hofs = frame;
        test_vofs = frame / 2;
        submission_count = 0;
        const int scroll_paints = paint_calls;
        emit();
        CHECK(paint_calls - scroll_paints <= 2);
        CHECK(image_requests == 4);
        CHECK(submission_count <= 10);
        for (int row = 1; row < 23; ++row)
            for (int col = 1; col < 31; ++col)
                CHECK(submitted_hd_pixel(col * 8 + 7 - (test_hofs & 7), row * 8 + 3 - (test_vofs & 7), tint));
    }
    /* Reuse keys include palette and flips, and expire before the next update. */
    test_hofs = test_vofs = 0;
    for (int i = 0; i < 1024; ++i)
        map_cells[i] = (uint16_t)((i % 3) * 0x1400u);
    submission_count = 0;
    const int variants_paints = paint_calls;
    emit();
    CHECK(paint_calls - variants_paints <= 4);
    CHECK(submitted_hd_pixel(9, 3, tint ^ 0x1000u));
    CHECK(submitted_hd_pixel(23, 4, tint ^ 0x2000u));
    const uint32_t old_tint = tint;
    tint ^= 0x00001100u;
    memset(c->dirty, 3, (size_t)c->area_w * c->area_h);
    submission_count = 0;
    const int changed_paints = paint_calls;
    emit();
    CHECK(paint_calls - changed_paints <= 4);
    CHECK(submitted_hd_pixel(9, 3, tint ^ 0x1000u));
    CHECK(!submitted_hd_pixel(9, 3, old_tint ^ 0x1000u));
    tint = old_tint;
    printf("HD repeated-tile stress: four images, %.2f MiB peak at scale 8\n", peak_bytes / (1024.0 * 1024.0));
    setup(8);
    draw(0);
    upload(0, c->bytes);
    map_width = map_height = 32;
    for (int i = 0; i < 1024; ++i)
        map_cells[i] = 1;
    emit();
    CHECK(submitted_hd_pixel(15, 11, tint));
    CHECK(c->extra && c->extra->hd_image >= 0);
    while (canvas_count)
        forget(&canvases[0]);
    free(canvases);
    printf("HD text operations: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
