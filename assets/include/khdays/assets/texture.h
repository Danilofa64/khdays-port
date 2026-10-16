#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace khdays::assets {

// An image decoded to RGBA8, rows top to bottom.
struct DecodedTexture final {
    std::string name;
    std::optional<std::string> palette_name;
    std::string format_name;
    int width = 0;
    int height = 0;
    bool color_zero_transparent = false;
    std::vector<std::uint8_t> rgba;
};

}  // namespace khdays::assets
