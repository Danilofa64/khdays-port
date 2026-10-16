#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "khdays/assets/texture.h"  // DecodedTexture

namespace khdays::assets {

// A decoded NCLR palette: a flat list of RGBA colors grouped into fixed-size
// sub-palettes (16 colors for 4bpp graphics, 256 for 8bpp).
struct Palette2D final {
    std::vector<std::array<std::uint8_t, 4>> colors;
    int colors_per_palette = 16;
};

// Decoded NCGR character (tile) graphics: 8x8 tiles of palette indices, stored
// tile by tile in row-major order.
struct TileGraphics final {
    int bpp = 4;  // 4 or 8
    int tile_count = 0;
    std::vector<std::uint8_t> indices;  // tile_count * 64 palette indices
};

// A decoded NSCR tilemap: one cell per 8x8 screen tile.
struct Tilemap final {
    int width_tiles = 0;
    int height_tiles = 0;
    struct Cell final {
        std::uint16_t tile = 0;
        std::uint8_t palette = 0;
        bool flip_h = false;
        bool flip_v = false;
    };
    std::vector<Cell> cells;
};

// Decode DS 2D graphics resources from memory (already decompressed), e.g. a
// Nitro resource carved out of a pack. Throw std::runtime_error on bad data.
Palette2D decode_nclr(const std::uint8_t* data, std::size_t size);
TileGraphics decode_ncgr(const std::uint8_t* data, std::size_t size);
Tilemap decode_nscr(const std::uint8_t* data, std::size_t size);

// A view of one resource inside a larger blob; empty if absent.
struct ResourceView final {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    explicit operator bool() const {
        return data != nullptr;
    }
};

// A parsed D2KP pack, the game's UI containers (the sub-files of UI/mlt/res.p2): typed sections of
// Nitro resources, each vector the raw resource bytes indexed as the game indexes that type, to decode
// with decode_nclr/ncgr/nscr and cell.h's.
struct Pk2dPack final {
    std::vector<ResourceView> palettes;  // NCLR (RLCN)
    std::vector<ResourceView> tiles;     // NCGR (RGCN)
    std::vector<ResourceView> screens;   // NSCR (RCSN)
    std::vector<ResourceView> cells;     // NCER (RECN)
    std::vector<ResourceView> anims;     // NANR (RNAN)
};

// Parse a D2KP pack (views point into `data`, which must outlive the result).
// Resources are classified by their own magic, so a resource lands in the right
// vector regardless of which header slot referenced it. Absent types stay empty;
// a non-D2KP blob yields an all-empty pack.
Pk2dPack parse_pk2d(const std::uint8_t* data, std::size_t size);

// A KAPH or D2KP slot container: eight slot pointers at +0x08, each a section {count, offsets, sizes}
// of raw entries; the game's models in slot 7, their animations in slot 0 (docs/FORMATS.md). Views
// point into `data`, which must outlive the result; `valid` false for a blob of neither magic.
struct SlotContainer final {
    std::array<std::vector<ResourceView>, 8> slots{};
    bool valid = false;
};
SlotContainer parse_slot_container(const std::uint8_t* data, std::size_t size);

// pass false for an opaque background (e.g. a full-screen logo).
DecodedTexture compose_background(const Tilemap& map, const TileGraphics& tiles, const Palette2D& palette,
                                  bool color_zero_transparent = true);

}  // namespace khdays::assets
