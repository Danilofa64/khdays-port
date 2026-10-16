// LZ compression in the two forms the game's files use, LZ10 and LZ11; a rebuilt file keeps its
// original's type. Any valid stream decodes to the same bytes (assets/src/message.cpp lz_decompress);
// Nintendo's own compressor's bytes are not matched (docs/FORMATS.md, "LZ").
#pragma once

#include <cstdint>
#include <vector>

namespace studio {

// `type` 0x10 or 0x11.
std::vector<std::uint8_t> lz_compress(std::uint8_t type, const std::vector<std::uint8_t>& data);

inline std::vector<std::uint8_t> lz11_compress(const std::vector<std::uint8_t>& data) {
    return lz_compress(0x11, data);
}

}  // namespace studio
