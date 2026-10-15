// A BCA0 (NSBCA) with one of its animations replaced.
// The format as the port reads it (src/assets/animation.cpp,
// after apicula): a JNT0 section whose Nitro dictionary names the animations
// and gives each one's J0AC block; a J0AC holds the frame count, the node
// count, a word kept as it is, the pivot and basis rotation tables, and per
// node its flags, two bytes kept as they are, and its translation, rotation
// and scale curves.
//
// A J0AC's tables and curve data are not inside it: the game's BCA0s share
// them between their animations, after the J0ACs, at offsets from each J0AC.
// The new J0AC holds all of its own and is appended at the end of JNT0, its
// dictionary entry pointed at it: the dictionary (its names and tree) and
// every other animation stay byte for byte, the old J0AC stays unreferenced.
// In it the nodes the new animation moves are written whole, every frame
// sampled (translation 20.12, scale with its inverse 20.12, rotation as the
// compressed orthonormal basis of 13-bit 1.12 values); the nodes it leaves
// alone keep the original's node blocks byte for byte but for their data
// offsets, which point into a copy of the original's shared data (from the
// first thing they use to the end of JNT0), with the new rotations after its
// basis table. Frame and node counts are the original's: the game loops or
// stops an animation at the count its clip gives (for example
// Ov259_ModelAnimTick), and what else it times by those frames is not known.
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
