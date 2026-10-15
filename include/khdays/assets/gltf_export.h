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

// `model` as one .gltf with its buffer embedded: a node per bone (its rest
// matrix under its parent, NeutralModel::object_parents -- as translation,
// rotation and scale when animations move it), one skin whose
// joints are the bones and whose inverse binds are their rest world matrices
// inverted, and the meshes in their rest pose -- each vertex weighted to the
// bones its palette entry is made of (NeutralModel::palette_bones), with its
// normal, texture coordinates over `textures`' sizes and colour. A mesh whose
// texture_name is not in `textures` is written untextured.
//
// Each of `animations` (of a model with a skeleton) becomes a glTF animation:
// at each of its frames, `frames_per_second` apart from 0, every bone's matrix
// under its parent as the skeleton composes the object matrices, as translation, rotation and
// scale keys (linear), every bone's, so that each animation sets the whole
// skeleton; a track that keeps one value through the animation is one key.
std::string to_gltf(const NeutralModel& model, const std::map<std::string, GltfTextureRef>& textures,
                    const std::vector<GltfAnimation>& animations = {}, float frames_per_second = 30.0F);

}  // namespace khdays::assets
