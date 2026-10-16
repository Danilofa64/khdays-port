#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "khdays/assets/graphics2d.h"  // TileGraphics, Palette2D, DecodedTexture

// Decoders for the Nintendo DS OBJ/sprite resources: NCER (cell banks — sprites
// composed of OAM pieces) and NANR (cell animations). A cell references tiles in
// an NCGR and colours in an NCLR, so render_cell composes one to RGBA.
namespace khdays::assets {

// One OAM piece of a cell: a hardware sprite rectangle placed at (x, y).
struct OamPiece final {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int tile = 0;  // OBJ tile number (in mapping units)
    int palette = 0;
    bool flip_h = false;
    bool flip_v = false;
};

struct Cell final {
    std::vector<OamPiece> pieces;
};

struct CellBank final {
    std::vector<Cell> cells;
    int tile_boundary = 32;  // bytes per OBJ tile-mapping unit (from mappingMode)
};

// Decode an NCER cell bank from an in-memory (decompressed) resource.
CellBank decode_ncer(const std::uint8_t* data, std::size_t size);

}  // namespace khdays::assets
