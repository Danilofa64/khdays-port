#include "khdays/assets/cell.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

// NCER (cell/OBJ sprite) and NANR (cell animation) decoders, plus cell → RGBA
// composition. The container magics are the little-endian Nitro forms ("RECN"
// with a "KBEC" section; "RNAN" with a "KNBA" section).

namespace khdays::assets {

namespace {

std::uint16_t u16(const std::uint8_t* d, std::size_t size, std::size_t o) {
    if (o + 2U > size) {
        throw std::runtime_error("cell: read past end");
    }
    return static_cast<std::uint16_t>(d[o] | (d[o + 1U] << 8U));
}

std::uint32_t u32(const std::uint8_t* d, std::size_t size, std::size_t o) {
    if (o + 4U > size) {
        throw std::runtime_error("cell: read past end");
    }
    return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1U]) << 8U) |
           (static_cast<std::uint32_t>(d[o + 2U]) << 16U) | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

// OBJ (shape, size) → (width, height) in pixels.
std::array<int, 2> oam_dimensions(int shape, int size) {
    static const int table[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},  // square
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},  // wide
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},  // tall
    };
    if (shape < 0 || shape > 2 || size < 0 || size > 3) {
        return {8, 8};
    }
    return {table[shape][size][0], table[shape][size][1]};
}

}  // namespace

CellBank decode_ncer(const std::uint8_t* data, const std::size_t size) {
    if (size < 0x30U || data[0] != 'R' || data[1] != 'E' || data[2] != 'C' || data[3] != 'N') {
        throw std::runtime_error("not an NCER cell bank");
    }
    constexpr std::size_t kb = 0x10U;  // KBEC section
    const auto num_cells = u16(data, size, kb + 0x08U);
    const auto cell_type = u16(data, size, kb + 0x0AU);
    const auto cell_data_off = u32(data, size, kb + 0x0CU);
    const auto mapping_mode = u32(data, size, kb + 0x10U);

    CellBank bank;
    bank.tile_boundary = 32 << (mapping_mode & 0x7U);
    const std::size_t record_size = cell_type == 1U ? 16U : 8U;
    const std::size_t records = kb + 8U + cell_data_off;
    const std::size_t oam_base = records + static_cast<std::size_t>(num_cells) * record_size;

    bank.cells.reserve(num_cells);
    for (std::uint16_t c = 0; c < num_cells; ++c) {
        const std::size_t rec = records + static_cast<std::size_t>(c) * record_size;
        const auto num_oam = u16(data, size, rec);
        const auto oam_off = u32(data, size, rec + 4U);
        Cell cell;
        cell.pieces.reserve(num_oam);
        for (std::uint16_t j = 0; j < num_oam; ++j) {
            const std::size_t p = oam_base + oam_off + static_cast<std::size_t>(j) * 6U;
            const auto a0 = u16(data, size, p);
            const auto a1 = u16(data, size, p + 2U);
            const auto a2 = u16(data, size, p + 4U);

            OamPiece piece;
            const int shape = a0 >> 14;
            const int obj_size = (a1 >> 14) & 0x3;
            const auto dims = oam_dimensions(shape, obj_size);
            piece.width = dims[0];
            piece.height = dims[1];
            int y = a0 & 0xFF;
            if (y >= 128) {
                y -= 256;
            }
            int x = a1 & 0x1FF;
            if (x >= 256) {
                x -= 512;
            }
            piece.x = x;
            piece.y = y;
            piece.tile = a2 & 0x3FF;
            piece.palette = (a2 >> 12) & 0xF;
            piece.flip_h = (a1 & 0x1000) != 0;
            piece.flip_v = (a1 & 0x2000) != 0;
            cell.pieces.push_back(piece);
        }
        bank.cells.push_back(std::move(cell));
    }
    return bank;
}

}  // namespace khdays::assets
