// A mission changed: the values the studio has read changed in
// place -- a step's operands (a point, a number, a time), its texts, the enemy
// table's groups, the mission's chests and emblems -- and the files written
// into the mod as the game reads them: files/mi/mi/<number> (the mission),
// files/mi/mi/trbox and files/mi/mi/evi (every mission's chests and emblems,
// this one's set changed). Nothing moves: a step keeps its length, a table its
// size, so every offset the game follows stays where it was; a text that grows
// is written past the script's data and its place there pointed at it.
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
bool load_mission_edit(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, int number, MissionEdit* out,
                       std::string* why);

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
std::string mission_mod_path(int number);  // files/mi/mi/0101
std::string placed_mod_path(const std::string& name);  // files/mi/mi/trbox

// Writes the mission, and trbox / evi when they differ from the game's (else
// the mod's copy goes: the game's is the same), each read back as the game
// reads it before it takes its place; `written` the files written or removed.
bool save_mission_edit(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, const MissionEdit& edit,
                       std::vector<std::string>* written, std::string* why);

// A mission made from another: `from`'s content (its file as edited: world,
// rooms, enemies, starts, texts) written into the mod as mission `target`,
// whose place in the Mission Mode list, name there, unlock and saved progress
// stay the target's (they are kept by its number). Its chests and emblems
// are copied into the target's sets when they have as many rows (the sets
// keep their sizes); `copied_sets` says whether they were.
bool copy_mission_to(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, const MissionEdit& from, int target,
                     std::vector<std::string>* written, bool* copied_sets, std::string* why);

// The mission given back to the game: the mod's mission file removed, and its
// sets in the mod's trbox / evi made the game's again (the file removed when
// it is then the game's).
bool give_back_mission(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, int number, std::string* why);

}  // namespace studio
