// A room of a mission world replaced by the modder's model: the change in mods/<Mod>/studio/rooms/
// (which the port ignores), and what the port reads made from it over the player's copy -- the world's
// archive with new collision, and the room's models as glTF (docs/STUDIO.md, "Rooms"). A room is its
// index in the world's table.
#pragma once

#include "core/rooms/room_collision.h"
#include "core/rooms/worlds.h"

#include "khdays/assets/model_import.h"

#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace studio {

// Where the modder's model sits in the room: scaled, turned about x, then y,
// then z (degrees), then moved (the game's units: 1 = 4096 in 20.12, the
// units --room-model exports in).
struct RoomPlacement {
    float scale = 1.0f;
    std::array<float, 3> turn{0, 0, 0};
    std::array<float, 3> move{0, 0, 0};
};

// What a room's model (one of its objects, by archive member) becomes.
enum class ObjectUse { model, hide, keep };

struct RoomReplacement {
    std::string world;      // its code: tt, aw ...
    int room = -1;          // its index in the world's table
    std::string model;      // the visible model's file, in the change's folder
    std::string collision;  // a separate collision model's, or empty: the visible one
    // the game's own grounds, walls and ceilings kept: only what is seen is
    // replaced (a room remade in the same place needs no collision of its own)
    bool keep_collision = false;
    RoomPlacement place;
    float ground_limit = 0x333 / 4096.0f;  // the unit normal's y above which a face is a ground
    // per object (archive member): unset, the room's first model takes the
    // modder's and the others are hidden
    std::map<int, ObjectUse> objects;
    std::map<std::string, std::vector<std::string>> names_by_material;
    std::map<std::size_t, std::vector<std::string>> names_by_triangle;
    // the original's gates and walls that open not kept where the game had them
    std::set<std::string> drop_original;
    // placement markers moved (by name: x, y, z in 20.12, the facing word)
    std::map<std::string, std::array<std::int32_t, 4>> markers;
    // gate models moved (by link key and model 0-4: their position x, y, z in 20.12)
    std::map<std::pair<int, int>, std::array<std::int32_t, 3>> gate_points;
};

std::filesystem::path rooms_folder(const std::filesystem::path& mod_dir, const std::string& code);
std::filesystem::path change_file(const std::filesystem::path& mod_dir, const std::string& code, int room);
std::filesystem::path change_models(const std::filesystem::path& mod_dir, const std::string& code, int room);

bool read_change(const std::filesystem::path& file, RoomReplacement* out, std::string* why);
std::string change_text(const RoomReplacement& r);

// The mod's replaced rooms of world `code` (by their files), and the worlds it changes.
std::vector<RoomReplacement> changes_of(const std::filesystem::path& mod_dir, const std::string& code,
                                        std::vector<std::string>* problems);
std::vector<std::string> changed_worlds(const std::filesystem::path& mod_dir);

// A model (read as the port reads it) with every vertex posed as the file
// places it and then placed; its bones, if any, left (a room's model stands still).
void place_model(khdays::assets::ImportedModel* model, const RoomPlacement& place);

// Where a model made from the game's room lines up with it again: the quarter turns and scale under
// which most of its points fall on the room's own (`share`, 0.25 or more: lined up; the move left at
// 0). A model that shares nothing with the room keeps its placement.
struct RoomAlignment {
    RoomPlacement place;
    double share = 0;  // under `place`
    double as_is = 0;  // with nothing changed
    // what to do: `turned` -- `place` puts it on the room (at least 5% of its
    // points, twice as many as it has as it is); `in_place` -- it lines up as
    // it is (3% or more: a room remade in the same place keeps some of the
    // game's points); `unrelated` -- it shares nothing worth saying
    enum Verdict { turned, in_place, unrelated } verdict = unrelated;
};
RoomAlignment align_with_room(const std::vector<std::array<float, 3>>& room_points,
                              const khdays::assets::ImportedModel& model);
bool placed_model(const std::filesystem::path& model, const RoomPlacement& place, khdays::assets::ImportedModel* out,
                  std::string* why);

// The model's triangles, placed, as the collision builder takes them: a two-sided mesh's steep faces
// doubled the other way; with `visible`, a translucent material's triangles passable.
CollisionSource collision_source(const khdays::assets::ImportedModel& placed, bool visible = false);

// The world with the change's gate model points moved.
void apply_gate_points(World* w, const RoomReplacement& r);

// The use each of the room's objects gets (by member, in the room's order).
std::vector<std::pair<int, ObjectUse>> object_uses(const WorldRoom& room, const RoomReplacement& r);

// The models the graphics dump names in a world's archive member
// (`mi/wd/wd_<code>#<member>/mdl/<name>`).
std::vector<std::string> member_models(const std::filesystem::path& graphics_dump, const std::string& code, int member);

// The rooms other than `room` that draw member `member` (models) or use it
// as their collision.
std::vector<int> rooms_sharing(const World& w, int room, int member, bool collision);

// One replaced room's collision, made as it will be written.
struct BuiltRoom {
    RoomCollision collision;
    CollisionReport report;
    std::vector<RoomWarning> warnings;  // the report's and the sharing ones
};
bool build_room(const World& w, const std::filesystem::path& mod_dir, const RoomReplacement& r, BuiltRoom* out,
                std::string* why);

// The rooms that change with this one (sharing its collision or a model it
// does not keep), as warnings.
std::vector<RoomWarning> sharing_warnings(const World& w, const RoomReplacement& r);

struct WorldOutcome {
    std::vector<std::string> lines;  // per room: counts and each warning
    bool removed = false;            // no room of the world replaced any more: its outputs removed
};

// Writes world `code`'s outputs from the mod's changes:
// the archive (checked before it replaces anything) and the rooms' models;
// with no change left, removes them. False with the reason; what was there stays.
bool write_world_outputs(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                         const std::filesystem::path& mod_dir, const std::string& code, WorldOutcome* out,
                         std::string* why);

// Saves a change: the model files (and their companions) copied beside it,
// the change written, then the world's outputs.
bool save_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                 const std::filesystem::path& mod_dir, RoomReplacement r, const std::filesystem::path& model_source,
                 const std::filesystem::path& collision_source_file, WorldOutcome* out, std::string* why);

// Removes a room's change and the models it wrote, then writes its world again.
bool revert_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                   const std::filesystem::path& mod_dir, const std::string& code, int room, WorldOutcome* out,
                   std::string* why);

}  // namespace studio
