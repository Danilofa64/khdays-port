// The game's texts as the studio edits them, and written back: string tables and message databases
// (docs/FORMATS.md, "Text"), control codes kept as the game has them.
#pragma once

#include "core/files/containers.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace studio {

struct TextFile {
    bool database = false;
    std::vector<std::vector<std::u16string>> lists;  // a string table: one list
    std::uint8_t lz = 0;                             // a string table's LZ type (0x10, 0x11), 0 when stored plain
    std::size_t header_size = 8;                     // a string table's header
    P2File p2;                                       // a database's container: its head and each entry's LZ kept
};

bool parse_text_file(const std::vector<std::uint8_t>& bytes, bool database, TextFile* out, std::string* why);
std::vector<std::uint8_t> write_text_file(const TextFile& t);

// A text item's file is a database (db/db_<lang>.p2) rather than a string table.
bool is_text_database(const std::string& game_path);

// The characters of `text` the font cannot draw (control codes left out),
// each once, in order.
std::u16string missing_glyphs(const std::u16string& text, const std::function<bool(char16_t)>& font_has);

// --check-texts: every text file of the game read and written back the same
// bytes (decompressed), every string's count said.
bool check_all_texts(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line);

}  // namespace studio
