// High-resolution atlas metadata for an original NitroFS font resource.
#ifndef KHDAYS_HD_FONTS_H
#define KHDAYS_HD_FONTS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace khdays::hd_fonts {

struct Glyph {
    int x = 0, y = 0, w = 0, h = 0; // reference-atlas pixels, NOT output size
    int left = 0, width = 0, advance = 0; // original logical (DS) metrics
    // Actual image-pixel rectangle, scaled from the reference atlas.
    double image_x = 0, image_y = 0, image_w = 0, image_h = 0;
};
struct Mapping {
    std::uint32_t codepoint = 0;
    std::uint32_t glyph = 0;
};
struct Atlas {
    std::string source;
    std::string png_path;
    int width = 0, height = 0; // actual replacement PNG dimensions
    int reference_width = 0, reference_height = 0;
    int cell_width = 0, cell_height = 0, baseline = 0, line_feed = 0;
    int max_width = 0;
    std::vector<Glyph> glyphs;
    std::vector<Mapping> charmap;
    std::vector<std::uint8_t> rgba; // actual_width * actual_height * 4
};

// Finds mods/<Mod>/fonts/<original-resource-path>.png, reading a .json beside it
// from the SAME mod. Returns null for no mod or invalid data; errors are logged.
// An original NFTR can be 'text/font_eu_10.nftr' or 'sf/ssfont_8.nftr'.
std::shared_ptr<const Atlas> load(const std::string &original_resource);

// Called by the offline graphics dump for an initial independent smoke test.
// Return: 0=no override, 1=valid, -1=invalid override.
int probe(const std::string &original_resource);

} // namespace khdays::hd_fonts
#endif
