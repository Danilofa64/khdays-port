// Each room's signal and lock on the game's own list of rooms (room_icons.h): the members' screen's
// own link icon (UI/mlt/res.p2 entry 1, cells 12-15) for the level a session with the room would have
// (khdays_net_link_level of its ping), and a lock before the players of a room with a password, in
// the icon's colours (no game font has one). docs/RUNTIME_MAP.md 16.2.
#include "room_icons.h"

extern "C" {
#include "elements.h"
#include "parts.h"
#include "../runtime/core/rom.h"
#include "../runtime/net/roomlist.h"
}

#include "khdays/assets/cell.h"
#include "khdays/assets/graphics2d.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>

namespace {

constexpr int kFirstLevelCell = 12; // link level 0's cell, the others after it
constexpr int kLevels = 4;
constexpr int kIcon = 16;
constexpr int kPlates = 4;
constexpr int kPlateRight = 192, kPlateY = 32, kPlatePitch = 24; // measured (R4)
constexpr int kIconGap = 2;                                      // dots between a plate and its icon
constexpr int kLockRight = 166; // the lock's right edge: before the players (right-aligned at 184)
constexpr int kLockW = 8, kLockH = 9;

// The lock's shape: 1 its outline, 2 its fill.
constexpr const char *kLock[kLockH] = {
    "..1111..", ".1....1.", ".1....1.", "11111111", "12222221", "12211221", "12211221", "12222221", "11111111",
};

struct Icons {
    bool tried = false, ok = false;
    std::array<std::array<std::uint8_t, kIcon * kIcon>, kLevels> level{}; // the OBJ palette's indices
    std::uint8_t outline = 0, fill = 0;
    std::array<int, kLevels> image{-1, -1, -1, -1};
    int lock_image = -1, scale = 0;
};
Icons icons;
KhdaysRomReader *reader; // its own: this runs at VBlank, maybe inside the game's own reading

bool read_icons()
{
    std::uint8_t *data = nullptr;
    std::uint32_t size = 0;
    if (reader == nullptr) {
        reader = khdays_rom_reader_new();
    }
    if (!khdays_rom_file_data(reader, "UI/mlt/res.p2", 1, &data, &size)) {
        std::free(data);
        return false;
    }
    bool ok = false;
    try {
        const auto pack = khdays::assets::parse_pk2d(data, size);
        if (!pack.tiles.empty() && !pack.cells.empty()) {
            const auto tiles = khdays::assets::decode_ncgr(pack.tiles[0].data, pack.tiles[0].size);
            const auto bank = khdays::assets::decode_ncer(pack.cells[0].data, pack.cells[0].size);
            ok = static_cast<int>(bank.cells.size()) >= kFirstLevelCell + kLevels;
            for (int l = 0; ok && l < kLevels; ++l) {
                auto &out = icons.level[static_cast<std::size_t>(l)];
                out.fill(0);
                for (const auto &piece : bank.cells[static_cast<std::size_t>(kFirstLevelCell + l)].pieces) {
                    // each 8x8 character of the piece, in the sheet's 1D order from its tile
                    const int units = bank.tile_boundary / (tiles.bpp == 8 ? 64 : 32);
                    const int first = piece.tile * (units > 0 ? units : 1);
                    for (int py = 0; py < piece.height; ++py) {
                        for (int px = 0; px < piece.width; ++px) {
                            const int c = first + (py / 8) * (piece.width / 8) + px / 8;
                            if (c < 0 || c >= tiles.tile_count) {
                                continue;
                            }
                            const std::uint8_t v = tiles.indices[static_cast<std::size_t>(c) * 64u +
                                                                 static_cast<std::size_t>((py % 8) * 8 + px % 8)];
                            const int dx = piece.x + (piece.flip_h ? piece.width - 1 - px : px) + kIcon / 2;
                            const int dy = piece.y + (piece.flip_v ? piece.height - 1 - py : py) + kIcon / 2;
                            if (v != 0 && dx >= 0 && dy >= 0 && dx < kIcon && dy < kIcon) {
                                out[static_cast<std::size_t>(dy * kIcon + dx)] =
                                    static_cast<std::uint8_t>(tiles.bpp == 8 ? v : piece.palette * 16 + v);
                            }
                        }
                    }
                }
            }
            if (ok) {
                // the icon's colours: its frame (the most used) and the white inside (the next)
                std::map<int, int> count;
                for (const auto v : icons.level[kLevels - 1]) {
                    if (v != 0) {
                        ++count[v];
                    }
                }
                int best = -1, second = -1;
                for (const auto &[v, n] : count) {
                    if (best < 0 || n > count[best]) {
                        second = best;
                        best = v;
                    } else if (second < 0 || n > count[second]) {
                        second = v;
                    }
                }
                ok = best > 0 && second > 0;
                icons.outline = static_cast<std::uint8_t>(best > 0 ? best : 0);
                icons.fill = static_cast<std::uint8_t>(second > 0 ? second : 0);
            }
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "ui: the rooms' signal could not be read from UI/mlt/res.p2 (%s)\n", e.what());
        ok = false;
    }
    std::free(data);
    if (!ok) {
        std::fprintf(stderr, "ui: UI/mlt/res.p2 entry 1 has not the link icons expected; the rooms show no signal\n");
    }
    return ok;
}

void forget_images()
{
    for (int &im : icons.image) {
        khdays_ui_image_put(im);
        im = -1;
    }
    khdays_ui_image_put(icons.lock_image);
    icons.lock_image = -1;
}

int image_of(const std::uint8_t *indices, int w, int h, int *slot)
{
    if (*slot >= 0) {
        return *slot;
    }
    const int scale = icons.scale;
    *slot = khdays_ui_image_get(w * scale, h * scale);
    std::uint32_t *texels = *slot >= 0 ? khdays_ui_image_begin(*slot) : nullptr;
    if (texels == nullptr) {
        return -1;
    }
    for (int y = 0; y < h * scale; ++y) {
        for (int x = 0; x < w * scale; ++x) {
            const std::uint8_t v = indices[(y / scale) * w + x / scale];
            texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w * scale) + static_cast<std::size_t>(x)] =
                v == 0 ? 0u : KET_OPAQUE | v;
        }
    }
    khdays_ui_image_end(*slot);
    return *slot;
}

int level_image(int level)
{
    return image_of(icons.level[static_cast<std::size_t>(level)].data(), kIcon, kIcon,
                    &icons.image[static_cast<std::size_t>(level)]);
}

int lock_image()
{
    std::array<std::uint8_t, kLockW * kLockH> shape{};
    for (int y = 0; y < kLockH; ++y) {
        for (int x = 0; x < kLockW; ++x) {
            const char c = kLock[y][x];
            shape[static_cast<std::size_t>(y * kLockW + x)] = c == '1' ? icons.outline : c == '2' ? icons.fill : 0;
        }
    }
    return image_of(shape.data(), kLockW, kLockH, &icons.lock_image);
}

void emit(const char *id, int image, int x, int y, int w, int h)
{
    if (image < 0) {
        return;
    }
    KhdaysUiElement e;
    e.id = id;
    e.screen = 1;              // engine B's: the game's list is on its sprites
    e.slot = KES_SPRITES | 0u; // over the plates (theirs are priority 1)
    e.x = static_cast<float>(x);
    e.y = static_cast<float>(y);
    e.w = static_cast<float>(w);
    e.h = static_cast<float>(h);
    e.image = image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}

} // namespace

extern "C" void khdays_ui_room_icons_emit(void)
{
    if (khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_ON) {
        return;
    }
    const KhdaysRoomView *v = khdays_roomlist_hold();
    if (v != nullptr && v->look_game && v->screen == KHDAYS_ROOMS_LIST) {
        if (!icons.tried) {
            icons.tried = true;
            icons.ok = read_icons();
        }
        if (icons.ok) {
            if (icons.scale != khdays_ui_scale()) {
                forget_images();
                icons.scale = khdays_ui_scale();
            }
            for (int plate = 0; plate < kPlates; ++plate) {
                const int r = v->top + plate;
                if (r >= v->row_count - 1 || v->rows[r].kind != KHDAYS_ROOMROW_ROOM) {
                    continue;
                }
                const KhdaysNetRoomView &room = v->rows[r].room;
                const int y = kPlateY + plate * kPlatePitch;
                if (room.ping_ms >= 0) {
                    const int level = room.ping_ms < 100 ? 3 : room.ping_ms < 200 ? 2 : 1;
                    emit("hud.room_signal", level_image(level), kPlateRight + kIconGap, y, kIcon, kIcon);
                }
                if (room.locked) {
                    emit("hud.room_lock", lock_image(), kLockRight - kLockW, y + (kIcon - kLockH) / 2, kLockW, kLockH);
                }
            }
        }
    }
    khdays_roomlist_release();
}
