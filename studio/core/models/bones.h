// A mod's model on the game's skeleton, by the port's rules.
// The port pairs and poses a mod's model in
// port/runtime/mods/models.cpp; that code is bound to the running game, so these
// rules are mirrored here, each citing the lines it follows. A change there
// must be made here too: the tests and the in-game comparison check
// the two agree.
#pragma once

#include "core/models/game_model.h"

#include "khdays/assets/model_import.h"

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace studio {

// `<model>.bones.txt` (models.cpp read_bones, lines 207-227): '#' starts a
// comment; a line of two words is a joint and the node it follows, or
// `scale <k>` (k > 0, else 1); and `cull back` / `cull none`.
enum class Cull { own, back, none };
struct BonesFile {
    std::vector<std::pair<std::string, std::string>> pairs;  // joint -> node, in file order
    float scale = 1.0f;
    Cull cull = Cull::own;
    std::vector<std::string> comments;  // whole-line comments, kept when the file is written again
};
BonesFile read_bones_file(const std::filesystem::path& file);
std::string bones_text(const BonesFile& bones);  // the file's words (comments the tool writes included)

// Whether a name can be written in bones.txt (no whitespace, no '#').
bool writable_name(const std::string& name);

struct RigJoint {
    std::string name;
    std::uint32_t palette_index = 0;
    int parent = -1;     // in Rig::joints, parents first
    int node = -1;       // the game's node it follows, -1 none
    bool exact = false;  // at its node's rest place, uncorrected: drawn with D_G
    std::array<float, 3> head{};
    Mat correction{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mat bind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 3> rest_head{};  // the joint's head before scaling (for proposals)
};

struct Rig {
    std::vector<RigJoint> joints;
    std::map<std::uint32_t, int> joint_of_palette;  // a vertex's joint index -> Rig::joints
    float scale = 1.0f;
    std::vector<std::string> missing_nodes;          // pairs naming a node the game model lacks
};

// The joints of `mod` on the game model `g` with `pairs` and `scale`
// (models.cpp load(), lines 269-360).
Rig build_rig(const GameModel& g, const khdays::assets::ImportedModel& mod,
              const std::map<std::string, std::string>& pairs, float scale);

// Each joint's skin matrix (vertices of the joint's palette entry to where
// they are drawn) for the game's nodes at `node_world` (models.cpp draw(),
// lines 428-488), and the matrix a vertex with no joint takes.
// `place` (optional) gets each joint's K: its rest place to where it is drawn.
void pose_rig(const GameModel& g, const Rig& rig, const std::vector<Mat>& node_world, std::vector<Mat>* skin,
              Mat* unskinned, std::vector<Mat>* place = nullptr);

// The pairs set on a model being replaced, carried over to the new one: each
// joint the new model also has keeps its node unless `pairs` (the new model's
// own bones.txt) names it already; the others are lost, by name -- so a
// modder who imports a better model keeps the bones they assigned.
struct CarriedPairs {
    int kept = 0;
    std::vector<std::string> lost;  // joints the new model has no joint of that name for
};
CarriedPairs carry_pairs(const std::map<std::string, std::string>& before, const khdays::assets::ImportedModel& model,
                         std::map<std::string, std::string>* pairs);

// For each joint that follows no node, the nearest game node by where their
// heads are at rest (the model scaled by `scale`): a proposal for the modder,
// never applied by itself. Joint index -> node index.
std::map<int, int> propose_pairs(const GameModel& g, const Rig& rig);

// Matrix helpers (column-major, models.cpp's convention).
Mat mat_mul(const Mat& a, const Mat& b);
Mat mat_inverse(const Mat& m);
std::array<float, 3> mat_point(const Mat& m, const std::array<float, 3>& p);
std::array<float, 3> mat_vec(const Mat& m, const std::array<float, 3>& v);

}  // namespace studio
