// LZ compression in the two forms the game's files use: type 0x10 ("LZ10")
// and 0x11 ("LZ11"), header byte, 24-bit size (or three zeros and a 32-bit
// one). Both occur: of the 1,114 `.z` files 947 are 0x11, 166 0x10 and one neither; of
// the P2 archives' compressed entries 5,164 are 0x11 and 2,921 0x10.
// A rebuilt file keeps its original's type: whether
// the game reads the other type in its place is not known. Any valid stream
// decodes to the same bytes (the port's decoder: src/assets/message.cpp
// lz_decompress); Nintendo's own compressor's bytes are not matched.
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
