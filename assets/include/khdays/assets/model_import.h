#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"

namespace khdays::assets {

// One joint of an imported skin: the palette entry it fills, its bone name (for
// matching a DS skeleton), and its inverse-bind matrix. Used to retarget DS
// animations onto the imported geometry.
struct ImportedJoint final {
    std::uint32_t palette_index = 0U;
    std::string name;
    std::array<float, 16> inverse_bind{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // The joint's global transform as the file places it (its rest),
    // column-major, and its parent: the nearest ancestor that is a joint of
    // the same skin, as an index into ImportedModel::joints, or -1.
    std::array<float, 16> world{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    int parent = -1;
};

// A model imported from a mod's file (glTF, gltf.h; FBX, fbx.h): neutral
// geometry plus the textures it ships with, keyed by the name each mesh
// references, in glTF's frame (Y up, metres). Rigged meshes are supported: each
// skin contributes a block of palette matrices (jointGlobal * inverseBind) and
// its vertices carry up to four bone indices and weights, so the model renders
// in its authored pose and can be re-posed. Static meshes bake their node
// transform and use the identity palette entry. Texture coordinates are in
// texels relative to each texture, matching the DS convention, so the renderer
// normalizes both the same way.
struct ImportedModel final {
    NeutralModel model;
    std::vector<std::pair<std::string, DecodedTexture>> textures;
    // Skin joints across all skins, aligned with the palette blocks built for
    // them. Empty for a fully static model.
    std::vector<ImportedJoint> joints;
    // Textures the model uses that were not found or could not be read (each
    // drawn white), with the reason, for the caller to report.
    std::vector<std::pair<std::string, std::string>> missing_textures;
};

}  // namespace khdays::assets
