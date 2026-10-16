// The port's pairing and pose rules as the tool mirrors them: bones.txt read and written, joints following nodes by name and by
// pairs, the rest pose correction past 20 degrees and not below it, a vertex
// of a corrected arm drawn along the game's arm, proposals by position. Two
// small skeletons made here.
#include "test.h"

#include "core/models/bones.h"
#include "core/files/paths.h"

#include <cmath>
#include <fstream>

using namespace studio;
namespace assets = khdays::assets;

namespace {

Mat translate(float x, float y, float z) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

// The game's skeleton: root at the origin, its arm one unit along +x.
GameModel game_arm_out() {
    GameModel g;
    g.model.object_names = {"root", "arm"};
    g.rest = {translate(0, 0, 0), translate(1, 0, 0)};
    g.parents = {-1, 0};
    return g;
}

// A mod's skeleton: root, then an arm at `x, y` from it; glTF-like (inverse
// bind = inverse of the joint's rest).
assets::ImportedModel mod_arm(const char* root, const char* arm, float x, float y) {
    assets::ImportedModel m;
    assets::ImportedJoint r;
    r.name = root;
    r.palette_index = 0;
    r.world = translate(0, 0, 0);
    r.inverse_bind = mat_inverse(r.world);
    assets::ImportedJoint a;
    a.name = arm;
    a.palette_index = 1;
    a.parent = 0;
    a.world = translate(x, y, 0);
    a.inverse_bind = mat_inverse(a.world);
    m.joints = {r, a};
    return m;
}

}  // namespace

TEST(bones_file_as_the_port_reads_it) {
    const fs::path dir = studio_test::scratch("bones_file");
    studio_test::write(
        dir / "a.bones.txt",
        "# a comment\r\nscale 0.92\r\nBone0 Bip01 extra words\r\ncull back\r\n  \r\nBone2 Bip01_Pelvis # why\r\n");
    BonesFile b = read_bones_file(dir / "a.bones.txt");
    CHECK(near(b.scale, 0.92f));
    CHECK(b.cull == Cull::back);
    CHECK(b.pairs.size() == 2u && b.pairs[0].first == "Bone0" && b.pairs[0].second == "Bip01");
    CHECK(b.pairs[1].second == "Bip01_Pelvis");
    CHECK(b.comments.size() == 1u && b.comments[0] == "# a comment");

    studio_test::write(dir / "bad.bones.txt", "scale -2\n");
    CHECK(near(read_bones_file(dir / "bad.bones.txt").scale, 1.0f));  // k > 0, else 1

    // written and read back the same
    b.cull = Cull::none;
    studio_test::write(dir / "b.bones.txt", bones_text(b));
    const BonesFile again = read_bones_file(dir / "b.bones.txt");
    CHECK(near(again.scale, 0.92f) && again.cull == Cull::none && again.pairs == b.pairs &&
          again.comments == b.comments);
    BonesFile fresh;
    studio_test::write(dir / "c.bones.txt", bones_text(fresh));
    const BonesFile plain = read_bones_file(dir / "c.bones.txt");
    CHECK(plain.pairs.empty() && near(plain.scale, 1.0f) && plain.cull == Cull::own);

    CHECK(writable_name("Bip01_L_Hand"));
    CHECK(!writable_name("Bip01 L Hand"));
    CHECK(!writable_name("a#b"));
    CHECK(!writable_name(""));
}

TEST(joints_follow_nodes_by_name_and_pairs) {
    const GameModel g = game_arm_out();
    const auto mod = mod_arm("root", "Bone7", 1, 0);
    Rig rig = build_rig(g, mod, {}, 1.0f);
    CHECK(rig.joints[0].node == 0);   // by name
    CHECK(rig.joints[1].node == -1);  // follows nothing: moves with its parent
    rig = build_rig(g, mod, {{"Bone7", "arm"}}, 1.0f);
    CHECK(rig.joints[1].node == 1);
    CHECK(rig.joints[1].exact);  // at its node's place, uncorrected
    rig = build_rig(g, mod, {{"Bone7", "nothere"}}, 1.0f);
    CHECK(rig.joints[1].node == -1 && rig.missing_nodes.size() == 1u);
}

TEST(rest_pose_turned_past_twenty_degrees) {
    const GameModel g = game_arm_out();
    // the mod's arm hangs down: 90 degrees from the game's, turned
    const Rig rig = build_rig(g, mod_arm("root", "arm", 0, -1), {}, 1.0f);
    CHECK(!rig.joints[0].exact);
    std::vector<Mat> skin, place;
    Mat unskinned{};
    pose_rig(g, rig, g.rest, &skin, &unskinned, &place);
    // a vertex two units down the mod's arm is drawn two units along the game's arm
    const auto p = mat_point(skin[1], {0, -2, 0});
    CHECK(near(p[0], 2) && near(p[1], 0) && near(p[2], 0));

    // 10 degrees off: left as it is
    const float a = 10.0f * 0.0174532925f;
    const Rig small = build_rig(g, mod_arm("root", "arm", std::cos(a), -std::sin(a)), {}, 1.0f);
    for (int i = 0; i < 16; ++i) {
        const float id = (i % 5 == 0) ? 1.0f : 0.0f;
        CHECK(near(small.joints[0].correction[static_cast<std::size_t>(i)], id));
    }
}

TEST(scale_and_rest) {
    const GameModel g = game_arm_out();
    // a model on the game's skeleton at twice its size, scale 0.5: exact at rest
    const Rig rig = build_rig(g, mod_arm("root", "arm", 2, 0), {}, 0.5f);
    CHECK(rig.joints[1].exact);
    std::vector<Mat> skin;
    Mat unskinned{};
    pose_rig(g, rig, g.rest, &skin, &unskinned);
    const auto p = mat_point(skin[1], {2, 0, 0});
    CHECK(near(p[0], 1) && near(p[1], 0));
}

TEST(proposals_by_position) {
    const GameModel g = game_arm_out();
    const auto mod = mod_arm("hips", "hand", 0.95f, 0.02f);
    const Rig rig = build_rig(g, mod, {}, 1.0f);
    const auto proposals = propose_pairs(g, rig);
    CHECK(proposals.size() == 2u);
    CHECK(proposals.at(0) == 0 && proposals.at(1) == 1);
}

TEST(a_new_model_keeps_the_bones_assigned) {
    // assigned on the model before: Bone7 -> arm, Bone9 -> root
    const std::map<std::string, std::string> before = {{"Bone7", "arm"}, {"Bone9", "root"}};
    // the new model has Bone7 but no Bone9
    const auto mod = mod_arm("root", "Bone7", 1, 0);
    std::map<std::string, std::string> pairs;
    CarriedPairs c = carry_pairs(before, mod, &pairs);
    CHECK(c.kept == 1 && pairs.at("Bone7") == "arm");
    CHECK(c.lost == std::vector<std::string>({"Bone9"}));
    // its own bones.txt names Bone7 already: that one wins
    pairs = {{"Bone7", "root"}};
    c = carry_pairs(before, mod, &pairs);
    CHECK(c.kept == 0 && pairs.at("Bone7") == "root" && c.lost.size() == 1u);
}
