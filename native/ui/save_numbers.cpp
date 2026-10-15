// The file numbers on the rows of the game's own save lists in pages (save_numbers.h). The DS's rows
// number its three files "01" to "03", each row with two 8x8 sprites of the list's sheet's white digits
// (UI/cm/cmo_&.p2 entry 3: its 8-bit characters 44, 42, 46 and 48 are 0, 1, 2 and 3, every other
// character the sprites' 128-byte steps leave unused; the title's list draws them as OBJ tiles 22, 21,
// 23 and 24). While the lists page through the port's files (runtime/menus/savelist.c) a row's file is
// the page's first + the row, so the port draws that number instead: the sheet has its digits 0-9 in
// the same shapes in cyan (characters 22 to 40, every other one), drawn here in the white digits' colours -- the white 0 against the cyan 0 gives, row by row, the colour each cyan one
// becomes, checked against the white 1, 2 and 3. The game's sprites are found by their pixels (any
// list, wherever its sprites lie), their own digit telling their row. Over file 99's number, the
// autosave's, a short tag in the game's 8-dot font (text/font_eu_08.nftr) in the same colours: the
// plate's dots above the number are free up to its day's label (the maintainer's choice, 2026-10-15).
// docs/RUNTIME_MAP.md 15.4.
#include "save_numbers.h"

extern "C" {
#include "elements.h"
#include "parts.h"
#include "../host/screens/ui_text.h"
#include "../runtime/core/io.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/vram.h"
#include "../runtime/core/window.h"
#include "../runtime/menus/saveslots.h"
extern int GetLanguage(void);
}

#include "khdays/assets/font.h"
#include "khdays/vfs/filesystem.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kCyan = 22;               // the cyan 0's character, the others every other one after it
constexpr int kWhite[4] = {44, 42, 46, 48}; // the white 0, 1, 2 and 3
constexpr int kUnitsFromTens = 8;       // the units' sprite at most this right of the tens' (title list: x 34 and 40)

using Glyph = std::array<std::uint8_t, 64>;

struct Digits {
    int tried = 0;   // the language last read
    bool ok = false;
    std::array<Glyph, 4> white{};  // as the game draws them, to find its sprites
    std::array<Glyph, 10> drawn{};  // 0-9 in the white digits' colours
    std::array<int, 10> image{};    // each one's element image, at `scale`
    int scale = 0;
};
Digits digits;
KhdaysRomReader* reader;  // its own: this runs at VBlank, maybe inside the game's own reading

std::uint32_t le16(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0] | p[1] << 8);
}

std::uint32_t le32(const std::uint8_t* p) {
    return le16(p) | le16(p + 2) << 16;
}

// The 8-bit characters of an NCGR at `d` (its CHAR block after the header, the pixels 8 + the offset at
// +0x1c into it), their count in *tiles; NULL when it is not one.
const std::uint8_t* characters(const std::uint8_t* d, std::uint32_t size, int* tiles) {
    if (size < 0x10 || std::memcmp(d, "RGCN", 4) != 0) {
        return nullptr;
    }
    const std::uint32_t block = le16(d + 0xc);
    if (block + 0x20 > size || std::memcmp(d + block, "RAHC", 4) != 0 || le32(d + block + 0xc) != 4) {
        return nullptr;  // 4: 8 bits a pixel
    }
    const std::uint32_t bytes = le32(d + block + 0x18), at = block + 8 + le32(d + block + 0x1c);
    if (at > size || bytes > size - at) {
        return nullptr;
    }
    *tiles = static_cast<int>(bytes / 64u);
    return d + at;
}

bool read_digits(int language) {
    static const char* const codes[] = {"", "en", "fr", "de", "it", "es"};  // the firmware's numbering
    char path[64];
    std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
    if (language < 1 || language > 5) {
        return false;
    }
    std::snprintf(path, sizeof(path), "UI/cm/cmo_%s.p2", codes[language]);
    if (reader == nullptr) {
        reader = khdays_rom_reader_new();
    }
    if (!khdays_rom_file_data(reader, path, 3, &data, &size)) {
        std::free(data);
        return false;
    }
    bool ok = false;
    // the pack's character sheets (D2KP: each resource found by its magic)
    for (std::uint32_t o = 0; !ok && o + 0x10 <= size; o += 4) {
        int tile_count = 0;
        const std::uint8_t* sheet = characters(data + o, size - o, &tile_count);
        if (sheet == nullptr || tile_count <= kWhite[3]) {
            continue;
        }
        auto tile = [&](int t) {
            Glyph g{};
            std::memcpy(g.data(), sheet + static_cast<std::size_t>(t) * 64, 64);
            return g;
        };
        {
            // the colour each cyan index takes on each row, from the 0s
            const Glyph cyan0 = tile(kCyan), white0 = tile(kWhite[0]);
            int map[8][256];
            std::memset(map, -1, sizeof(map));
            bool same = true;
            for (int p = 0; p < 64 && same; ++p) {
                if ((cyan0[p] == 0) != (white0[p] == 0)) {
                    same = false;
                } else if (cyan0[p] != 0) {
                    map[p / 8][cyan0[p]] = white0[p];
                }
            }
            if (!same) {
                continue;
            }
            for (int d = 0; d < 10; ++d) {
                const Glyph c = tile(kCyan + 2 * d);
                for (int p = 0; p < 64; ++p) {
                    const int m = c[p] == 0 ? 0 : map[p / 8][c[p]];
                    digits.drawn[d][p] = static_cast<std::uint8_t>(m < 0 ? 0 : m);
                }
            }
            for (int d = 0; d < 4; ++d) {
                digits.white[d] = tile(kWhite[d]);
                same = same && digits.white[d] == digits.drawn[d];  // the white 1, 2 and 3 as made
            }
            ok = same;
        }
    }
    std::free(data);
    if (!ok) {
        std::fprintf(stderr, "ui: the save list's digits are not in %s entry 3 as expected; the game's numbers stay\n",
                     path);
    }
    return ok;
}

// The tag over the autosave's number: its image at `scale`, in the white digits' fill and outline.
constexpr int kAutosaveFile = 99;
constexpr int kTagGap = 2;  // dots between the tag and the number below it
struct Tag {
    int image = -1, scale = 0, language = -1, w = 0, h = 0;
};
Tag tag;
std::optional<khdays::assets::Font> font;
bool font_tried;

const khdays::assets::Font* the_font() {
    if (!font_tried) {
        font_tried = true;
        const auto path = khdays::vfs::resolve("text/font_eu_08.nftr");
        try {
            if (path) {
                font = khdays::assets::decode_nftr(*path);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ui: the save list's tag font could not be read (%s)\n", e.what());
        }
    }
    return font ? &*font : nullptr;
}

std::u16string utf16(const char* s) {
    std::u16string out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p != 0;) {
        unsigned c = *p++;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        c &= more == 3 ? 0x07u : more == 2 ? 0x0fu : more == 1 ? 0x1fu : 0x7fu;
        for (; more > 0 && (*p & 0xc0) == 0x80; --more) {
            c = (c << 6) | (*p++ & 0x3fu);
        }
        out.push_back(static_cast<char16_t>(c));
    }
    return out;
}

int tag_image(int language, int scale) {
    const auto* f = the_font();
    if (f == nullptr) {
        return -1;
    }
    if (tag.image >= 0 && tag.scale == scale && tag.language == language) {
        return tag.image;
    }
    khdays_ui_image_put(tag.image);
    tag.image = -1;
    const auto text = khdays::assets::render_text(*f, utf16(khdays_text_in(TXT_SAVE_AUTOSAVE_TAG, language)));
    // the white 0's fill on its first row and its outline (its last column)
    const std::uint8_t fill = digits.white[0][2], outline = digits.white[0][7];
    tag.w = text.width + 1;
    tag.h = text.height + 1;
    tag.scale = scale;
    tag.language = language;
    tag.image = khdays_ui_image_get(tag.w * scale, tag.h * scale);
    std::uint32_t* texels = tag.image >= 0 ? khdays_ui_image_begin(tag.image) : nullptr;
    if (texels == nullptr) {
        return -1;
    }
    auto ink = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < text.width && y < text.height &&
               text.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(text.width) +
                          static_cast<std::size_t>(x)) * 4u + 3u] >= 128;
    };
    for (int y = 0; y < tag.h * scale; ++y) {
        for (int x = 0; x < tag.w * scale; ++x) {
            const int dx = x / scale, dy = y / scale;
            // the letters, and their outline right of and below them
            const std::uint32_t v = ink(dx, dy) ? fill : ink(dx - 1, dy - 1) || ink(dx - 1, dy) || ink(dx, dy - 1) ? outline : 0u;
            texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(tag.w * scale) + static_cast<std::size_t>(x)] =
                v == 0 ? 0u : KET_OPAQUE | v;
        }
    }
    khdays_ui_image_end(tag.image);
    return tag.image;
}

const std::uint8_t* obj_memory(int engine, std::uint32_t address) {
    const KhdaysVramPages* pages = khdays_vram_pages();
    address %= engine ? 0x20000u : 0x40000u;
    const std::uint8_t* page = pages->page[engine ? KHDAYS_AREA_BOBJ : KHDAYS_AREA_AOBJ][address / KHDAYS_VRAM_PAGE];
    return page != nullptr ? page + address % KHDAYS_VRAM_PAGE : nullptr;
}

// Which white digit an 8x8 256-colour sprite of `engine` draws (its tile at `tile`), or -1.
int digit_of(int engine, std::uint32_t dispcnt, std::uint32_t tile) {
    Glyph g{};
    const std::uint32_t base = (dispcnt & 0x10u) ? tile * (32u << ((dispcnt >> 20) & 3u)) : (tile & ~1u) * 32u;
    for (int p = 0; p < 64; ++p) {
        const std::uint8_t* b = obj_memory(engine, base + static_cast<std::uint32_t>(p));
        g[p] = b != nullptr ? *b : 0;
    }
    for (int d = 0; d < 4; ++d) {
        if (g == digits.white[d]) {
            return d;
        }
    }
    return -1;
}

int image_of(int d, int scale) {
    if (digits.scale != scale) {
        for (int& im : digits.image) {
            khdays_ui_image_put(im);
            im = -1;
        }
        digits.scale = scale;
    }
    if (digits.image[d] < 0) {
        digits.image[d] = khdays_ui_image_get(8 * scale, 8 * scale);
        std::uint32_t* texels = digits.image[d] >= 0 ? khdays_ui_image_begin(digits.image[d]) : nullptr;
        if (texels == nullptr) {
            return -1;
        }
        for (int y = 0; y < 8 * scale; ++y) {
            for (int x = 0; x < 8 * scale; ++x) {
                const std::uint8_t v = digits.drawn[d][(y / scale) * 8 + x / scale];
                texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(8 * scale) + static_cast<std::size_t>(x)] =
                    v == 0 ? 0u : KET_OPAQUE | v;
            }
        }
        khdays_ui_image_end(digits.image[d]);
    }
    return digits.image[d];
}

// One engine's rows: the game's digits found, the file's drawn in their place.
void emit_engine(int engine, int top) {
    const std::uint32_t dispcnt = IO32(engine ? 0x1000u : 0u);
    if (!(dispcnt & 0x1000u)) {
        return;  // no sprites shown
    }
    const std::uint8_t* oam = static_cast<const std::uint8_t*>(KHDAYS_W(0x07000000u + 0x400u * static_cast<std::uint32_t>(engine)));
    struct Found {
        int index, x, y, digit;
        std::uint32_t slot;
    } found[128];
    int n = 0;
    for (int i = 0; i < 128; ++i) {
        const std::uint32_t a0 = static_cast<std::uint32_t>(oam[8 * i] | oam[8 * i + 1] << 8);
        const std::uint32_t a1 = static_cast<std::uint32_t>(oam[8 * i + 2] | oam[8 * i + 3] << 8);
        const std::uint32_t a2 = static_cast<std::uint32_t>(oam[8 * i + 4] | oam[8 * i + 5] << 8);
        const std::uint32_t mode = (a0 >> 10) & 3u;
        // shown, not affine, 8x8 (square, size 0), 256 colours, normal or semi-transparent, no flips
        if ((a0 & 0x300u) != 0 || (a0 >> 14) != 0 || (a1 >> 14) != 0 || !((a0 >> 13) & 1u) || mode >= 2u ||
            (a1 & 0x3000u) != 0) {
            continue;
        }
        const int d = digit_of(engine, dispcnt, a2 & 0x3ffu);
        if (d < 0) {
            continue;
        }
        int x = static_cast<int>(a1 & 0x1ffu), y = static_cast<int>(a0 & 0xffu);
        x = x >= 256 ? x - 512 : x;
        y = y >= 192 ? y - 256 : y;
        found[n++] = {i, x, y, d, KES_SPRITES | ((a2 >> 10) & 3u) | (mode == 1u ? 1u << KES_SEMI_SHIFT : 0u)};
    }
    const int scale = khdays_ui_scale();
    for (int u = 0; u < n; ++u) {
        // a row's units (its row + 1) and the tens (0) beside it
        if (found[u].digit < 1) {
            continue;
        }
        int t = -1;
        for (int k = 0; k < n && t < 0; ++k) {
            if (found[k].digit == 0 && found[k].y == found[u].y && found[k].x < found[u].x &&
                found[u].x - found[k].x <= kUnitsFromTens) {
                t = k;
            }
        }
        if (t < 0) {
            continue;
        }
        const int file = top + found[u].digit;  // the row's file, counted from 1
        if (file == kAutosaveFile) {
            const int image = tag_image(GetLanguage(), scale);
            if (image >= 0) {
                // centred over the two digits' dots (each glyph's dots in its columns 2-7)
                KhdaysUiElement e;
                e.id = "hud.save_autosave";
                e.screen = engine;
                e.slot = found[u].slot;
                e.x = static_cast<float>((found[t].x + 2 + found[u].x + 8) / 2 - tag.w / 2);
                e.y = static_cast<float>(found[u].y - kTagGap - tag.h);
                e.w = static_cast<float>(tag.w);
                e.h = static_cast<float>(tag.h);
                e.image = image;
                khdays_ui_emit(KHDAYS_UI_HUD, &e);
            }
        }
        const int shown[2] = {file / 10, file % 10};
        const Found* at[2] = {&found[t], &found[u]};
        for (int k = 0; k < 2; ++k) {
            const int image = image_of(shown[k], scale);
            if (image < 0) {
                return;
            }
            KhdaysUiElement e;
            e.id = "hud.save_number";
            e.screen = engine;
            e.slot = at[k]->slot;
            e.x = static_cast<float>(at[k]->x);
            e.y = static_cast<float>(at[k]->y);
            e.w = 8.0f;
            e.h = 8.0f;
            e.image = image;
            khdays_ui_emit(KHDAYS_UI_HUD, &e);
            khdays_ui_left_out_obj(KHDAYS_UI_HUD, engine, at[k]->index);
        }
    }
}

}  // namespace

extern "C" void khdays_ui_save_numbers_emit(void) {
    const int top = khdays_slots_page();
    if (top <= 0 || khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_ON) {
        return;  // the first page's are the game's own numbers
    }
    const int language = GetLanguage();
    if (language != digits.tried) {
        digits.tried = language;
        for (int& im : digits.image) {
            if (digits.scale != 0) {
                khdays_ui_image_put(im);  /* the digits are the new language's */
            }
            im = -1;
        }
        digits.ok = read_digits(language);
    }
    if (digits.ok) {
        emit_engine(0, top);  // the camps' lists (the upper screen)
        emit_engine(1, top);  // the title's (the lower)
    }
}
