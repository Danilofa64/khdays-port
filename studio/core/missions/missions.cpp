#include "core/missions/missions.h"

#include "core/missions/command_names.h"
#include "core/missions/mission_db.h"
#include "core/files/paths.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace studio {

namespace {

std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | static_cast<std::uint32_t>(d[at + 1]) << 8 |
           static_cast<std::uint32_t>(d[at + 2]) << 16 | static_cast<std::uint32_t>(d[at + 3]) << 24;
}
std::uint32_t le16(const std::vector<std::uint8_t>& d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | static_cast<std::uint32_t>(d[at + 1]) << 8;
}
void add32(std::vector<std::uint8_t>& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The names of section 0's member 0.
bool parse_names(const std::vector<std::uint8_t>& d, std::vector<std::string>* out, std::string* why) {
    if (d.size() < 2)
        return fail_with(why, "a name table too short");
    const std::size_t n = le16(d, 0);
    if (2 + 2 * n > d.size())
        return fail_with(why, "a name table past its member");
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t at = le16(d, 2 + 2 * i);
        std::string name;
        while (at < d.size() && d[at] != 0)
            name.push_back(static_cast<char>(d[at++]));
        if (at >= d.size())
            return fail_with(why, "a script name not ended");
        out->push_back(name);
    }
    return true;
}

}  // namespace

OperandSlot operand_at(const ScriptStep& s, std::size_t word) {
    OperandSlot o;
    if (word + 1 >= s.words.size())
        return o;  // a slot is two words
    o.mode = static_cast<std::int16_t>(s.words[word] & 0xffff);
    o.sel = static_cast<std::int16_t>(s.words[word] >> 16);
    o.value = static_cast<std::int32_t>(s.words[word + 1]);
    return o;
}

bool slot_mode(std::int16_t mode) {
    return mode == 0 || mode == 1 || mode == 2 || mode == 4 || mode == 8 || mode == 0x10 || mode == 0x40 ||
           (mode & 0x80) != 0;
}

bool raw_step(const ScriptStep& s) {
    return s.group == 0 && (s.command == 1 || s.command == 5 || s.command == 10);
}

std::string script_string(const Script& s, std::uint32_t offset) {
    std::string out;
    for (std::size_t i = offset; i < s.tail.size(); ++i) {
        const std::uint8_t c = s.tail[i];
        if (c == 0)
            return out;
        if (c < 0x20 || c == 0x7f)
            return {};
        out += static_cast<char>(c);
    }
    return {};  // no end inside the data
}

std::string script_text(const Script& s, std::uint32_t table, int language) {
    const std::size_t at = static_cast<std::size_t>(table) + 4 * static_cast<std::size_t>(language);
    if (language < 0 || at + 4 > s.tail.size())
        return {};
    const std::uint32_t offset = le32(s.tail, at);
    return offset == 0xffffffffu ? std::string() : script_string(s, offset);
}

std::string slot_text(const OperandSlot& o, const Script* script, int language) {
    char buf[64];
    if (o.mode == 0)
        std::snprintf(buf, sizeof(buf), o.value == 0 ? "none" : "none:%d", o.value);
    else if (o.mode == 2)
        std::snprintf(buf, sizeof(buf), "data+0x%x", static_cast<unsigned>(o.value));
    else if (o.mode == 0x40)
        std::snprintf(buf, sizeof(buf), "text(data+0x%x)", static_cast<unsigned>(o.value));
    else if (o.mode == 1)
        std::snprintf(buf, sizeof(buf), "%d", o.value);
    else if (o.mode == 0x10)
        std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(o.value) / 4096.0);
    else if (o.mode == 4)
        std::snprintf(buf, sizeof(buf), "state[%u:%u]", static_cast<unsigned>(o.value) & 0xffff,
                      static_cast<unsigned>(o.value) >> 16);
    else if (o.mode == 8)
        std::snprintf(buf, sizeof(buf), "var[%d]", o.value);
    else
        std::snprintf(buf, sizeof(buf), "table(%04x,%d)[%d]", static_cast<unsigned>(o.mode) & 0xffff, o.sel, o.value);
    std::string out = buf;
    const std::string text = script == nullptr ? std::string()
                             : o.mode == 2     ? script_string(*script, static_cast<std::uint32_t>(o.value))
                             : o.mode == 0x40  ? script_text(*script, static_cast<std::uint32_t>(o.value), language)
                                               : std::string();
    if (!text.empty())
        out += " \"" + text + "\"";
    return out;
}

std::string expression_text(const Script& s, std::uint32_t offset, int language) {
    static const char* const ops[] = {"+", "-", "*", "/", "==", ">", "<", ">=", "<=", "!=", "&&", "||"};
    std::vector<std::string> stack;
    std::size_t at = offset;
    for (;;) {
        if (at + 2 > s.tail.size())
            return {};  // no end inside the data
        const unsigned code = le16(s.tail, at);
        if (code == 0xf)
            break;
        if (code == 0) {
            if (at + 10 > s.tail.size())
                return {};
            OperandSlot o;
            o.mode = static_cast<std::int16_t>(le16(s.tail, at + 2));
            o.sel = static_cast<std::int16_t>(le16(s.tail, at + 4));
            o.value = static_cast<std::int32_t>(le32(s.tail, at + 6));
            if (!slot_mode(o.mode))
                return {};
            stack.push_back(slot_text(o, &s, language));
            at += 10;
            continue;
        }
        if (code == 1 || code == 2) {
            if (stack.empty())
                return {};
            stack.back() = (code == 1 ? "-" : "!") + stack.back();
        } else if (code >= 3 && code <= 14) {
            if (stack.size() < 2)
                return {};
            const std::string right = stack.back();
            stack.pop_back();
            stack.back() = "(" + stack.back() + " " + ops[code - 3] + " " + right + ")";
        } else {
            return {};  // a code Script_EvalExpr does not have
        }
        at += 2;
    }
    if (stack.size() != 1)
        return {};
    std::string out = stack.front();
    if (out.size() >= 2 && out.front() == '(' && out.back() == ')')
        out = out.substr(1, out.size() - 2);
    return out;
}

std::string operands_text(const ScriptStep& s, const Script* script, int language) {
    std::string out;
    char buf[64];
    if (raw_step(s)) {
        for (std::size_t w = 0; w < s.words.size(); ++w) {
            if (!out.empty())
                out += ", ";
            const bool target = s.command != 5 || w == 1;
            if (s.command == 5 && w == 0)
                std::snprintf(buf, sizeof(buf), "data+0x%x", s.words[w]);
            else if (target && s.words[w] != 0xffffffffu)
                std::snprintf(buf, sizeof(buf), "@%05x", s.words[w] + 4);
            else
                std::snprintf(buf, sizeof(buf), "%d", static_cast<std::int32_t>(s.words[w]));
            out += buf;
        }
        return out;
    }
    std::size_t w = 0;
    while (w < s.words.size()) {
        if (!out.empty())
            out += ", ";
        const OperandSlot o = operand_at(s, w);
        if (w + 1 < s.words.size() && slot_mode(o.mode)) {
            out += slot_text(o, script, language);
            w += 2;
        } else {
            std::snprintf(buf, sizeof(buf), "0x%08x", s.words[w]);
            out += buf;
            w += 1;
        }
    }
    return out;
}

bool parse_script(const std::vector<std::uint8_t>& block, Script* out, std::string* why) {
    if (block.size() < 4)
        return fail_with(why, "a script shorter than its size word");
    const std::size_t size = le32(block, 0);
    if (size < 4 || size > block.size())
        return fail_with(why, "a script's size past its block");
    Script s;
    std::size_t at = 4;
    while (at < size) {
        if (at + 4 > size)
            return fail_with(why, "a step's header past the script");
        ScriptStep step;
        step.group = block[at];
        step.command = block[at + 1];
        const std::uint32_t lw = le16(block, at + 2);
        const std::size_t words = lw & 0x7ff;
        step.field = static_cast<std::uint8_t>(lw >> 11);
        if (words == 0)
            return fail_with(why, "a step of no length");
        if (at + 4 * words > size)
            return fail_with(why, "a step past the script's end");
        for (std::size_t k = 1; k < words; ++k)
            step.words.push_back(le32(block, at + 4 * k));
        s.steps.push_back(std::move(step));
        at += 4 * words;
    }
    s.tail.assign(block.begin() + static_cast<std::ptrdiff_t>(size), block.end());
    s.name = out->name;
    *out = std::move(s);
    return true;
}

std::vector<std::uint8_t> write_script(const Script& s) {
    std::vector<std::uint8_t> d;
    add32(d, 0);
    for (const auto& step : s.steps) {
        d.push_back(step.group);
        d.push_back(step.command);
        const std::uint32_t lw =
            static_cast<std::uint32_t>(step.length() & 0x7ff) | static_cast<std::uint32_t>(step.field) << 11;
        d.push_back(static_cast<std::uint8_t>(lw & 0xff));
        d.push_back(static_cast<std::uint8_t>(lw >> 8));
        for (const auto w : step.words)
            add32(d, w);
    }
    const std::uint32_t size = static_cast<std::uint32_t>(d.size());
    for (int i = 0; i < 4; ++i)
        d[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((size >> (8 * i)) & 0xff);
    d.insert(d.end(), s.tail.begin(), s.tail.end());
    return d;
}

bool parse_script_archive(const std::vector<std::uint8_t>& data, ScriptArchive* out, std::string* why) {
    ScriptArchive a;
    if (!parse_cakp(data, &a.pack, why))
        return false;
    std::vector<std::size_t> scripts;
    for (std::size_t b = 0; b < a.pack.blocks.size(); ++b) {
        const KaphBlock& block = a.pack.blocks[b];
        if (block.section == 0 && block.index == 0 && !parse_names(block.data, &a.names, why))
            return false;
        if (block.section == 1)
            scripts.push_back(b);
    }
    // section 1 in its own order (the blocks are kept in the order of their offsets)
    std::sort(scripts.begin(), scripts.end(),
              [&](std::size_t x, std::size_t y) { return a.pack.blocks[x].index < a.pack.blocks[y].index; });
    for (const std::size_t b : scripts) {
        Script s;
        const std::size_t i = a.pack.blocks[b].index;
        s.name = i < a.names.size() ? a.names[i] : std::string();
        if (!parse_script(a.pack.blocks[b].data, &s, why)) {
            if (why != nullptr)
                *why = "script " + std::to_string(i) + ": " + *why;
            return false;
        }
        a.scripts.push_back(std::move(s));
        a.script_block.push_back(b);
    }
    *out = std::move(a);
    return true;
}

std::vector<std::uint8_t> write_script_archive(const ScriptArchive& a) {
    KaphFile k = a.pack;
    for (std::size_t i = 0; i < a.scripts.size() && i < a.script_block.size(); ++i)
        k.blocks[a.script_block[i]].data = write_script(a.scripts[i]);
    return write_kaph(k);
}

int MissionFile::entry(const std::string& name) const {
    for (std::size_t e = 0; e < entry_names.size(); ++e) {
        if (entry_names[e].compare(0, name.size(), name) == 0)
            return static_cast<int>(e);
    }
    return -1;
}

bool parse_enemy_table(const std::vector<std::uint8_t>& d, EnemyTable* out, std::string* why) {
    if (d.size() < 8)
        return fail_with(why, "an enemy table shorter than its header");
    const std::size_t rows = d[0], groups = d[1], at_rows = le32(d, 4);
    if (8 + 4 * groups > d.size() || at_rows + 16 * rows > d.size())
        return fail_with(why, "an enemy table past its end");
    EnemyTable t;
    for (std::size_t r = 0; r < rows; ++r) {
        EnemyKind k;
        std::copy(d.begin() + static_cast<std::ptrdiff_t>(at_rows + 16 * r),
                  d.begin() + static_cast<std::ptrdiff_t>(at_rows + 16 * r + 16), k.raw.begin());
        t.kinds.push_back(k);
    }
    for (std::size_t g = 0; g < groups; ++g) {
        const std::size_t at = le32(d, 8 + 4 * g);
        if (at + 4 > d.size())
            return fail_with(why, "an enemy group past its table");
        EnemyGroup group;
        const std::size_t parts = d[at];
        group.state = d[at + 1];
        group.total = static_cast<std::uint16_t>(le16(d, at + 2));
        if (at + 4 + 4 * parts > d.size())
            return fail_with(why, "an enemy group's parts past its table");
        for (std::size_t p = 0; p < parts; ++p) {
            const std::size_t o = at + 4 + 4 * p;
            group.parts.push_back({d[o], d[o + 1], d[o + 2], d[o + 3]});
        }
        t.groups.push_back(std::move(group));
    }
    *out = std::move(t);
    return true;
}

bool parse_mission(const std::vector<std::uint8_t>& file, int number, MissionFile* out, std::string* why) {
    MissionFile m;
    m.number = number;
    if (!parse_p2(file, &m.p2, why))
        return false;
    // the entries' names: after the 16-byte header, the descriptors and the start sectors
    {
        const std::size_t n = m.p2.entries.size();
        const std::size_t at = 0x10 + 4 * n + 4 * ((n + 1) / 2);
        for (std::size_t e = 0; e < n; ++e) {
            std::string name;
            for (std::size_t k = 0; k < 8 && at + 8 * e + k < m.p2.head.size(); ++k) {
                const char ch = static_cast<char>(m.p2.head[at + 8 * e + k]);
                if (ch == 0)
                    break;
                name.push_back(ch);
            }
            m.entry_names.push_back(name);
        }
    }
    for (std::size_t e = 0; e < m.p2.entries.size(); ++e) {
        const auto& data = m.p2.entries[e].data;
        MissionEntry entry;
        if (data.size() >= 4 && data[0] == 'C' && data[1] == 'A' && data[2] == 'K' && data[3] == 'P') {
            entry.kind = MissionEntry::script_archive;
            if (!parse_script_archive(data, &entry.archive, why)) {
                if (why != nullptr)
                    *why = "entry " + std::to_string(e) + ": " + *why;
                return false;
            }
        } else {
            entry.kind = data.empty() ? MissionEntry::empty : MissionEntry::table;
        }
        m.entries.push_back(std::move(entry));
    }
    *out = std::move(m);
    return true;
}

std::vector<std::uint8_t> write_mission(const MissionFile& m) {
    P2File p2 = m.p2;
    for (std::size_t e = 0; e < m.entries.size() && e < p2.entries.size(); ++e) {
        if (m.entries[e].kind == MissionEntry::script_archive)
            p2.entries[e].data = write_script_archive(m.entries[e].archive);
    }
    return write_p2(p2);
}

std::string mission_path(int number) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "mi/mi/%04d", number);
    return buf;
}

std::vector<int> mission_numbers(const std::filesystem::path& nitrofs) {
    std::vector<int> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(nitrofs / "mi" / "mi", ec)) {
        const std::string name = u8(e.path().filename());
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; }))
            continue;
        out.push_back(std::atoi(name.c_str()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool load_mission(const std::filesystem::path& nitrofs, int number, MissionFile* out, std::string* why) {
    const auto path = nitrofs / path_u8(mission_path(number));
    const auto bytes = read_file(path);
    if (bytes.empty())
        return fail_with(why, "cannot read " + mission_path(number));
    return parse_mission(bytes, number, out, why);
}

bool check_all_missions(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line) {
    int files = 0, problems = 0, enemy_tables = 0, enemy_odd = 0;
    std::size_t archives = 0, scripts = 0, steps = 0, unnamed = 0, expressions = 0, not_slots = 0;
    std::map<std::pair<int, int>, std::size_t> unnamed_commands;
    for (const int number : mission_numbers(nitrofs)) {
        ++files;
        const std::string path = mission_path(number);
        const auto bytes = read_file(nitrofs / path_u8(path));
        MissionFile m;
        std::string why;
        if (!parse_mission(bytes, number, &m, &why)) {
            line(path + ": " + why);
            ++problems;
            continue;
        }
        // written back: every entry the same bytes as the game uses
        P2File back;
        if (!parse_p2(write_mission(m), &back, &why) || back.entries.size() != m.p2.entries.size()) {
            line(path + ": written back, not read back: " + why);
            ++problems;
            continue;
        }
        for (std::size_t e = 0; e < back.entries.size(); ++e) {
            if (back.entries[e].data != m.p2.entries[e].data) {
                line(path + " entry " + std::to_string(e) + ": written back differently");
                ++problems;
            }
        }
        // the enemy tables: read, every group's parts summing to its total
        for (const char* table : {"em0m", "em0"}) {
            int e = -1;
            for (std::size_t k = 0; k < m.entry_names.size(); ++k) {
                if (m.entry_names[k] == table)
                    e = static_cast<int>(k);
            }
            if (e < 0)
                continue;
            EnemyTable t;
            if (!parse_enemy_table(m.p2.entries[static_cast<std::size_t>(e)].data, &t, &why)) {
                line(path + " " + table + ": " + why);
                ++problems;
                continue;
            }
            ++enemy_tables;
            for (const auto& g : t.groups) {
                // the unread +2: the parts' limits summed, or -1 with every limit 0xff
                int sum = 0;
                bool unlimited = g.total == 0xffff;
                for (const auto& part : g.parts) {
                    sum += part.limit;
                    unlimited = unlimited && part.limit == 0xff;
                    if (part.row >= t.kinds.size())
                        ++enemy_odd;
                }
                if (sum != g.total && !unlimited)
                    ++enemy_odd;
            }
        }
        std::size_t file_scripts = 0, file_steps = 0, file_unnamed = 0;
        for (const auto& entry : m.entries) {
            if (entry.kind != MissionEntry::script_archive)
                continue;
            ++archives;
            for (const auto& s : entry.archive.scripts) {
                ++file_scripts;
                for (const auto& step : s.steps) {
                    ++file_steps;
                    if (raw_step(step)) {
                        if (step.command == 5 && !step.words.empty()) {
                            ++expressions;
                            if (expression_text(s, step.words[0]).empty()) {
                                line(path + " " + s.name + ": the expression at data+" + std::to_string(step.words[0]) +
                                     " does not read");
                                ++problems;
                            }
                        }
                    } else {
                        bool slots = step.words.size() % 2 == 0;
                        for (std::size_t w = 0; slots && w < step.words.size(); w += 2)
                            slots = slot_mode(operand_at(step, w).mode);
                        if (!slots)
                            ++not_slots;
                    }
                    const CommandName* n = command_name(step.group, step.command);
                    if (n == nullptr) {
                        ++file_unnamed;
                        ++unnamed_commands[{step.group, step.command}];
                    }
                }
            }
        }
        scripts += file_scripts;
        steps += file_steps;
        unnamed += file_unnamed;
        line(path + ": " + std::to_string(m.entries.size()) + " entries, " + std::to_string(file_scripts) +
             " scripts, " + std::to_string(file_steps) + " steps, " + std::to_string(file_unnamed) + " unnamed");
    }
    // the mission database's files
    for (const auto& name : mission_db_files()) {
        std::string why;
        if (!check_mission_db_file(nitrofs, name, &why)) {
            line("mi/mi/" + name + ": " + why);
            ++problems;
        } else {
            std::vector<PlacedSet> sets;
            std::size_t rows = 0;
            if ((name == "trbox" || name == "evi") && load_placed_sets(nitrofs, name, &sets, &why)) {
                for (const auto& s : sets)
                    rows += s.rows.size();
                line("mi/mi/" + name + ": read and written back the same; " + std::to_string(sets.size()) +
                     " missions' sets, " + std::to_string(rows) + (name == "trbox" ? " chests" : " emblems"));
            } else {
                line("mi/mi/" + name + ": read and written back the same");
            }
        }
    }
    std::string missing;
    for (const auto& [gc, n] : unnamed_commands) {
        missing += (missing.empty() ? "" : ", ") + std::to_string(gc.first) + "." + std::to_string(gc.second) + " x" +
                   std::to_string(n);
    }
    if (!missing.empty())
        line("steps no table names (group.command): " + missing);
    line(std::to_string(expressions) + " branch expressions read (Script_EvalExpr); " + std::to_string(not_slots) +
         " steps whose words do not read as slots, besides the three commands that read words");
    line(
        std::to_string(enemy_tables) + " enemy tables read; " + std::to_string(enemy_odd) +
        " groups whose limits neither sum to their +2 nor are all 0xff with -1 there, or name a row the table does not have");
    line(std::to_string(files) + " mission files, " + std::to_string(archives) + " script archives, " +
         std::to_string(scripts) + " scripts, " + std::to_string(steps) + " steps (" + std::to_string(unnamed) +
         " unnamed); " + std::to_string(problems) + " problems");
    return problems == 0;
}

}  // namespace studio
