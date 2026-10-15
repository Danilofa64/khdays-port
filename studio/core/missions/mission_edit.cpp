#include "core/missions/mission_edit.h"

#include "core/files/file_write.h"
#include "core/mods/mods.h"
#include "core/files/paths.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace studio {

namespace {

namespace fs = std::filesystem;

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr) *why = reason;
    return false;
}

std::vector<std::uint8_t> read_bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | static_cast<std::uint32_t>(d[at + 1]) << 8 | static_cast<std::uint32_t>(d[at + 2]) << 16 |
           static_cast<std::uint32_t>(d[at + 3]) << 24;
}

void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = static_cast<std::uint8_t>(v & 0xff);
    d[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
}

void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xff);
}

// The mod's copy of a game file when it holds one, else the game's; `from_mod` says which.
std::vector<std::uint8_t> mod_or_game(const fs::path& nitrofs, const fs::path& mod_dir, const std::string& game_path, bool* from_mod) {
    std::error_code ec;
    if (!mod_dir.empty()) {
        const fs::path mine = mod_dir / "files" / path_u8(game_path);
        if (fs::is_regular_file(mine, ec)) {
            *from_mod = true;
            return read_bytes(mine);
        }
    }
    *from_mod = false;
    return read_bytes(nitrofs / path_u8(game_path));
}

// Writes `bytes` at `rel` in the mod, read back by `verify` first; or, when they are the game's own
// bytes, removes the mod's copy (the game's is the same).
bool write_or_drop(const fs::path& mod_dir, const std::string& rel, const std::vector<std::uint8_t>& bytes,
                   const std::vector<std::uint8_t>& game, const std::function<std::string(const std::vector<std::uint8_t>&)>& verify,
                   std::vector<std::string>* written, std::string* why) {
    const fs::path dest = mod_dir / path_u8(rel);
    std::error_code ec;
    if (bytes == game) {
        if (fs::is_regular_file(dest, ec)) {
            if (!remove_mod_file(mod_dir, dest, why)) return false;
            if (written != nullptr) written->push_back(rel + " (removed: the game's own)");
        }
        return true;
    }
    const WriteResult r = write_into_mod(
        mod_dir, dest,
        [&](const fs::path& temporary) -> std::string {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return out ? std::string() : "cannot write " + u8(temporary);
        },
        [&](const fs::path& temporary) { return verify(read_bytes(temporary)); });
    if (!r.ok) return fail_with(why, r.locked ? rel + ": the file is in use (is the game running?)" : rel + ": " + r.why);
    if (written != nullptr) written->push_back(rel);
    return true;
}

}  // namespace

bool load_mission_edit(const fs::path& nitrofs, const fs::path& mod_dir, int number, MissionEdit* out, std::string* why) {
    MissionEdit e;
    e.number = number;
    const auto mission = mod_or_game(nitrofs, mod_dir, mission_path(number), &e.mission_from_mod);
    if (mission.empty()) return fail_with(why, "cannot read " + mission_path(number));
    if (!parse_mission(mission, number, &e.mission, why)) return false;
    e.trbox = mod_or_game(nitrofs, mod_dir, "mi/mi/trbox", &e.trbox_from_mod);
    e.evi = mod_or_game(nitrofs, mod_dir, "mi/mi/evi", &e.evi_from_mod);
    if (!e.trbox.empty() && !parse_placed_sets(e.trbox, &e.trbox_sets, why)) return false;
    if (!e.evi.empty() && !parse_placed_sets(e.evi, &e.evi_sets, why)) return false;
    *out = std::move(e);
    return true;
}

void set_slot(ScriptStep& step, std::size_t slot, const OperandSlot& o) {
    const std::size_t w = 2 * slot;
    if (w + 1 >= step.words.size()) return;
    step.words[w] = static_cast<std::uint16_t>(o.mode) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(o.sel)) << 16;
    step.words[w + 1] = static_cast<std::uint32_t>(o.value);
}

void set_fx(ScriptStep& step, std::size_t slot, std::int32_t fx) {
    OperandSlot o = operand_at(step, 2 * slot);
    o.mode = 0x10;
    o.sel = 0;
    o.value = fx;
    set_slot(step, slot, o);
}

void set_int(ScriptStep& step, std::size_t slot, std::int32_t value) {
    OperandSlot o = operand_at(step, 2 * slot);
    o.mode = 1;
    o.sel = 0;
    o.value = value;
    set_slot(step, slot, o);
}

bool set_step_text(Script& script, ScriptStep& step, std::size_t slot, int language, const std::string& utf8) {
    OperandSlot o = operand_at(step, 2 * slot);
    if (o.mode != 2 && o.mode != 0x40) return false;
    // the text past the data, NUL-ended
    const auto at = static_cast<std::uint32_t>(script.tail.size());
    script.tail.insert(script.tail.end(), utf8.begin(), utf8.end());
    script.tail.push_back(0);
    if (o.mode == 2) {
        o.value = static_cast<std::int32_t>(at);
        set_slot(step, slot, o);
        return true;
    }
    const std::size_t word = static_cast<std::size_t>(static_cast<std::uint32_t>(o.value)) + 4 * static_cast<std::size_t>(language);
    if (language < 1 || language > 5 || word + 4 > at) {
        script.tail.resize(at);  // the table not there: nothing changed
        return false;
    }
    put32(script.tail, word, at);
    return true;
}

bool write_enemy_table_over(std::vector<std::uint8_t>& d, const EnemyTable& t, std::string* why) {
    EnemyTable now;
    if (!parse_enemy_table(d, &now, why)) return false;
    if (now.kinds.size() != t.kinds.size() || now.groups.size() != t.groups.size()) return fail_with(why, "the enemy table's counts changed");
    const std::size_t at_rows = le32(d, 4);
    for (std::size_t r = 0; r < t.kinds.size(); ++r) std::memcpy(&d[at_rows + 16 * r], t.kinds[r].raw.data(), 16);
    for (std::size_t g = 0; g < t.groups.size(); ++g) {
        const std::size_t at = le32(d, 8 + 4 * g);
        const auto& group = t.groups[g];
        if (group.parts.size() != now.groups[g].parts.size()) return fail_with(why, "an enemy group's parts changed in number");
        d[at + 1] = group.state;
        // +2: the parts' limits summed, or -1 (not read by the code): kept so
        if (now.groups[g].total != 0xffff) {
            unsigned sum = 0;
            for (const auto& p : group.parts) sum += p.limit;
            put16(d, at + 2, sum);
        }
        for (std::size_t p = 0; p < group.parts.size(); ++p) {
            const std::size_t o = at + 4 + 4 * p;
            d[o] = group.parts[p].row;
            d[o + 1] = group.parts[p].limit;
            d[o + 2] = group.parts[p].count;
            d[o + 3] = group.parts[p].last_player;
        }
    }
    return true;
}

bool write_placed_set_over(std::vector<std::uint8_t>& file, const PlacedSet& set, std::string* why) {
    const std::size_t o = set.at;
    if (o + 4 > file.size() || static_cast<int>(file[o] | file[o + 1] << 8) != set.mission) return fail_with(why, "the set is not where it was read");
    if (static_cast<std::int8_t>(file[o + 2]) != static_cast<int>(set.rows.size())) return fail_with(why, "the set's rows changed in number");
    if (o + 4 + 0x14 * set.rows.size() > file.size()) return fail_with(why, "the set's rows past the file");
    for (std::size_t i = 0; i < set.rows.size(); ++i) {
        const std::size_t at = o + 4 + 0x14 * i;
        const PlacedRow& r = set.rows[i];
        file[at] = static_cast<std::uint8_t>(r.b0);
        file[at + 1] = static_cast<std::uint8_t>(r.b1);
        put16(file, at + 2, static_cast<std::uint16_t>(r.h2));
        put16(file, at + 4, static_cast<std::uint16_t>(r.h4));
        put16(file, at + 6, static_cast<std::uint16_t>(r.h6));
        for (std::size_t k = 0; k < 3; ++k) put32(file, at + 8 + 4 * k, static_cast<std::uint32_t>(r.at[k]));
    }
    return true;
}

PlacedSet* placed_set_of(std::vector<PlacedSet>& sets, int mission) {
    for (auto& s : sets) {
        if (s.mission == mission) return &s;
    }
    return nullptr;
}

std::string mission_mod_path(int number) { return "files/" + mission_path(number); }

std::string placed_mod_path(const std::string& name) { return "files/mi/mi/" + name; }

bool save_mission_edit(const fs::path& nitrofs, const fs::path& mod_dir, const MissionEdit& edit, std::vector<std::string>* written,
                       std::string* why) {
    // each file checked as the game reads it before it takes its place
    const auto mission_ok = [&](const std::vector<std::uint8_t>& d) -> std::string {
        MissionFile m;
        std::string problem;
        if (!parse_mission(d, edit.number, &m, &problem)) return "the mission does not read back: " + problem;
        return {};
    };
    const auto sets_ok = [&](const std::vector<std::uint8_t>& d) -> std::string {
        std::vector<PlacedSet> s;
        std::string problem;
        if (!parse_placed_sets(d, &s, &problem)) return "the sets do not read back: " + problem;
        return {};
    };
    const auto game_mission = read_bytes(nitrofs / path_u8(mission_path(edit.number)));
    if (!write_or_drop(mod_dir, mission_mod_path(edit.number), write_mission(edit.mission), game_mission, mission_ok, written, why)) return false;
    if (!edit.trbox.empty() &&
        !write_or_drop(mod_dir, placed_mod_path("trbox"), edit.trbox, read_bytes(nitrofs / "mi" / "mi" / "trbox"), sets_ok, written, why)) {
        return false;
    }
    if (!edit.evi.empty() &&
        !write_or_drop(mod_dir, placed_mod_path("evi"), edit.evi, read_bytes(nitrofs / "mi" / "mi" / "evi"), sets_ok, written, why)) {
        return false;
    }
    return true;
}

bool copy_mission_to(const fs::path& nitrofs, const fs::path& mod_dir, const MissionEdit& from, int target, std::vector<std::string>* written,
                     bool* copied_sets, std::string* why) {
    MissionEdit to;
    if (!load_mission_edit(nitrofs, mod_dir, target, &to, why)) return false;
    to.mission = from.mission;
    to.mission.number = target;
    bool all = true;
    const auto copy_set = [&](const std::vector<PlacedSet>& src_sets, std::vector<PlacedSet>& dst_sets, std::vector<std::uint8_t>& dst_file) {
        const PlacedSet* src = placed_set(src_sets, from.number);
        PlacedSet* dst = placed_set_of(dst_sets, target);
        if (src == nullptr && dst == nullptr) return true;
        if (src == nullptr || dst == nullptr || src->rows.size() != dst->rows.size()) return false;
        PlacedSet moved = *dst;
        moved.rows = src->rows;
        if (!write_placed_set_over(dst_file, moved, nullptr)) return false;
        dst->rows = src->rows;
        return true;
    };
    all = copy_set(from.trbox_sets, to.trbox_sets, to.trbox) && all;
    all = copy_set(from.evi_sets, to.evi_sets, to.evi) && all;
    if (copied_sets != nullptr) *copied_sets = all;
    return save_mission_edit(nitrofs, mod_dir, to, written, why);
}

bool give_back_mission(const fs::path& nitrofs, const fs::path& mod_dir, int number, std::string* why) {
    std::error_code ec;
    const fs::path mine = mod_dir / path_u8(mission_mod_path(number));
    if (fs::is_regular_file(mine, ec) && !remove_mod_file(mod_dir, mine, why)) return false;
    // this mission's chests and emblems the game's again in the mod's trbox / evi
    for (const std::string name : {"trbox", "evi"}) {
        const fs::path copy = mod_dir / path_u8(placed_mod_path(name));
        if (!fs::is_regular_file(copy, ec)) continue;
        auto bytes = read_bytes(copy);
        const auto game = read_bytes(nitrofs / "mi" / "mi" / path_u8(name));
        std::vector<PlacedSet> mod_sets, game_sets;
        if (!parse_placed_sets(bytes, &mod_sets, why) || !parse_placed_sets(game, &game_sets, why)) return false;
        PlacedSet* m = placed_set_of(mod_sets, number);
        PlacedSet* g = placed_set_of(game_sets, number);
        if (m != nullptr && g != nullptr && m->rows.size() == g->rows.size()) {
            PlacedSet back = *g;
            back.at = m->at;
            if (!write_placed_set_over(bytes, back, why)) return false;
        }
        const auto ok = [](const std::vector<std::uint8_t>& d) -> std::string {
            std::vector<PlacedSet> s;
            return parse_placed_sets(d, &s, nullptr) ? std::string() : std::string("the sets do not read back");
        };
        if (!write_or_drop(mod_dir, placed_mod_path(name), bytes, game, ok, nullptr, why)) return false;
    }
    return true;
}

}  // namespace studio
