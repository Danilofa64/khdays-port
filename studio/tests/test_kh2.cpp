// KH2's rooms in a room change: the change's `kh2` line read back, the first place of a KH2 room that
// shares no points with the room (KH2's units, the floors at one height, the middles together), and the
// markers a made collision leaves inside a wall or with no ground under them, named. Synthetic only.
#include "test.h"

#include "core/rooms/kh2_source.h"
#include "core/rooms/room_change.h"
#include "core/rooms/room_collision.h"
#include "core/rooms/worlds.h"

#include <cmath>
#include <cstring>

using namespace studio;

namespace {

constexpr std::int32_t kUnit = 4096;

RoomFace flat(int x0, int z0, int x1, int z1, int y) {
    RoomFace f;
    f.corners = 4;
    f.v = {{{x0 * kUnit, y * kUnit, z0 * kUnit},
            {x0 * kUnit, y * kUnit, z1 * kUnit},
            {x1 * kUnit, y * kUnit, z1 * kUnit},
            {x1 * kUnit, y * kUnit, z0 * kUnit}}};
    f.bounds = {std::min(x0, x1) * kUnit, std::min(z0, z1) * kUnit, std::max(x0, x1) * kUnit, std::max(z0, z1) * kUnit};
    return f;
}

// A record of the 0x18-byte list: a marker named `name` at (x, y, z) units.
void add_marker(RoomCollision* c, const char* name, double x, double y, double z) {
    const std::size_t at = c->records18.size();
    c->records18.resize(at + 0x18, 0);
    std::memcpy(&c->records18[at], name, std::strlen(name));
    const std::int32_t place[3] = {static_cast<std::int32_t>(x * kUnit), static_cast<std::int32_t>(y * kUnit),
                                   static_cast<std::int32_t>(z * kUnit)};
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 4; ++b)
            c->records18[at + 8 + 4 * a + b] = static_cast<std::uint8_t>((place[a] >> (8 * b)) & 0xff);
    }
}

void quad(CollisionSource* s, std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> c,
          std::array<float, 3> d) {
    s->triangles.push_back({a, b, c});
    s->triangles.push_back({a, c, d});
    s->materials.insert(s->materials.end(), 2, "");
}

}  // namespace

TEST(a_room_change_keeps_its_kh2_map) {
    RoomReplacement r;
    r.world = "tt";
    r.room = 0;
    r.model = "tt02.gltf";
    r.kh2 = "tt02";
    r.place.scale = 0.01f;
    const auto file = studio_test::scratch("kh2_change") / "0.txt";
    studio_test::write(file, change_text(r));
    RoomReplacement back;
    std::string why;
    CHECK(read_change(file, &back, &why));
    CHECK(back.kh2 == "tt02" && back.model == "tt02.gltf" && back.place.scale == 0.01f);
    CHECK(change_text(back) == change_text(r));
}

TEST(the_pairs_are_read_and_a_mods_line_replaces_one) {
    std::vector<Kh2Pair> pairs;
    std::vector<std::string> notes;
    read_kh2_pairs("# a comment\n\ntt 0 tt02 ; model tt_02, textures tt02_*\r\nbb 5 bb04\n", "studio", &pairs, &notes);
    CHECK(notes.empty() && pairs.size() == 2);
    const Kh2Pair* p = kh2_pair_of(pairs, "tt", 0);
    CHECK(p != nullptr && p->map == "tt02" && p->evidence == "model tt_02, textures tt02_*");
    CHECK(kh2_pair_of(pairs, "bb", 5) != nullptr && kh2_pair_of(pairs, "bb", 5)->evidence.empty());
    CHECK(kh2_pair_of(pairs, "tt", 1) == nullptr);
    // a mod's file: one room taken to another map, one added, two lines it cannot read named
    read_kh2_pairs("tt 0 tt09 ; mine\nnm 3 nm06\ntt zero tt02\ntt 1 tt03 extra\n", "mod", &pairs, &notes);
    CHECK(pairs.size() == 3 && kh2_pair_of(pairs, "tt", 0)->map == "tt09");
    CHECK(kh2_pair_of(pairs, "nm", 3) != nullptr);
    CHECK(notes.size() == 2 && notes[0] == "mod line 3: not <world> <room> <map> ; <evidence>" &&
          notes[1].rfind("mod line 4:", 0) == 0);
    // the studio's own: the Usual Spot is Twilight Town's room 0
    notes.clear();
    const auto own = kh2_pairs({}, &notes);
    CHECK(notes.empty() && kh2_pair_of(own, "tt", 0) != nullptr && kh2_pair_of(own, "tt", 0)->map == "tt02");
}

TEST(a_kh2_room_sharing_no_points_is_placed_by_its_units) {
    // the game's room: a ground two units up, from -4 to 4
    RoomCollision room;
    room.faces[kGround] = {flat(-4, -4, 4, 4, 2)};
    // KH2's: a floor 500 KH2 units up, from x 1000 to 3000 and z -1000 to 1000
    khdays::assets::ImportedModel kh2;
    khdays::assets::NeutralMesh mesh;
    for (const auto& p : {std::array<float, 3>{1000, 500, -1000}, std::array<float, 3>{1000, 500, 1000},
                          std::array<float, 3>{3000, 500, 1000}, std::array<float, 3>{3000, 500, -1000}}) {
        khdays::assets::NeutralVertex v;
        v.position = p;
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    kh2.model.meshes = {mesh};
    // a sky far off does not count for the room's middle
    khdays::assets::NeutralMesh sky = mesh;
    sky.sky = true;
    for (auto& v : sky.vertices)
        v.position[0] += 900000;
    kh2.model.meshes.push_back(sky);
    const RoomAlignment a = kh2_alignment({}, room, kh2);
    CHECK(a.verdict == RoomAlignment::kh2_units);
    CHECK(a.place.scale == 0.01f);
    CHECK(std::abs(a.place.move[0] + 20) < 1e-3f && std::abs(a.place.move[1] + 3) < 1e-3f &&
          std::abs(a.place.move[2]) < 1e-3f);
}

TEST(markers_in_a_wall_or_over_nothing_are_named) {
    RoomCollision original;
    original.header.assign(0xb0, 0);
    original.faces[kGround] = {flat(-4, -4, 4, 4, 0)};
    add_marker(&original, "pent01_0", 0, 0, 0);  // on the floor, in the open
    add_marker(&original, "ent_02", 2, 0, 0);    // where the new wall stands
    add_marker(&original, "btl_03", 20, 0, 20);  // off the new floor
    build_tree(&original, 0, 0, 32 * kUnit, 2 * kUnit);
    // the new floor and a wall across x = 2
    CollisionSource source;
    quad(&source, {-4, 0, -4}, {-4, 0, 4}, {4, 0, 4}, {4, 0, -4});
    quad(&source, {2, 0, -4}, {2, 3, -4}, {2, 3, 4}, {2, 0, 4});
    RoomCollision built;
    CollisionReport report;
    std::string why;
    CHECK(build_collision(source, original, CollisionRules{}, &built, &report, &why));
    bool in_wall = false, floating = false, wrong = false;
    for (const auto& w : report.warnings) {
        if (w.kind == RoomWarning::marker_in_wall)
            (w.args[0] == "ent_02" && w.args[1] == "2.00" ? in_wall : wrong) = true;
        if (w.kind == RoomWarning::marker_floating)
            (w.args[0] == "btl_03" ? floating : wrong) = true;
    }
    CHECK(in_wall && floating && !wrong);
}
