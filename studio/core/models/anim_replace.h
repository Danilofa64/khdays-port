// One of the game's animations replaced in the game file that holds it, everything else wrapped again
// as it was, the result in mods/<Mod>/files/<game path> (docs/STUDIO.md, "Models and animations").
#pragma once

#include "core/models/bca0.h"
#include "core/models/game_model.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace studio {

// The game's path of a label (`ba/ch/ro/ab.p2#3` -> `ba/ch/ro/ab.p2`).
std::string label_path(const std::string& label);

// The original animation's local matrices at each of its frames.
AnimationPose animation_pose(const GameModel& g, const khdays::assets::SkeletalAnimation& a);

// `file` (the game file, or a mod's copy) with animation `index` of a BCA0 replaced by `pose`, the
// nodes `keep` marks keeping the original's curves. The BCA0 is found by its block's order and the
// kept curves come from the game's own, so a second replacement starts from the original. `new_offset`:
// the BCA0 in the result. False with the reason.
bool rebuild_with_animation(const std::vector<std::uint8_t>& file, const std::vector<std::uint8_t>& original,
                            const std::string& label, std::size_t offset, std::size_t index, const AnimationPose& pose,
                            const std::vector<bool>& keep, std::vector<std::uint8_t>* out, std::size_t* new_offset,
                            std::string* why);

// The animation as the port reads it back from such a file.
bool read_animation(const std::vector<std::uint8_t>& file, const std::string& label, std::size_t offset,
                    std::size_t index, khdays::assets::SkeletalAnimation* out, std::string* why);

// The largest difference between `pose` and `a` sampled at each frame, each
// value measured against the length of its column (at least 1): the
// fixed-point rounding is about 0.0003.
float pose_difference(const AnimationPose& pose, const khdays::assets::SkeletalAnimation& a);

// The game model's nodes whose matrix is not the plain product the dump's
// glTF assumes (its local under its parent): a clip made on the dump's
// skeleton lands on them only approximately.
std::vector<std::string> nodes_not_plain(const GameModel& g);

struct AnimationReplacement {
    std::filesystem::path written;        // the mod's file
    std::string clip_name;
    std::vector<std::string> animated;    // the game's nodes the clip moves
    std::vector<std::string> left_alone;  // keeping the original's motion
    std::vector<std::string> not_plain;   // see nodes_not_plain
    bool on_mod_copy = false;             // built on the mod's own copy of the file (other changes kept)
    float difference = 0;                 // read back against the clip (pose_difference)
};

// Animation `a` of model `g` replaced by the glTF clip `clip` in
// `mods/<Mod>/files/<game path>` (`mod_dir` the mod's folder): built on the
// mod's copy of that file when it has one, else on the game's; written
// safely and read back. False with the reason.
bool replace_animation_in_mod(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir,
                              const GameModel& g, const GameAnimation& a, const std::filesystem::path& clip,
                              AnimationReplacement* out, std::string* why);

// Every animation of the game rebuilt and read back (the format's check): every node kept, every node
// replaced by its own motion, and half and half. `line` gets one line per problem and the totals; true
// when there was none.
bool check_all_animations(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line);

}  // namespace studio
