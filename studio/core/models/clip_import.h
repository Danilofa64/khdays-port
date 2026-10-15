// An animation clip from a glTF or glb file, sampled onto the game's nodes:
// the clip's nodes named like the game's are taken
// at as many evenly spaced times over the clip as the original animation has
// frames, each node's translation, rotation and scale as glTF composes them
// (T * R * S); the game's nodes the clip leaves alone keep the original's
// motion. The clip is meant to be made on the game's own skeleton, the one
// the port's dump writes as glTF (each node's matrix under its parent).
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

// `nodes` the game model's node names; `original` the original animation's
// local matrices per frame and node (its frame count is the clip's). Of a
// file with several animations (a model exported with its animations,
// core/models/model_export.h), the one named `wanted` (the name the export gave the
// animation replaced; case aside), else its first. False with the reason --
// among them a clip that moves none of the game's nodes.
bool load_clip(const std::filesystem::path& file, const std::vector<std::string>& nodes, const AnimationPose& original,
               Clip* out, std::string* why, const std::string& wanted = {});

}  // namespace studio
