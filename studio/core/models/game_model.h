// One of the game's models as the port pairs a mod's model with it
// (port/runtime/mods/models.cpp load(): its nodes' names, parents and rest
// matrices) and the animations beside it.
#pragma once

#include "khdays/assets/animation.h"
#include "khdays/assets/mesh.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace studio {

using Mat = std::array<float, 16>;

// A game file's pieces of data, as the port's graphics dump walks them
// (port/runtime/mods/names.cpp each_data): each entry of a "P2" archive, labelled
// `<path>#<entry>` and LZ-decompressed when its flag says so, or the whole
// file, LZ-decompressed when its path ends in ".z".
struct GameData {
    std::string label;
    std::vector<std::uint8_t> bytes;
};
std::vector<GameData> game_file_data(const std::filesystem::path& nitrofs, const std::string& game_path);

// The data labelled `label` (`<path>` or `<path>#<entry>`); empty when not found.
std::vector<std::uint8_t> game_label_data(const std::filesystem::path& nitrofs, const std::string& label);

struct GameAnimation {
    std::string label;      // the data holding the BCA0 (`ba/ch/ro/def.p.z`)
    std::size_t offset = 0; // the BCA0 in that data
    std::size_t size = 0;
    std::size_t index = 0;  // the animation in the BCA0
    std::string name;
    int frames = 0;
    bool fits = true;       // made for this skeleton (fits_skeleton), not another with as many bones
};

struct GameModel {
    std::string label;                      // the data holding its BMD0
    std::string name;                       // the model's name
    khdays::assets::NeutralModel model;
    std::vector<Mat> rest;                  // W_G per node (models.cpp lines 246-248)
    std::vector<int> parents;               // per node, -1 a root
    std::map<std::string, std::filesystem::path> textures;  // texture name -> its dumped PNG
    std::vector<GameAnimation> animations;  // the animations with as many nodes: those that fit first
};

// Whether an animation was made for this model's skeleton: at its first
// frame its bones keep the model's lengths (most of them, within a tenth).
// Another model's animation with as many bones moves them by its own
// lengths and pulls the mesh apart.
bool fits_skeleton(const khdays::assets::NeutralModel& model, const khdays::assets::SkeletalAnimation& animation);

// The model the dump names `<label>/mdl/<name>` (item.key), from the
// extracted game files; its textures from the graphics dump; the animations
// of its folder (and of mi/mo, where the missions' scripts load the motions
// of the models of mi/ch by name: Ov023_ActorStartMotion) with as many nodes.
// False with the reason.
bool load_game_model(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                     const std::string& key, GameModel* out, std::string* why);

// An animation's data, decoded by the port's reader (assets/src/animation.cpp).
bool load_game_animation(const std::filesystem::path& nitrofs, const GameAnimation& a,
                         khdays::assets::SkeletalAnimation* out, std::string* why);

// The model's node matrices at `frame` of `animation` (rest when null), in the
// space of `rest`: what the port pairs a mod's joints with (models.cpp draw():
// D_G = M_G * inverse(W_G)). Also the game's own palette, to draw its mesh.
void pose_game_model(const GameModel& g, const khdays::assets::SkeletalAnimation* animation, float frame,
                     std::vector<Mat>* node_world, std::vector<Mat>* palette);

}  // namespace studio
