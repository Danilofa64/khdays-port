// A room written as one glTF to build a replacement on: the
// models its object list draws, at rest, and its collision in the same space
// -- a mesh per kind of face and set of names, its material named
// `ground`, `wall` or `ceiling` followed by the names (`ground gate03 nohit`).
#pragma once

#include "core/rooms/worlds.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace studio {

// Writes `out` (a .gltf, its buffer embedded) and the models' textures beside
// it; false with the reason. Models the dump does not have are left out and
// named in `notes`.
bool export_room(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump, const World& w, int room,
                 const std::filesystem::path& out, std::vector<std::string>* notes, std::string* why);

// The points of the room's models (as the dump has them, at rest) in the
// room's space: what align_with_room compares a modder's model with.
std::vector<std::array<float, 3>> room_points(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                                              const World& w, int room);

}  // namespace studio
