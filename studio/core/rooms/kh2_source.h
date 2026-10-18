// KINGDOM HEARTS II FINAL MIX's rooms from the player's KINGDOM HEARTS HD 1.5+2.5 ReMIX copy, made into
// glTF models to replace Days's rooms with: a map taken out of the collection's archives by the
// installer's own reader (installer/collection_archive.py) into saves/collection/kh2 as
// original/map/<map>.map and remastered/map/<map>.map/-N.dds, then its background, skies and moving
// pieces made into a model, its HD pictures as PNG beside it. What is made holds Square Enix's work:
// it stays the player's own.
#pragma once

#include "core/mods/settings.h"

#include "khdays/assets/texture.h"

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

// The player's KINGDOM HEARTS HD 1.5+2.5 ReMIX folder: the studio's setting, else the one the install
// made its HD pieces from (setup/state.json); empty when neither names a folder that is there.
std::filesystem::path kh2_collection(const Settings& settings);

// The collection's archives that hold KH2's maps, in the order they are asked: kh2_first, kh2_fifth,
// kh2_sixth, in its language folder (Image/dt: Steam's, Image/en: the Epic Games Store's).
std::vector<std::filesystem::path> kh2_archives(const std::filesystem::path& collection);

// The maps of KH2's world `world` (tt) the copy holds (<world>00 to <world>99), in order; false with
// the reason (no collection, no archive, the reader not run).
bool kh2_world_maps(const Settings& settings, const std::string& world, std::vector<std::string>* maps,
                    std::string* why);

// Map `map` (tt02) taken out of the copy into kh2_folder() unless it is there already; false with the
// reason: no collection set, its executable or archives missing, no archive holding the map.
bool take_out_kh2_map(const Settings& settings, const std::string& map, std::string* why);

// Whether `map` is in kh2_folder() already.
bool kh2_map_taken_out(const std::filesystem::path& root, const std::string& map);

// The map's radar picture (its room seen from above), once it is taken out; false when it has none.
bool kh2_radar(const std::filesystem::path& root, const std::string& map, khdays::assets::DecodedTexture* out);

// `out` (a .gltf) and its pictures made from map `map` (tt02) in `folder`; false with the reason.
bool make_kh2_room(const std::filesystem::path& folder, const std::string& map, const std::filesystem::path& out,
                   Kh2Made* made, std::string* why);

// Where the studio makes a map's model before a room change takes it: kh2_folder()/made/<map>/<map>.gltf.
std::filesystem::path kh2_made_model(const std::filesystem::path& root, const std::string& map);

// The KH2 map a Days room was made from (studio/data/kh2_rooms.txt: `<world> <room> <map> ; <evidence>`).
struct Kh2Pair {
    std::string world;
    int room = -1;
    std::string map;
    std::string evidence;
};

// The pairs of `text` (`source` names it in `notes`, one line for each that cannot be read), added to
// `pairs`: a room already there takes the new line's map.
void read_kh2_pairs(const std::string& text, const std::string& source, std::vector<Kh2Pair>* pairs,
                    std::vector<std::string>* notes);

// The studio's own pairs (built in), then those of the mod `mod_dir` (its studio/kh2_rooms.txt), when given.
std::vector<Kh2Pair> kh2_pairs(const std::filesystem::path& mod_dir, std::vector<std::string>* notes);

// The pair of Days room `room` of world `world`, or null.
const Kh2Pair* kh2_pair_of(const std::vector<Kh2Pair>& pairs, const std::string& world, int room);

}  // namespace studio
