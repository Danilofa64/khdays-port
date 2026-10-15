#include "core/missions/mission_db.h"

#include "core/files/paths.h"

#include "khdays/assets/message.h"  // lz_decompress

#include <algorithm>
#include <fstream>
#include <iterator>

namespace studio {

namespace {

std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | static_cast<std::uint32_t>(d[at + 1]) << 8 | static_cast<std::uint32_t>(d[at + 2]) << 16 |
           static_cast<std::uint32_t>(d[at + 3]) << 24;
}
void add32(std::vector<std::uint8_t>& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}
bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr) *why = reason;
    return false;
}

}  // namespace

bool parse_counted_records(const std::vector<std::uint8_t>& d, std::size_t record_size, CountedRecords* out, std::string* why) {
    if (d.size() < 4) return fail_with(why, "shorter than its count");
    const std::size_t n = le32(d, 0);
    if (4 + n * record_size != d.size()) return fail_with(why, "its count of records is not its size");
    CountedRecords r;
    r.record_size = record_size;
    for (std::size_t i = 0; i < n; ++i) {
        r.records.emplace_back(d.begin() + static_cast<std::ptrdiff_t>(4 + i * record_size),
                               d.begin() + static_cast<std::ptrdiff_t>(4 + (i + 1) * record_size));
    }
    *out = std::move(r);
    return true;
}

std::vector<std::uint8_t> write_counted_records(const CountedRecords& r) {
    std::vector<std::uint8_t> d;
    add32(d, static_cast<std::uint32_t>(r.records.size()));
    for (const auto& rec : r.records) d.insert(d.end(), rec.begin(), rec.end());
    return d;
}

std::vector<std::uint8_t> OffsetRecords::record(std::size_t i) const {
    if (i >= offsets.size()) return {};
    const std::size_t table = 4 + 4 * offsets.size();
    std::size_t end = table + body.size();
    for (const auto o : offsets) {
        if (o > offsets[i] && o < end) end = o;
    }
    if (offsets[i] < table || offsets[i] > end) return {};
    return std::vector<std::uint8_t>(body.begin() + static_cast<std::ptrdiff_t>(offsets[i] - table),
                                     body.begin() + static_cast<std::ptrdiff_t>(end - table));
}

bool parse_offset_records(const std::vector<std::uint8_t>& d, OffsetRecords* out, std::string* why) {
    if (d.size() < 4) return fail_with(why, "shorter than its count");
    const std::size_t n = le32(d, 0);
    const std::size_t table = 4 + 4 * n;
    if (table > d.size()) return fail_with(why, "its offsets past its end");
    OffsetRecords r;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t o = le32(d, 4 + 4 * i);
        if (o < table || o > d.size()) return fail_with(why, "an offset outside its records");
        r.offsets.push_back(o);
    }
    r.body.assign(d.begin() + static_cast<std::ptrdiff_t>(table), d.end());
    *out = std::move(r);
    return true;
}

std::vector<std::uint8_t> write_offset_records(const OffsetRecords& r) {
    std::vector<std::uint8_t> d;
    add32(d, static_cast<std::uint32_t>(r.offsets.size()));
    for (const auto o : r.offsets) add32(d, o);
    d.insert(d.end(), r.body.begin(), r.body.end());
    return d;
}

bool parse_placed_sets(const std::vector<std::uint8_t>& d, std::vector<PlacedSet>* out, std::string* why) {
    OffsetRecords r;
    if (!parse_offset_records(d, &r, why)) return false;
    std::vector<PlacedSet> sets;
    for (const auto o : r.offsets) {
        if (o + 4 > d.size()) return fail_with(why, "a set past the file");
        PlacedSet s;
        s.at = o;
        s.mission = static_cast<int>(d[o] | d[o + 1] << 8);
        const int count = static_cast<std::int8_t>(d[o + 2]);
        if (count < 0 || o + 4 + 0x14ull * static_cast<unsigned>(count) > d.size()) return fail_with(why, "a set's rows past the file");
        for (int i = 0; i < count; ++i) {
            const std::size_t at = o + 4 + 0x14 * static_cast<std::size_t>(i);
            PlacedRow row;
            row.b0 = static_cast<std::int8_t>(d[at]);
            row.b1 = static_cast<std::int8_t>(d[at + 1]);
            row.h2 = static_cast<std::int16_t>(d[at + 2] | d[at + 3] << 8);
            row.h4 = static_cast<std::int16_t>(d[at + 4] | d[at + 5] << 8);
            row.h6 = static_cast<std::int16_t>(d[at + 6] | d[at + 7] << 8);
            for (int k = 0; k < 3; ++k) row.at[static_cast<std::size_t>(k)] = static_cast<std::int32_t>(le32(d, at + 8 + 4 * static_cast<std::size_t>(k)));
            s.rows.push_back(row);
        }
        sets.push_back(std::move(s));
    }
    *out = std::move(sets);
    return true;
}

const PlacedSet* placed_set(const std::vector<PlacedSet>& sets, int mission) {
    for (const auto& s : sets) {
        if (s.mission == mission) return &s;
    }
    return nullptr;
}

bool load_placed_sets(const std::filesystem::path& nitrofs, const std::string& name, std::vector<PlacedSet>* out, std::string* why) {
    std::ifstream in(nitrofs / "mi" / "mi" / path_u8(name), std::ios::binary);
    std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.empty()) return fail_with(why, "cannot read mi/mi/" + name);
    return parse_placed_sets(d, out, why);
}

std::vector<std::string> mission_db_files() {
    return {"mdb.z", "eid.z", "evi", "trbox"};
}

bool check_mission_db_file(const std::filesystem::path& nitrofs, const std::string& name, std::string* why) {
    std::ifstream in(nitrofs / "mi" / "mi" / path_u8(name), std::ios::binary);
    std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.empty()) return fail_with(why, "cannot read it");
    if (name.size() > 2 && name.substr(name.size() - 2) == ".z") {
        try {
            d = khdays::assets::lz_decompress(d);
        } catch (const std::exception& e) {
            return fail_with(why, std::string("its LZ cannot be undone: ") + e.what());
        }
    }
    if (name == "mdb.z" || name == "eid.z") {
        CountedRecords r;
        if (!parse_counted_records(d, name == "mdb.z" ? 60 : 24, &r, why)) return false;
        if (write_counted_records(r) != d) return fail_with(why, "written back differently");
        return true;
    }
    OffsetRecords r;
    if (!parse_offset_records(d, &r, why)) return false;
    if (write_offset_records(r) != d) return fail_with(why, "written back differently");
    std::vector<PlacedSet> sets;  // and read as the field reads it
    return parse_placed_sets(d, &sets, why);
}

}  // namespace studio
