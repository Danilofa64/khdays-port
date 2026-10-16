// An animation clip from a glTF or glb file, sampled onto the game's nodes (those named alike) at the
// original's frame count, T * R * S as glTF composes them; the nodes it leaves alone keep the original's
// motion. Made on the game's own skeleton, as the dump writes it (docs/STUDIO.md).
#pragma once

#include "core/models/bca0.h"

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct Clip {
    AnimationPose pose;                   // [frame][node], the game's nodes
    std::vector<std::string> animated;    // the game's nodes the clip moves
    std::vector<std::string> left_alone;  // and those it does not (the original's motion)
    std::vector<bool> moved;              // per game node: the clip moves it
    std::string name;                     // the clip's animation's name
    std::size_t animations_in_file = 0;   // how many the file has
};

// `nodes` the game model's node names; `original` the original's local matrices per frame and node. Of
// a file with several animations, the one named `wanted` (case aside), else its first. False with the
// reason, among them a clip that moves none of the game's nodes.
bool load_clip(const std::filesystem::path& file, const std::vector<std::string>& nodes, const AnimationPose& original,
               Clip* out, std::string* why, const std::string& wanted = {});

}  // namespace studio
