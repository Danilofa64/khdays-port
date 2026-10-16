#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/graphics2d.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

void set_u16(Bytes& b, std::size_t o, std::uint16_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}
void set_u32(Bytes& b, std::size_t o, std::uint32_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
    b.at(o + 2U) = static_cast<std::uint8_t>((v >> 16U) & 0xFFU);
    b.at(o + 3U) = static_cast<std::uint8_t>((v >> 24U) & 0xFFU);
}
void set_magic(Bytes& b, const char* m) {
    for (int i = 0; i < 4; ++i) {
        b.at(static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(m[i]);
    }
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    try {
        // NCLR: 4-color palette (index 1 = red RGB555 0x001F).
        Bytes nclr(0x30, 0U);
        set_magic(nclr, "RLCN");
        set_u32(nclr, 0x18U, 3U);   // bit depth 3 -> 16 colors/palette
        set_u32(nclr, 0x20U, 8U);   // data size = 4 colors
        set_u16(nclr, 0x28U + 0U, 0x0000U);  // index 0 black
        set_u16(nclr, 0x28U + 2U, 0x001FU);  // index 1 red
        set_u16(nclr, 0x28U + 4U, 0x03E0U);  // index 2 green
        set_u16(nclr, 0x28U + 6U, 0x7FFFU);  // index 3 white

        // NCGR: one 4bpp tile, every pixel index 1.
        Bytes ncgr(0x50, 0U);
        set_magic(ncgr, "RGCN");
        set_u32(ncgr, 0x1CU, 3U);       // 4bpp
        set_u32(ncgr, 0x28U, 32U);      // data size = 1 tile
        set_u32(ncgr, 0x2CU, 0x18U);    // data offset -> tiles at 0x30
        for (std::size_t i = 0x30U; i < 0x50U; ++i) {
            ncgr[i] = 0x11U;  // two index-1 pixels per byte
        }

        // NSCR: one cell, tile 0, palette 0.
        Bytes nscr(0x26, 0U);
        set_magic(nscr, "RCSN");
        set_u16(nscr, 0x18U, 8U);   // width px
        set_u16(nscr, 0x1AU, 8U);   // height px
        set_u32(nscr, 0x20U, 2U);   // data size = 1 cell
        set_u16(nscr, 0x24U, 0x0000U);

        const auto palette = khdays::assets::decode_nclr(nclr.data(), nclr.size());
        expect(palette.colors.size() == 4U, "palette color count");
        expect(palette.colors[1][0] == 255 && palette.colors[1][1] == 0
                   && palette.colors[1][2] == 0,
               "palette index 1 is red");

        const auto tiles = khdays::assets::decode_ncgr(ncgr.data(), ncgr.size());
        expect(tiles.tile_count == 1, "tile count");
        expect(tiles.bpp == 4, "bpp");
        expect(tiles.indices.size() == 64U, "tile index count");
        expect(tiles.indices[0] == 1 && tiles.indices[63] == 1, "tile indices");

        const auto map = khdays::assets::decode_nscr(nscr.data(), nscr.size());
        expect(map.width_tiles == 1 && map.height_tiles == 1, "tilemap size");
        expect(map.cells.size() == 1U && map.cells[0].tile == 0, "tilemap cell");

        // parse_pk2d: a synthetic D2KP with type-section pointers at +0x08
        // (NCLR), +0x0C (NCGR) and +0x14 (NCER), each a {count, offsets[],
        // sizes[]} section listing one resource. Classification is by the
        // resource's own magic, so a resource lands in the right vector even
        // though the header slots do not encode the type directly.
        {
            const auto put_magic = [](Bytes& b, std::size_t o, const char* m) {
                for (int i = 0; i < 4; ++i) {
                    b.at(o + static_cast<std::size_t>(i)) =
                        static_cast<std::uint8_t>(m[i]);
                }
            };
            Bytes pk(0x130U, 0xFFU);  // 0xFF fill = "empty slot" for unused ptrs
            put_magic(pk, 0x00U, "D2KP");
            set_u32(pk, 0x04U, 0U);
            set_u32(pk, 0x08U, 0x28U);  // NCLR section
            set_u32(pk, 0x0CU, 0x34U);  // NCGR section
            set_u32(pk, 0x10U, 0xFFFFFFFFU);
            set_u32(pk, 0x14U, 0x40U);  // NCER section
            set_u32(pk, 0x18U, 0xFFFFFFFFU);
            set_u32(pk, 0x1CU, 0xFFFFFFFFU);
            set_u32(pk, 0x20U, 0xFFFFFFFFU);
            set_u32(pk, 0x24U, 0xFFFFFFFFU);
            // Sections: {count=1, offset, size}
            set_u32(pk, 0x28U, 1U);
            set_u32(pk, 0x2CU, 0x100U);
            set_u32(pk, 0x30U, 16U);
            set_u32(pk, 0x34U, 1U);
            set_u32(pk, 0x38U, 0x110U);
            set_u32(pk, 0x3CU, 16U);
            set_u32(pk, 0x40U, 1U);
            set_u32(pk, 0x44U, 0x120U);
            set_u32(pk, 0x48U, 16U);
            put_magic(pk, 0x100U, "RLCN");
            put_magic(pk, 0x110U, "RGCN");
            put_magic(pk, 0x120U, "RECN");

            const auto pack = khdays::assets::parse_pk2d(pk.data(), pk.size());
            expect(pack.palettes.size() == 1U, "pk2d NCLR count");
            expect(pack.tiles.size() == 1U, "pk2d NCGR count");
            expect(pack.cells.size() == 1U, "pk2d NCER count");
            expect(pack.screens.empty() && pack.anims.empty(), "pk2d empties");
            expect(pack.palettes[0].data == pk.data() + 0x100U
                       && pack.palettes[0].size == 16U,
                   "pk2d NCLR view");

            // A non-D2KP blob yields an all-empty pack.
            Bytes junk(64U, 0U);
            const auto none = khdays::assets::parse_pk2d(junk.data(), junk.size());
            expect(none.palettes.empty() && none.tiles.empty()
                       && none.cells.empty(),
                   "pk2d rejects non-D2KP");
        }

        {
            // A KAPH/D2KP slot container: eight slot pointers at +0x08, each
            // addressing {u32 count; u32 offsets[count]; u32 sizes[count]}.
            // The game puts the NSBMD in slot 7 and the NSBCA in slot 0.
            Bytes kaph(0x60U, 0U);
            kaph[0] = 'K';
            kaph[1] = 'A';
            kaph[2] = 'P';
            kaph[3] = 'H';
            set_u32(kaph, 0x08U + 7U * 4U, 0x28U);  // slot 7 -> section
            set_u32(kaph, 0x28U, 1U);               // count
            set_u32(kaph, 0x2CU, 0x38U);            // offsets[0]
            set_u32(kaph, 0x30U, 8U);               // sizes[0]
            kaph[0x38U] = 'B';
            kaph[0x39U] = 'M';
            kaph[0x3AU] = 'D';
            kaph[0x3BU] = '0';

            const auto pack =
                khdays::assets::parse_slot_container(kaph.data(), kaph.size());
            expect(pack.valid, "KAPH is recognised as a slot container");
            expect(pack.slots[7].size() == 1U, "slot 7 holds one entry");
            expect(pack.slots[7][0].size == 8U, "slot 7 entry keeps its size");
            expect(pack.slots[7][0].data == kaph.data() + 0x38U,
                   "slot 7 entry points into the container");
            expect(pack.slots[0].empty(), "an unused slot stays empty");

            // The same layout under the UI magic.
            Bytes d2kp = kaph;
            d2kp[0] = 'D';
            d2kp[1] = '2';
            d2kp[2] = 'K';
            d2kp[3] = 'P';
            expect(khdays::assets::parse_slot_container(d2kp.data(), d2kp.size())
                       .slots[7]
                       .size() == 1U,
                   "D2KP shares the slot layout");

            // Anything else is rejected rather than parsed as garbage.
            const Bytes junk(0x40U, 0U);
            expect(!khdays::assets::parse_slot_container(junk.data(), junk.size())
                        .valid,
                   "a blob with no container magic is rejected");

            // A section pointing past the end must not be trusted.
            Bytes bad = kaph;
            set_u32(bad, 0x08U + 7U * 4U, 0xFFFF0000U);
            expect(khdays::assets::parse_slot_container(bad.data(), bad.size())
                       .slots[7]
                       .empty(),
                   "an out-of-range section is skipped");
        }

        std::cout << "2D graphics test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "2D graphics test failed: " << error.what() << '\n';
        return 1;
    }
}
