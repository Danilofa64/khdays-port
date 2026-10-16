// The mission database's files beside the mission files (mdb.z, eid.z, evi, trbox), held as read until
// their fields are named from their readers (docs/FORMATS.md, "Missions").
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace studio {

// Records of a fixed size after a count word.
struct CountedRecords {
    std::size_t record_size = 0;
    std::vector<std::vector<std::uint8_t>> records;
};
bool parse_counted_records(const std::vector<std::uint8_t>& d, std::size_t record_size, CountedRecords* out, std::string* why);
std::vector<std::uint8_t> write_counted_records(const CountedRecords& r);

// A count, that many offsets (from the file's start), then the bytes they
// point into; record i runs from its offset to the next larger one (the last
// to the end).
struct OffsetRecords {
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint8_t> body;  // the bytes after the offsets
    std::vector<std::uint8_t> record(std::size_t i) const;
};
bool parse_offset_records(const std::vector<std::uint8_t>& d, OffsetRecords* out, std::string* why);
std::vector<std::uint8_t> write_offset_records(const OffsetRecords& r);

// What the field reads of `trbox` (the chests) and `evi` (the emblems): rows of 0x14 bytes per mission,
// keyed by its number -- slot or bit, kind, keys, flags, a heading, a point (docs/FORMATS.md,
// "Missions").
struct PlacedRow {
    std::int8_t b0 = 0, b1 = 0;
    std::int16_t h2 = 0, h4 = 0, h6 = 0;
    std::array<std::int32_t, 3> at{};
};
struct PlacedSet {
    int mission = 0;
    std::vector<PlacedRow> rows;
    std::size_t at = 0;  // where the set starts in its file
};
bool parse_placed_sets(const std::vector<std::uint8_t>& d, std::vector<PlacedSet>* out, std::string* why);
// The set of `mission`, or null (the first, as Ov002_FindHandlerByKey takes it).
const PlacedSet* placed_set(const std::vector<PlacedSet>& sets, int mission);
// mi/mi/<name> read and its sets parsed (trbox, evi).
bool load_placed_sets(const std::filesystem::path& nitrofs, const std::string& name, std::vector<PlacedSet>* out, std::string* why);

// The four files' names under mi/mi, and the check of one: read (LZ
// undone for .z), parsed, written back the same bytes.
std::vector<std::string> mission_db_files();
bool check_mission_db_file(const std::filesystem::path& nitrofs, const std::string& name, std::string* why);

}  // namespace studio
