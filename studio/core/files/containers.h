// The game's containers, rebuilt with one piece changed and everything else as it was: P2 and KAPH,
// their tables, gaps and alignments kept (docs/FORMATS.md, "Containers").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace studio {

struct P2Entry {
    bool compressed = false;         // its descriptor's bit 31
    std::uint8_t lz = 0;             // its LZ type (0x10, 0x11), kept; 0 when stored empty (some are, flagged)
    std::vector<std::uint8_t> data;  // as the game uses it (decompressed)
};

struct P2File {
    std::vector<std::uint8_t> head;  // the original's bytes before `base`
    std::uint32_t base = 0x200;
    std::vector<P2Entry> entries;
};

bool parse_p2(const std::vector<std::uint8_t>& file, P2File* out, std::string* why);
std::vector<std::uint8_t> write_p2(const P2File& p2);

struct KaphBlock {
    int section = 0;
    std::size_t index = 0;
    std::uint32_t offset = 0;  // in the original
    std::vector<std::uint8_t> data;
};

struct KaphFile {
    std::vector<std::uint8_t> head;  // the header and the section tables, as in the original
    std::vector<KaphBlock> blocks;   // in the original's order of offsets
};

bool parse_kaph(const std::vector<std::uint8_t>& file, KaphFile* out, std::string* why);
std::vector<std::uint8_t> write_kaph(const KaphFile& kaph);

// A "CAKP" archive (the mission files' scripts): laid
// out as KAPH under its own magic (`Archive_GetMember`), written by write_kaph.
bool parse_cakp(const std::vector<std::uint8_t>& file, KaphFile* out, std::string* why);

}  // namespace studio
