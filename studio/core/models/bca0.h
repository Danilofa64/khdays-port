// A BCA0 (NSBCA) with one of its animations replaced: the new J0AC, holding all of its own data, is
// appended to JNT0 and its dictionary entry pointed at it; everything else stays byte for byte. Frame
// and node counts are the original's (docs/FORMATS.md, "Animations"; docs/STUDIO.md).
#pragma once

#include "core/models/game_model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace studio {

struct Bca0Animation {
    std::string name;  // from the dictionary
    int frames = 0;
    int nodes = 0;
};

// The animations a BCA0 names (empty on a malformed one).
std::vector<Bca0Animation> bca0_animations(const std::uint8_t* data, std::size_t size);

// Each node's local matrix (column-major) at each frame: pose[frame][node].
using AnimationPose = std::vector<std::vector<Mat>>;

// `m` as a J0AC can hold it, before its fixed-point rounding: its
// translation, the rotation nearest its first two columns, and a scale per
// column (a mirror in the third). A matrix that is not a rotation times a
// scale (one blended between two rotations, a sheared one) comes out the
// nearest that is.
Mat bca0_trs(const Mat& m);

// The BCA0 with animation `index` replaced by `pose` (its frame and node
// counts must be the original's). `keep[node]` true: that node keeps its
// curves from animation `index` of `original` (the game's own BCA0 the
// animation came from; null: `bca0` itself), its pose ignored; an empty
// `keep` keeps none. False with the reason.
bool replace_bca0_animation(const std::vector<std::uint8_t>& bca0, std::size_t index, const AnimationPose& pose,
                            const std::vector<bool>& keep, const std::vector<std::uint8_t>* original,
                            std::vector<std::uint8_t>* out, std::string* why);

}  // namespace studio
