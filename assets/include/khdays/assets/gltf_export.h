#pragma once

// A decoded DS model written out as glTF 2.0 with its skeleton, to make
// replacement models from (the port's graphics dump).

#include <array>
#include <map>
#include <string>
#include <vector>

#include "khdays/assets/mesh.h"

namespace khdays::assets {

// A texture the glTF refers to: its image's URI (relative to the .gltf) and
// its size in texels, the unit the DS's texture coordinates count in.
struct GltfTextureRef final {
    std::string uri;
    int width = 0;
    int height = 0;
};

// An animation of the model's skeleton to write with it, under `name`: the
// bones' object matrices (as NeutralModel::object_matrices) at each of its
// frames, each a translation, a rotation and a scale -- not a blend of two
// rotations' matrices, which glTF's rotation keys cannot hold.
struct GltfAnimation final {
    std::string name;
    std::vector<std::vector<std::array<float, 16>>> frames;
};

// `model` as one .gltf with its buffer embedded: a node per bone under its parent, one skin (the bones,
// their rest world matrices inverted), the meshes in their rest pose weighted to their palette's bones,
// textured over `textures`' sizes (untextured when missing). Each of `animations` becomes a glTF
// animation: every bone's matrix under its parent at each frame as linear TRS keys, a constant track
// one key.
std::string to_gltf(const NeutralModel& model, const std::map<std::string, GltfTextureRef>& textures,
                    const std::vector<GltfAnimation>& animations = {}, float frames_per_second = 30.0F);

}  // namespace khdays::assets
