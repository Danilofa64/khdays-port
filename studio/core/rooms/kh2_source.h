// KINGDOM HEARTS II FINAL MIX's rooms from the player's KINGDOM HEARTS HD 1.5+2.5 ReMIX copy, made into
// glTF models to replace Days's rooms with: a map read from a folder holding it as the collection's
// archives give it (original/map/<map>.map and remastered/map/<map>.map/-N.dds), its background,
// skies and moving pieces, its HD pictures as PNG beside the model. What is made holds Square Enix's
// work: it stays the player's own.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct Kh2Made {
    std::size_t triangles = 0;
    std::size_t pictures = 0;
    std::size_t pieces = 0;           // moving or still pieces placed
    std::size_t animations = 0;       // of them, moving
    std::size_t scrolling = 0;        // materials whose texture scrolls
    std::size_t framed = 0;           // materials whose texture changes picture
    std::vector<std::string> report;  // what was left out or is not played
};

// Where maps taken out of the collection are kept: saves/collection/kh2 under the port's folder.
std::filesystem::path kh2_folder(const std::filesystem::path& root);

// `out` (a .gltf) and its pictures made from map `map` (tt02) in `folder`; false with the reason.
bool make_kh2_room(const std::filesystem::path& folder, const std::string& map, const std::filesystem::path& out,
                   Kh2Made* made, std::string* why);

}  // namespace studio
