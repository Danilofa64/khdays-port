// A mission changed in place -- operands, texts, enemy groups, chests and emblems -- and written into
// the mod as the game reads it. Nothing moves, so every offset stays; a text that grows goes past the
// script's data (docs/STUDIO.md, "Missions").
#pragma once

#include "core/missions/mission_db.h"
#include "core/missions/missions.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct MissionEdit {
    int number = 0;
    MissionFile mission;
    std::vector<std::uint8_t> trbox, evi;  // the whole files
    std::vector<PlacedSet> trbox_sets, evi_sets;
    bool mission_from_mod = false, trbox_from_mod = false, evi_from_mod = false;
};

// The mission as the mod has it (its own copies, where it holds them) or as
// the game has it; `mod_dir` empty: the game's.
bool load_mission_edit(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, int number,
                       MissionEdit* out, std::string* why);

// A step's operand `slot` (its words 2*slot, 2*slot+1) made `o`.
void set_slot(ScriptStep& step, std::size_t slot, const OperandSlot& o);
// A point's coordinate written as a 20.12 number (mode 0x10: ScriptVm_ReadOperandFx32 reads it so).
void set_fx(ScriptStep& step, std::size_t slot, std::int32_t fx);
// An integer operand (mode 1).
void set_int(ScriptStep& step, std::size_t slot, std::int32_t value);

// The text a step's operand points at (mode 2: one text; 0x40: a table of a
// text per language, GetLanguage's 1-5) made `utf8`: written at the end of
// the script's data and pointed at -- the operand (mode 2) or the language's
// word of the table (0x40). False when the operand is neither.
bool set_step_text(Script& script, ScriptStep& step, std::size_t slot, int language, const std::string& utf8);

// The enemy table's rows and groups written over its bytes (same counts:
// parse_enemy_table's layout).
bool write_enemy_table_over(std::vector<std::uint8_t>& d, const EnemyTable& t, std::string* why);

// A mission's set written over its rows in trbox / evi (same row count).
bool write_placed_set_over(std::vector<std::uint8_t>& file, const PlacedSet& set, std::string* why);
// The mission's set in a file's sets, or null.
PlacedSet* placed_set_of(std::vector<PlacedSet>& sets, int mission);

// The files a change writes, relative to the mod.
std::string mission_mod_path(int number);              // files/mi/mi/0101
std::string placed_mod_path(const std::string& name);  // files/mi/mi/trbox

// Writes the mission, and trbox / evi when they differ from the game's (else
// the mod's copy goes: the game's is the same), each read back as the game
// reads it before it takes its place; `written` the files written or removed.
bool save_mission_edit(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir,
                       const MissionEdit& edit, std::vector<std::string>* written, std::string* why);

// A mission made from another: `from`'s content written as mission `target`, whose list place, name,
// unlock and saved progress stay the target's; its chests and emblems copied when the target's sets
// have as many rows (`copied_sets`).
bool copy_mission_to(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir,
                     const MissionEdit& from, int target, std::vector<std::string>* written, bool* copied_sets,
                     std::string* why);

// The mission given back to the game: the mod's mission file removed, and its
// sets in the mod's trbox / evi made the game's again (the file removed when
// it is then the game's).
bool give_back_mission(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, int number,
                       std::string* why);

}  // namespace studio
