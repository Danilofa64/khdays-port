// Paths as the tool shows and stores them: UTF-8 with '/' separators, the way
// the port's dumps and mods name things.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace studio {

namespace fs = std::filesystem;

// A path as UTF-8 with '/' separators.
std::string u8(const fs::path& path);

// A path as UTF-8 with the system's separators (for other programs).
std::string native_u8(const fs::path& path);

// A path from UTF-8.
fs::path path_u8(std::string_view utf8);

// ASCII lower case (names are matched without regard to case).
std::string lower(std::string_view text);

// Whether `path` lies inside `dir` once both are made absolute and normal
// (no symbolic links resolved: the files may not exist yet).
bool inside(const fs::path& dir, const fs::path& path);

}  // namespace studio
