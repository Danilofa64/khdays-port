// The game's texts the port words for a PC (text_patches.cpp).
#ifndef KHDAYS_TEXT_PATCHES_H
#define KHDAYS_TEXT_PATCHES_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The game file `game_path` (as the game's FNT names it) read from `source`
// with the port's wording in place of the DS's: true and the file's new bytes
// in `out`; false when the file is not one of them, or holds none of the
// game's own words there (a mod's), served as it is.
bool khdays_text_patch(const std::string& game_path, const std::filesystem::path& source,
                       std::vector<std::uint8_t>& out);

#endif
