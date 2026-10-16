#include "khdays/assets/gltf.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

#include "khdays/assets/image.h"

// cgltf is third-party and not warning-clean; isolate it.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

const cgltf_accessor* find_attribute(
    const cgltf_primitive& primitive,
    const cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const auto& attr = primitive.attributes[i];
        if (attr.type == type && attr.index == 0) {
            return attr.data;
        }
    }
    return nullptr;
}

// The base-color image referenced by a primitive's material, or nullptr.
const cgltf_image* base_color_image(const cgltf_primitive& primitive) {
    const cgltf_material* material = primitive.material;
    if (material == nullptr || material->has_pbr_metallic_roughness == 0) {
        return nullptr;
    }
    const cgltf_texture* texture =
        material->pbr_metallic_roughness.base_color_texture.texture;
    if (texture == nullptr) {
        return nullptr;
    }
    return texture->image;
}

std::string image_key(const cgltf_image* image, std::size_t fallback_index) {
    if (image != nullptr && image->name != nullptr && image->name[0] != '\0') {
        return image->name;
    }
    if (image != nullptr && image->uri != nullptr) {
        return image->uri;
    }
    return "gltf_image_" + std::to_string(fallback_index);
}

// Column-major 4x4 multiply (result = a * b), matching the neutral palette's
// m[col * 4 + row] layout. glTF stores matrices column-major, so cgltf's
// transforms and inverse binds drop straight in.
std::array<float, 16> matrix_multiply(const float* a, const float* b) {
    std::array<float, 16> result{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return result;
}

constexpr std::array<float, 16> kIdentity{
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

}  // namespace

namespace khdays::assets {

ImportedModel import_gltf(const std::filesystem::path& input_path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const auto path_string = input_path.string();

    if (cgltf_parse_file(&options, path_string.c_str(), &data)
        != cgltf_result_success) {
        throw std::runtime_error("cannot parse glTF: " + path_string);
    }
    if (cgltf_load_buffers(&options, data, path_string.c_str())
        != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("cannot load glTF buffers: " + path_string);
    }

    ImportedModel result;
    result.model.name = input_path.stem().string();
    // Palette entry 0 is the identity, used by static (unskinned) meshes.
    result.model.palette.push_back(kIdentity);

    const auto base_dir = input_path.parent_path();

    // Load each referenced image once, keyed by name; remember its size. An
    // image is embedded (a buffer view -- a .glb's -- or a data URI) or a file
    // found by find_texture; one that cannot be had is drawn white and told
    // to the caller.
    std::map<std::string, DecodedTexture> loaded;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& image = data->images[i];
        const auto key = image_key(&image, i);
        if (loaded.count(key) != 0U) {
            continue;
        }
        try {
            DecodedTexture texture;
            if (image.buffer_view != nullptr) {
                const cgltf_buffer_view& view = *image.buffer_view;
                if (view.buffer == nullptr || view.buffer->data == nullptr) {
                    throw std::runtime_error("its buffer was not loaded");
                }
                texture = read_image(static_cast<const std::uint8_t*>(view.buffer->data) + view.offset, view.size, key);
            } else if (image.uri != nullptr && std::strncmp(image.uri, "data:", 5) == 0) {
                const char* comma = std::strchr(image.uri, ',');
                if (comma == nullptr || std::strstr(image.uri, ";base64,") == nullptr) {
                    throw std::runtime_error("a data URI that is not base64");
                }
                const std::size_t length = std::strlen(comma + 1);
                std::size_t size = length / 4 * 3;
                if (length >= 1 && comma[length] == '=') --size;
                if (length >= 2 && comma[length - 1] == '=') --size;
                void* bytes = nullptr;
                if (cgltf_load_buffer_base64(&options, size, comma + 1, &bytes) != cgltf_result_success) {
                    throw std::runtime_error("its base64 cannot be decoded");
                }
                try {
                    texture = read_image(static_cast<const std::uint8_t*>(bytes), size, key);
                } catch (...) {
                    std::free(bytes);
                    throw;
                }
                std::free(bytes);
            } else if (image.uri != nullptr) {
                std::string uri = image.uri;
                cgltf_decode_uri(uri.data());  // %20 and the like
                uri.resize(std::strlen(uri.c_str()));
                const auto file = find_texture(base_dir, uri);
                if (!file) {
                    throw std::runtime_error("not found");
                }
                texture = read_image(*file);
            } else {
                throw std::runtime_error("no data and no file");
            }
            texture.name = key;
            loaded.emplace(key, std::move(texture));
        } catch (const std::exception& error) {
            // The mesh falls back to white; the caller says so.
            result.missing_textures.emplace_back(key, error.what());
        }
    }

    // One palette block per skin, joint j's jointGlobalTransform[j] * inverseBind[j]: the identity in
    // the authored pose, re-posed by animated joints. A skinned mesh's node transform is ignored, as
    // glTF says.
    std::map<const cgltf_skin*, std::uint32_t> skin_base;
    for (cgltf_size s = 0; s < data->skins_count; ++s) {
        const cgltf_skin& skin = data->skins[s];
        skin_base.emplace(
            &skin, static_cast<std::uint32_t>(result.model.palette.size()));
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            cgltf_float world[16];
            cgltf_node_transform_world(skin.joints[j], world);
            std::array<float, 16> inverse_bind = kIdentity;
            if (skin.inverse_bind_matrices != nullptr) {
                cgltf_accessor_read_float(
                    skin.inverse_bind_matrices, j, inverse_bind.data(), 16);
            }
            ImportedJoint joint;
            joint.palette_index =
                static_cast<std::uint32_t>(result.model.palette.size());
            const cgltf_node* joint_node = skin.joints[j];
            if (joint_node != nullptr && joint_node->name != nullptr) {
                joint.name = joint_node->name;
            }
            joint.inverse_bind = inverse_bind;
            std::copy(world, world + 16, joint.world.begin());
            result.joints.push_back(std::move(joint));
            result.model.palette.push_back(
                matrix_multiply(world, inverse_bind.data()));
        }
        // Each joint's parent: its nearest ancestor among the skin's joints.
        const std::size_t first = result.joints.size() - skin.joints_count;
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            for (const cgltf_node* up = skin.joints[j]->parent;
                 up != nullptr && result.joints[first + j].parent < 0; up = up->parent) {
                for (cgltf_size k = 0; k < skin.joints_count; ++k) {
                    if (skin.joints[k] == up) {
                        result.joints[first + j].parent = static_cast<int>(first + k);
                        break;
                    }
                }
            }
        }
    }

    std::array<float, 4> position{};
    std::array<float, 2> uv{};

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (node.mesh == nullptr) {
            continue;
        }
        cgltf_float world[16];
        cgltf_node_transform_world(&node, world);
        const cgltf_mesh& mesh = *node.mesh;
        const cgltf_size m = n;  // index used for names

        // A skinned node places its geometry through the skeleton, so its own
        // world transform is ignored and each vertex carries bone indices.
        const cgltf_skin* skin = node.skin;
        const auto skin_it = skin != nullptr ? skin_base.find(skin) : skin_base.end();
        const std::uint32_t joint_base =
            skin_it != skin_base.end() ? skin_it->second : 0U;

        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles) {
                continue;
            }
            const cgltf_accessor* positions =
                find_attribute(primitive, cgltf_attribute_type_position);
            if (positions == nullptr) {
                continue;
            }
            const cgltf_accessor* texcoords =
                find_attribute(primitive, cgltf_attribute_type_texcoord);
            const cgltf_accessor* colors =
                find_attribute(primitive, cgltf_attribute_type_color);
            const cgltf_accessor* normals =
                find_attribute(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* joints =
                skin != nullptr
                    ? find_attribute(primitive, cgltf_attribute_type_joints)
                    : nullptr;
            const cgltf_accessor* weights =
                skin != nullptr
                    ? find_attribute(primitive, cgltf_attribute_type_weights)
                    : nullptr;
            const bool skinned = joints != nullptr && weights != nullptr;

            const cgltf_image* image = base_color_image(primitive);
            const auto texture_key =
                image != nullptr ? image_key(image, m) : std::string{};
            float tex_w = 1.0F;
            float tex_h = 1.0F;
            const auto texture_it = loaded.find(texture_key);
            if (texture_it != loaded.end()) {
                tex_w = static_cast<float>(texture_it->second.width);
                tex_h = static_cast<float>(texture_it->second.height);
            }

            NeutralMesh out_mesh;
            out_mesh.name =
                mesh.name != nullptr ? mesh.name : ("mesh_" + std::to_string(m));
            out_mesh.texture_name = texture_key;
            out_mesh.material_name =
                primitive.material != nullptr && primitive.material->name != nullptr ? primitive.material->name : "";
            out_mesh.double_sided =
                primitive.material != nullptr && primitive.material->double_sided != 0;
            if (primitive.material != nullptr) {
                switch (primitive.material->alpha_mode) {
                    case cgltf_alpha_mode_mask:
                        out_mesh.alpha_mode = NeutralMesh::AlphaMode::mask;
                        out_mesh.alpha_cutoff = primitive.material->alpha_cutoff;
                        break;
                    case cgltf_alpha_mode_blend:
                        out_mesh.alpha_mode = NeutralMesh::AlphaMode::blend;
                        break;
                    default:
                        out_mesh.alpha_mode = NeutralMesh::AlphaMode::opaque;
                        break;
                }
            } else {
                out_mesh.alpha_mode = NeutralMesh::AlphaMode::opaque;  // glTF's default material
            }

            const auto vertex_count = positions->count;
            out_mesh.vertices.reserve(vertex_count);
            for (cgltf_size v = 0; v < vertex_count; ++v) {
                NeutralVertex vertex;
                cgltf_accessor_read_float(positions, v, position.data(), 3);
                if (skinned) {
                    // The skeleton (palette) places the geometry; keep the raw
                    // position, exactly like a DS vertex.
                    vertex.position = {position[0], position[1], position[2]};
                } else {
                    // Static mesh: bake in the node's world transform.
                    vertex.position = {
                        world[0] * position[0] + world[4] * position[1]
                            + world[8] * position[2] + world[12],
                        world[1] * position[0] + world[5] * position[1]
                            + world[9] * position[2] + world[13],
                        world[2] * position[0] + world[6] * position[1]
                            + world[10] * position[2] + world[14]};
                }

                std::array<float, 3> normal{};
                if (normals != nullptr
                    && cgltf_accessor_read_float(normals, v, normal.data(), 3)) {
                    // Like the position: raw when skinned, else through the
                    // node's transform (its 3x3; renormalize before lighting).
                    vertex.normal = skinned
                        ? normal
                        : std::array<float, 3>{
                              world[0] * normal[0] + world[4] * normal[1] + world[8] * normal[2],
                              world[1] * normal[0] + world[5] * normal[1] + world[9] * normal[2],
                              world[2] * normal[0] + world[6] * normal[1] + world[10] * normal[2]};
                }

                if (texcoords != nullptr
                    && cgltf_accessor_read_float(texcoords, v, uv.data(), 2)) {
                    // Store as texels so the renderer normalizes like DS UVs.
                    vertex.texcoord = {uv[0] * tex_w, uv[1] * tex_h};
                }

                std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
                if (colors != nullptr) {
                    cgltf_accessor_read_float(colors, v, color.data(), 4);
                }
                vertex.color = {
                    static_cast<std::uint8_t>(color[0] * 255.0F),
                    static_cast<std::uint8_t>(color[1] * 255.0F),
                    static_cast<std::uint8_t>(color[2] * 255.0F),
                    static_cast<std::uint8_t>(color[3] * 255.0F)};

                if (skinned) {
                    cgltf_uint joint_indices[4] = {0U, 0U, 0U, 0U};
                    cgltf_accessor_read_uint(joints, v, joint_indices, 4);
                    cgltf_accessor_read_float(weights, v, vertex.weights.data(), 4);
                    for (int i = 0; i < 4; ++i) {
                        vertex.joints[static_cast<std::size_t>(i)] =
                            joint_base + joint_indices[i];
                    }
                }
                // Static vertices keep the defaults: palette 0 (identity),
                // weight 1.
                out_mesh.vertices.push_back(vertex);
            }

            if (primitive.indices != nullptr) {
                out_mesh.indices.reserve(primitive.indices->count);
                for (cgltf_size k = 0; k < primitive.indices->count; ++k) {
                    out_mesh.indices.push_back(static_cast<std::uint32_t>(
                        cgltf_accessor_read_index(primitive.indices, k)));
                }
            } else {
                for (cgltf_size k = 0; k < vertex_count; ++k) {
                    out_mesh.indices.push_back(static_cast<std::uint32_t>(k));
                }
            }

            result.model.header_vertex_count += static_cast<std::uint16_t>(
                out_mesh.vertices.size());
            result.model.meshes.push_back(std::move(out_mesh));
        }
    }

    for (auto& [key, texture] : loaded) {
        result.textures.emplace_back(key, std::move(texture));
    }

    cgltf_free(data);
    return result;
}

}  // namespace khdays::assets
