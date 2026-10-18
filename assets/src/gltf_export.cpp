#include "khdays/assets/gltf_export.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace khdays::assets {

namespace {

using Matrix = std::array<float, 16>;  // column-major: m[col * 4 + row]

Matrix identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k)
                sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    }
    return r;
}

// The general 4x4 inverse (cofactors); identity when singular.
Matrix inverse(const Matrix& m) {
    Matrix inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12F)
        return identity();
    for (auto& v : inv)
        v /= det;
    return inv;
}

std::array<float, 3> apply(const Matrix& m, const std::array<float, 3>& p, float w) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12] * w, m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13] * w,
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] * w};
}

// A matrix as glTF's translation, rotation (x, y, z, w) and scale, T * R * S:
// a mirror goes into the scale's x; the rotation of a sheared matrix is the
// nearest one to its columns.
struct Trs final {
    std::array<float, 3> t{0, 0, 0};
    std::array<float, 4> r{0, 0, 0, 1};
    std::array<float, 3> s{1, 1, 1};
};

Trs decompose(const Matrix& m) {
    Trs o;
    o.t = {m[12], m[13], m[14]};
    std::array<std::array<float, 3>, 3> c{};
    for (std::size_t k = 0; k < 3; ++k) {
        c[k] = {m[4 * k], m[4 * k + 1], m[4 * k + 2]};
        o.s[k] = std::sqrt(c[k][0] * c[k][0] + c[k][1] * c[k][1] + c[k][2] * c[k][2]);
        if (o.s[k] > 1e-12F) {
            for (auto& v : c[k])
                v /= o.s[k];
        }
    }
    const float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) -
                      c[1][0] * (c[0][1] * c[2][2] - c[0][2] * c[2][1]) +
                      c[2][0] * (c[0][1] * c[1][2] - c[0][2] * c[1][1]);
    if (det < 0.0F) {
        o.s[0] = -o.s[0];
        for (auto& v : c[0])
            v = -v;
    }
    // c[col][row]
    const float trace = c[0][0] + c[1][1] + c[2][2];
    std::array<float, 4> q{};
    if (trace > 0.0F) {
        const float k = std::sqrt(trace + 1.0F) * 2.0F;
        q = {(c[1][2] - c[2][1]) / k, (c[2][0] - c[0][2]) / k, (c[0][1] - c[1][0]) / k, 0.25F * k};
    } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
        const float k = std::sqrt(1.0F + c[0][0] - c[1][1] - c[2][2]) * 2.0F;
        q = {0.25F * k, (c[1][0] + c[0][1]) / k, (c[2][0] + c[0][2]) / k, (c[1][2] - c[2][1]) / k};
    } else if (c[1][1] > c[2][2]) {
        const float k = std::sqrt(1.0F + c[1][1] - c[0][0] - c[2][2]) * 2.0F;
        q = {(c[1][0] + c[0][1]) / k, 0.25F * k, (c[2][1] + c[1][2]) / k, (c[2][0] - c[0][2]) / k};
    } else {
        const float k = std::sqrt(1.0F + c[2][2] - c[0][0] - c[1][1]) * 2.0F;
        q = {(c[2][0] + c[0][2]) / k, (c[2][1] + c[1][2]) / k, 0.25F * k, (c[0][1] - c[1][0]) / k};
    }
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-12F) {
        for (auto& v : q)
            v /= len;
        o.r = q;
    }
    return o;
}

// Each bone's matrix under its parent, from the bones' world matrices.
std::vector<Matrix> locals(const std::vector<Matrix>& world, const std::vector<int>& parents) {
    std::vector<Matrix> out(world.size());
    for (std::size_t b = 0; b < world.size(); ++b) {
        const bool has_parent = parents[b] >= 0 && static_cast<std::size_t>(parents[b]) < world.size();
        out[b] = has_parent ? multiply(inverse(world[static_cast<std::size_t>(parents[b])]), world[b]) : world[b];
    }
    return out;
}

std::string base64(const std::vector<std::uint8_t>& bytes) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t a = bytes[i];
        const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const std::uint32_t v = a << 16 | b << 8 | c;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? table[(v >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? table[v & 63] : '=';
    }
    return out;
}

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) < 0x20)
            continue;
        out += c;
    }
    return out + "\"";
}

// The buffer, with a view and an accessor per array written to it.
struct Writer final {
    std::vector<std::uint8_t> buffer;
    std::ostringstream views, accessors;
    int nviews = 0, naccessors = 0;

    template <typename T>
    int add(const std::vector<T>& data, int components, int component_type, const char* type, int target,
            bool normalized = false, const std::vector<float>* min = nullptr, const std::vector<float>* max = nullptr) {
        while (buffer.size() % 4 != 0)
            buffer.push_back(0);
        const std::size_t offset = buffer.size();
        const std::size_t bytes = data.size() * sizeof(T);
        buffer.resize(offset + bytes);
        if (bytes != 0)
            std::memcpy(buffer.data() + offset, data.data(), bytes);
        views << (nviews ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << offset << ",\"byteLength\":" << bytes;
        if (target)
            views << ",\"target\":" << target;
        views << "}";
        accessors << (naccessors ? "," : "") << "{\"bufferView\":" << nviews << ",\"componentType\":" << component_type
                  << ",\"count\":" << data.size() / static_cast<std::size_t>(components) << ",\"type\":\"" << type
                  << "\"";
        if (normalized)
            accessors << ",\"normalized\":true";
        if (min != nullptr && max != nullptr) {
            accessors << ",\"min\":[";
            for (std::size_t i = 0; i < min->size(); ++i)
                accessors << (i ? "," : "") << (*min)[i];
            accessors << "],\"max\":[";
            for (std::size_t i = 0; i < max->size(); ++i)
                accessors << (i ? "," : "") << (*max)[i];
            accessors << "]";
        }
        accessors << "}";
        ++nviews;
        return naccessors++;
    }
};

}  // namespace

bool texels_translucent(const std::uint8_t* rgba, std::size_t pixels) {
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t a = rgba[i * 4 + 3];
        if (a >= 8 && a < 248)
            return true;
    }
    return false;
}

std::string to_gltf(const NeutralModel& model, const std::map<std::string, GltfTextureRef>& textures,
                    const std::vector<GltfAnimation>& animations, float frames_per_second) {
    const std::size_t nbones = model.object_names.size();
    std::vector<Matrix> world = model.skinning != nullptr
                                    ? compute_bone_world_matrices(*model.skinning, model.object_matrices)
                                    : std::vector<Matrix>(nbones, identity());
    world.resize(nbones, identity());
    std::vector<int> parents = model.object_parents;
    parents.resize(nbones, -1);

    Writer w;
    w.accessors << std::setprecision(9);
    std::ostringstream nodes, primitives, materials, images, gltf_textures;
    nodes << std::setprecision(9);

    // the bones: their rest matrices under their parents
    std::vector<std::vector<std::size_t>> children(nbones);
    for (std::size_t b = 0; b < nbones; ++b) {
        if (parents[b] >= 0 && static_cast<std::size_t>(parents[b]) < nbones && parents[b] != static_cast<int>(b)) {
            children[static_cast<std::size_t>(parents[b])].push_back(b);
        }
    }
    const std::vector<Matrix> rest_local = locals(world, parents);
    // a node an animation moves holds its rest as translation, rotation and
    // scale (glTF 2.0: an animated node has no matrix); else its matrix
    const bool animated =
        nbones != 0 && model.skinning != nullptr && frames_per_second > 0.0F &&
        std::any_of(animations.begin(), animations.end(), [](const GltfAnimation& a) { return !a.frames.empty(); });
    for (std::size_t b = 0; b < nbones; ++b) {
        const Matrix& local = rest_local[b];
        nodes << (b ? "," : "") << "{\"name\":" << json_string(model.object_names[b]);
        if (animated) {
            const Trs rest = decompose(local);
            nodes << ",\"translation\":[" << rest.t[0] << "," << rest.t[1] << "," << rest.t[2] << "],\"rotation\":["
                  << rest.r[0] << "," << rest.r[1] << "," << rest.r[2] << "," << rest.r[3] << "],\"scale\":["
                  << rest.s[0] << "," << rest.s[1] << "," << rest.s[2] << "]";
        } else {
            nodes << ",\"matrix\":[";
            for (int k = 0; k < 16; ++k)
                nodes << (k ? "," : "") << local[static_cast<std::size_t>(k)];
            nodes << "]";
        }
        if (!children[b].empty()) {
            nodes << ",\"children\":[";
            for (std::size_t c = 0; c < children[b].size(); ++c)
                nodes << (c ? "," : "") << children[b][c];
            nodes << "]";
        }
        nodes << "}";
    }
    // the inverse binds
    std::vector<float> inverse_binds;
    for (std::size_t b = 0; b < nbones; ++b) {
        const Matrix inv = inverse(world[b]);
        inverse_binds.insert(inverse_binds.end(), inv.begin(), inv.end());
    }
    const int inverse_bind_accessor = nbones ? w.add(inverse_binds, 16, 5126, "MAT4", 0) : -1;

    // the materials: one per texture, alpha and sides, and one untextured. A mesh takes its alpha
    // as its material said (an imported model's: opaque, cut out at its cutoff, or blended); a
    // decoded DS mesh, or an FBX's, says nothing: blended when its texture's texels or its vertex
    // colours have an alpha of their own (A3I5, A5I3, which the DS blends; a room's shading in vertex
    // colours black at alpha 0.05), else cut out at half, as the DS's alpha test draws it -- as the
    // port takes a model that does not say (runtime/mods/models.cpp). Its back faces drawn or not as the mesh says
    // (a DS material's POLYGON_ATTR, an imported one's doubleSided): a room remade with one-sided
    // pumpkins showed their insides through the mouth
    std::map<std::string, int> material_of, image_of;
    int nmaterials = 0, nimages = 0;
    const auto material_for = [&](const std::string& texture, const NeutralMesh& mesh) {
        using AlphaMode = NeutralMesh::AlphaMode;
        const auto ref = textures.find(texture);
        bool partial_vertex = false;
        for (const auto& v : mesh.vertices)
            partial_vertex = partial_vertex || (v.color[3] >= 8 && v.color[3] < 248);
        const bool blended = mesh.alpha_mode == AlphaMode::blend ||
                             (mesh.alpha_mode == AlphaMode::unknown &&
                              (partial_vertex || (ref != textures.end() && ref->second.translucent)));
        const char* mode = mesh.alpha_mode == AlphaMode::opaque ? "OPAQUE" : blended ? "BLEND" : "MASK";
        const float cutoff = mesh.alpha_mode == AlphaMode::mask ? mesh.alpha_cutoff : 0.5F;
        std::ostringstream key;
        key << texture << '\n' << mode << '\n' << cutoff << '\n' << mesh.double_sided;
        const auto it = material_of.find(key.str());
        if (it != material_of.end())
            return it->second;
        materials << (nmaterials ? "," : "") << "{\"name\":" << json_string(texture.empty() ? "untextured" : texture)
                  << (mesh.double_sided ? ",\"doubleSided\":true" : "") << ",\"alphaMode\":\"" << mode << "\"";
        if (std::strcmp(mode, "MASK") == 0)
            materials << ",\"alphaCutoff\":" << cutoff;
        materials << ",\"pbrMetallicRoughness\":{\"metallicFactor\":0,\"roughnessFactor\":1";
        if (ref != textures.end()) {
            auto image = image_of.find(texture);
            if (image == image_of.end()) {
                images << (nimages ? "," : "") << "{\"uri\":" << json_string(ref->second.uri) << "}";
                gltf_textures << (nimages ? "," : "") << "{\"sampler\":0,\"source\":" << nimages << "}";
                image = image_of.emplace(texture, nimages++).first;
            }
            materials << ",\"baseColorTexture\":{\"index\":" << image->second << "}";
        }
        materials << "}}";
        material_of[key.str()] = nmaterials;
        return nmaterials++;
    };

    // the meshes, in their rest pose
    int nprimitives = 0;
    for (const auto& mesh : model.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty())
            continue;
        const auto ref = textures.find(mesh.texture_name);
        const float tw = ref != textures.end() && ref->second.width > 0 ? static_cast<float>(ref->second.width) : 1.0F;
        const float th =
            ref != textures.end() && ref->second.height > 0 ? static_cast<float>(ref->second.height) : 1.0F;
        std::vector<float> positions, normals, uvs, weights;
        std::vector<std::uint8_t> colors;
        std::vector<std::uint16_t> joints;
        std::vector<float> lo{1e30F, 1e30F, 1e30F}, hi{-1e30F, -1e30F, -1e30F};
        bool any_normal = false;
        for (const auto& v : mesh.vertices) {
            const std::uint32_t slot = v.joints[0];
            const Matrix palette = slot < model.palette.size() ? model.palette[slot] : identity();
            const auto p = apply(palette, v.position, 1.0F);
            for (int k = 0; k < 3; ++k) {
                positions.push_back(p[static_cast<std::size_t>(k)]);
                lo[static_cast<std::size_t>(k)] =
                    std::min(lo[static_cast<std::size_t>(k)], p[static_cast<std::size_t>(k)]);
                hi[static_cast<std::size_t>(k)] =
                    std::max(hi[static_cast<std::size_t>(k)], p[static_cast<std::size_t>(k)]);
            }
            auto n = apply(palette, v.normal, 0.0F);
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 1e-6F) {
                any_normal = true;
                for (auto& c : n)
                    c /= len;
            } else {
                n = {0.0F, 1.0F, 0.0F};
            }
            normals.insert(normals.end(), n.begin(), n.end());
            uvs.push_back(v.texcoord[0] / tw);
            uvs.push_back(v.texcoord[1] / th);
            colors.insert(colors.end(), v.color.begin(), v.color.end());
            // the bones this vertex's palette entry is made of: the four heaviest
            std::vector<std::pair<std::uint32_t, float>> bones;
            if (slot < model.palette_bones.size())
                bones = model.palette_bones[slot];
            std::sort(bones.begin(), bones.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            if (bones.size() > 4)
                bones.resize(4);
            float sum = 0.0F;
            for (const auto& b : bones)
                sum += b.second;
            for (std::size_t k = 0; k < 4; ++k) {
                const bool used = k < bones.size() && bones[k].first < nbones && sum > 0.0F;
                joints.push_back(used ? static_cast<std::uint16_t>(bones[k].first) : 0);
                weights.push_back(used ? bones[k].second / sum
                                       : (k == 0 && (bones.empty() || sum <= 0.0F) ? 1.0F : 0.0F));
            }
        }
        const int pos = w.add(positions, 3, 5126, "VEC3", 34962, false, &lo, &hi);
        const int nor = any_normal ? w.add(normals, 3, 5126, "VEC3", 34962) : -1;
        const int uv = w.add(uvs, 2, 5126, "VEC2", 34962);
        const int col = w.add(colors, 4, 5121, "VEC4", 34962, true);
        const int jnt = nbones ? w.add(joints, 4, 5123, "VEC4", 34962) : -1;
        const int wgt = nbones ? w.add(weights, 4, 5126, "VEC4", 34962) : -1;
        const int idx = w.add(mesh.indices, 1, 5125, "SCALAR", 34963);
        const int material = material_for(ref != textures.end() ? mesh.texture_name : std::string(), mesh);
        primitives << (nprimitives ? "," : "") << "{\"attributes\":{\"POSITION\":" << pos;
        if (nor >= 0)
            primitives << ",\"NORMAL\":" << nor;
        primitives << ",\"TEXCOORD_0\":" << uv << ",\"COLOR_0\":" << col;
        if (jnt >= 0)
            primitives << ",\"JOINTS_0\":" << jnt << ",\"WEIGHTS_0\":" << wgt;
        primitives << "},\"indices\":" << idx << ",\"material\":" << material << "}";
        ++nprimitives;
    }

    // the animations: every bone's matrix under its parent at each frame
    std::ostringstream anims;
    anims << std::setprecision(9);
    int nanims = 0;
    int one_key = -1;  // the time of a track that keeps one value
    for (const auto& a : animations) {
        if (!animated)
            break;
        if (a.frames.empty())
            continue;
        const std::size_t frames = a.frames.size();
        std::vector<std::vector<Trs>> pose(frames);
        for (std::size_t f = 0; f < frames; ++f) {
            std::vector<Matrix> posed = compute_bone_world_matrices(*model.skinning, a.frames[f]);
            posed.resize(nbones, identity());
            for (const auto& m : locals(posed, parents))
                pose[f].push_back(decompose(m));
            // each rotation on the side of the one before, so that the keys turn the short way
            for (std::size_t b = 0; f > 0 && b < nbones; ++b) {
                const auto& was = pose[f - 1][b].r;
                auto& r = pose[f][b].r;
                if (was[0] * r[0] + was[1] * r[1] + was[2] * r[2] + was[3] * r[3] < 0.0F) {
                    for (auto& v : r)
                        v = -v;
                }
            }
        }
        std::vector<float> times(frames);
        for (std::size_t f = 0; f < frames; ++f)
            times[f] = static_cast<float>(f) / frames_per_second;
        int all_keys = -1;
        std::ostringstream samplers, channels;
        int nchannels = 0;
        const char* paths[] = {"translation", "rotation", "scale"};
        for (std::size_t b = 0; b < nbones; ++b) {
            for (int path = 0; path < 3; ++path) {
                const std::size_t n = path == 1 ? 4 : 3;
                std::vector<float> values;
                bool constant = true;
                for (std::size_t f = 0; f < frames; ++f) {
                    const Trs& p = pose[f][b];
                    const float* v = path == 0 ? p.t.data() : path == 1 ? p.r.data() : p.s.data();
                    const float* first = path == 0   ? pose[0][b].t.data()
                                         : path == 1 ? pose[0][b].r.data()
                                                     : pose[0][b].s.data();
                    for (std::size_t k = 0; k < n; ++k) {
                        constant =
                            constant && std::fabs(v[k] - first[k]) <= 1e-6F * std::max(1.0F, std::fabs(first[k]));
                    }
                    values.insert(values.end(), v, v + n);
                }
                if (constant)
                    values.resize(n);
                int input = -1;
                if (constant) {
                    if (one_key < 0) {
                        const std::vector<float> zero{0.0F};
                        one_key = w.add(zero, 1, 5126, "SCALAR", 0, false, &zero, &zero);
                    }
                    input = one_key;
                } else {
                    if (all_keys < 0) {
                        const std::vector<float> lo{times.front()}, hi{times.back()};
                        all_keys = w.add(times, 1, 5126, "SCALAR", 0, false, &lo, &hi);
                    }
                    input = all_keys;
                }
                const int output = w.add(values, static_cast<int>(n), 5126, n == 4 ? "VEC4" : "VEC3", 0);
                samplers << (nchannels ? "," : "") << "{\"input\":" << input << ",\"output\":" << output
                         << ",\"interpolation\":\"LINEAR\"}";
                channels << (nchannels ? "," : "") << "{\"sampler\":" << nchannels << ",\"target\":{\"node\":" << b
                         << ",\"path\":\"" << paths[path] << "\"}}";
                ++nchannels;
            }
        }
        anims << (nanims ? "," : "") << "{\"name\":" << json_string(a.name) << ",\"samplers\":[" << samplers.str()
              << "],\"channels\":[" << channels.str() << "]}";
        ++nanims;
    }

    // the mesh's node, and the scene: the root bones and the mesh
    const std::size_t mesh_node = nbones;
    if (nbones)
        nodes << ",";
    nodes << "{\"name\":" << json_string(model.name) << (nprimitives ? ",\"mesh\":0" : "")
          << (nbones && nprimitives ? ",\"skin\":0" : "") << "}";
    std::ostringstream out;
    out << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"khdays-native --dump-graphics\"},\"scene\":0,"
        << "\"scenes\":[{\"nodes\":[";
    bool first = true;
    for (std::size_t b = 0; b < nbones; ++b) {
        if (parents[b] < 0 || static_cast<std::size_t>(parents[b]) >= nbones) {
            out << (first ? "" : ",") << b;
            first = false;
        }
    }
    out << (first ? "" : ",") << mesh_node << "]}],\"nodes\":[" << nodes.str() << "]";
    if (nprimitives) {
        out << ",\"meshes\":[{\"name\":" << json_string(model.name) << ",\"primitives\":[" << primitives.str() << "]}]";
    }
    if (nbones && nprimitives) {
        out << ",\"skins\":[{\"name\":" << json_string(model.name)
            << ",\"inverseBindMatrices\":" << inverse_bind_accessor << ",\"joints\":[";
        for (std::size_t b = 0; b < nbones; ++b)
            out << (b ? "," : "") << b;
        out << "]}]";
    }
    if (nanims)
        out << ",\"animations\":[" << anims.str() << "]";
    if (nmaterials)
        out << ",\"materials\":[" << materials.str() << "]";
    if (nimages) {
        // nearest, as the DS samples
        out << ",\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728}],\"images\":[" << images.str()
            << "],\"textures\":[" << gltf_textures.str() << "]";
    }
    out << ",\"buffers\":[{\"byteLength\":" << w.buffer.size() << ",\"uri\":\"data:application/octet-stream;base64,"
        << base64(w.buffer) << "\"}],\"bufferViews\":[" << w.views.str() << "],\"accessors\":[" << w.accessors.str()
        << "]}";
    return out.str();
}

}  // namespace khdays::assets
