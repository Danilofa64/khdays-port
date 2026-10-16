// One of the game's models written out as glTF to make a replacement from:
// its skeleton, meshes and textures as the port's graphics dump
// writes them, and, when asked, the animations made for its skeleton -- to
// open in Blender with the game's motion, change one and bring it back
// ("Replace this animation...", core/models/clip_import.h).
#pragma once

#include "core/models/game_model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

// The name the export gives each of `g.animations`: the game's name, a name
// already given followed by _2, _3...; empty for the animations made for
// another skeleton (GameAnimation::fits false), which are not exported.
std::vector<std::string> export_animation_names(const GameModel& g);

// That name for `a`, one of `g.animations` (the same file, place and index).
std::string export_animation_name(const GameModel& g, const GameAnimation& a);

// How many of `g.animations` the export writes.
int exported_animation_count(const GameModel& g);

struct ModelExport {
    int files = 0;       // the glTF and the textures beside it
    int animations = 0;  // the animations in it
};

// `g` to `out` (.gltf), its textures beside it (the dump's own glTF's pictures, else its first picture
// of each); with `animations` those made for its skeleton, 30 frames a second. False with the reason.
bool export_game_model(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                       const GameModel& g, bool animations, const std::filesystem::path& out, ModelExport* result,
                       std::string* why);

}  // namespace studio
