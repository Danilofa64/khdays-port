#include "core/models/clip_import.h"

#include "core/files/paths.h"

#include "cgltf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>

namespace studio {

namespace {

using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>;  // x, y, z, w (glTF's order)

struct Channel {
    const cgltf_animation_sampler* sampler = nullptr;
    int components = 3;
};

struct NodeTracks {
    Vec3 t{0, 0, 0}, s{1, 1, 1};
    Quat r{0, 0, 0, 1};
    Channel translation, rotation, scale;
};

Quat normalized(Quat q) {
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 1e-8f) {
        for (auto& c : q)
            c /= l;
    }
    return q;
}

Quat slerp(Quat a, Quat b, float u) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0) {
        for (auto& c : b)
            c = -c;
        d = -d;
    }
    if (d > 0.9995f) {
        Quat q{};
        for (int i = 0; i < 4; ++i)
            q[static_cast<std::size_t>(i)] =
                a[static_cast<std::size_t>(i)] + u * (b[static_cast<std::size_t>(i)] - a[static_cast<std::size_t>(i)]);
        return normalized(q);
    }
    const float th = std::acos(d), sn = std::sin(th);
    const float wa = std::sin((1 - u) * th) / sn, wb = std::sin(u * th) / sn;
    Quat q{};
    for (int i = 0; i < 4; ++i)
        q[static_cast<std::size_t>(i)] = wa * a[static_cast<std::size_t>(i)] + wb * b[static_cast<std::size_t>(i)];
    return q;
}

// A sampler's value at time `t` (glTF 2.0: LINEAR, STEP, CUBICSPLINE; rotations slerped).
std::array<float, 4> sample(const Channel& c, float t, bool rotation) {
    const cgltf_animation_sampler* s = c.sampler;
    const cgltf_size keys = s->input->count;
    std::array<float, 4> out{0, 0, 0, 1};
    auto value = [&](cgltf_size i) {
        std::array<float, 4> v{0, 0, 0, 1};
        cgltf_accessor_read_float(s->output, i, v.data(), static_cast<cgltf_size>(c.components));
        return v;
    };
    const bool cubic = s->interpolation == cgltf_interpolation_type_cubic_spline;
    auto key_value = [&](cgltf_size k) { return cubic ? value(3 * k + 1) : value(k); };
    float t0 = 0, t1 = 0;
    cgltf_accessor_read_float(s->input, 0, &t0, 1);
    cgltf_accessor_read_float(s->input, keys - 1, &t1, 1);
    if (keys == 1 || t <= t0)
        return key_value(0);
    if (t >= t1)
        return key_value(keys - 1);
    cgltf_size k = 0;
    float a = t0, b = t0;
    for (k = 0; k + 1 < keys; ++k) {
        cgltf_accessor_read_float(s->input, k, &a, 1);
        cgltf_accessor_read_float(s->input, k + 1, &b, 1);
        if (t >= a && t <= b)
            break;
    }
    const float span = b - a;
    const float u = span > 0 ? (t - a) / span : 0;
    if (s->interpolation == cgltf_interpolation_type_step)
        return key_value(k);
    if (cubic) {
        const auto p0 = value(3 * k + 1), m0 = value(3 * k + 2), p1 = value(3 * (k + 1) + 1), m1 = value(3 * (k + 1));
        const float u2 = u * u, u3 = u2 * u;
        for (int i = 0; i < c.components; ++i) {
            const auto x = static_cast<std::size_t>(i);
            out[x] = (2 * u3 - 3 * u2 + 1) * p0[x] + (u3 - 2 * u2 + u) * span * m0[x] + (-2 * u3 + 3 * u2) * p1[x] +
                     (u3 - u2) * span * m1[x];
        }
        if (rotation) {
            const auto q = normalized({out[0], out[1], out[2], out[3]});
            out = {q[0], q[1], q[2], q[3]};
        }
        return out;
    }
    const auto p0 = key_value(k), p1 = key_value(k + 1);
    if (rotation) {
        const auto q = slerp({p0[0], p0[1], p0[2], p0[3]}, {p1[0], p1[1], p1[2], p1[3]}, u);
        return {q[0], q[1], q[2], q[3]};
    }
    for (int i = 0; i < c.components; ++i) {
        const auto x = static_cast<std::size_t>(i);
        out[x] = p0[x] + u * (p1[x] - p0[x]);
    }
    return out;
}

Mat compose(const Vec3& t, const Quat& q, const Vec3& s) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                        2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                        2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
    return {r[0] * s[0], r[1] * s[0], r[2] * s[0], 0, r[3] * s[1], r[4] * s[1], r[5] * s[1], 0,
            r[6] * s[2], r[7] * s[2], r[8] * s[2], 0, t[0],        t[1],        t[2],        1};
}

}  // namespace

bool load_clip(const fs::path& file, const std::vector<std::string>& nodes, const AnimationPose& original, Clip* out,
               std::string* why, const std::string& wanted) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    const std::string ext = lower(u8(file.extension()));
    if (ext != ".gltf" && ext != ".glb")
        return fail("a clip is read from glTF or glb");
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string path = u8(file);
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
        return fail("not a glTF file");
    const std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
        return fail("its buffers cannot be read");
    if (data->animations_count == 0)
        return fail("the file has no animation");
    // the one named as wanted, else the first
    cgltf_size chosen = 0;
    bool named = wanted.empty();
    for (int pass = 0; pass < 2 && !named; ++pass) {
        for (cgltf_size i = 0; i < data->animations_count && !named; ++i) {
            const char* name = data->animations[i].name;
            if (name != nullptr && (pass == 0 ? wanted == name : lower(wanted) == lower(name))) {
                chosen = i;
                named = true;
            }
        }
    }
    const cgltf_animation& anim = data->animations[chosen];

    // the clip's nodes by name, at rest, and their channels
    std::map<std::string, NodeTracks> tracks;
    std::map<const cgltf_node*, std::string> names;
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        if (n.name == nullptr)
            continue;
        NodeTracks t;
        if (n.has_matrix) {
            // its matrix as T * R * S: the dump writes matrices
            Mat m;
            std::copy(n.matrix, n.matrix + 16, m.begin());
            t.t = {m[12], m[13], m[14]};
            Vec3 c[3];
            for (int k = 0; k < 3; ++k) {
                c[k] = {m[static_cast<std::size_t>(4 * k)], m[static_cast<std::size_t>(4 * k + 1)],
                        m[static_cast<std::size_t>(4 * k + 2)]};
                t.s[static_cast<std::size_t>(k)] = std::sqrt(c[k][0] * c[k][0] + c[k][1] * c[k][1] + c[k][2] * c[k][2]);
                if (t.s[static_cast<std::size_t>(k)] > 1e-8f) {
                    for (auto& v : c[k])
                        v /= t.s[static_cast<std::size_t>(k)];
                }
            }
            // rotation matrix to quaternion
            const float tr = c[0][0] + c[1][1] + c[2][2];
            Quat q{};
            if (tr > 0) {
                const float sq = std::sqrt(tr + 1.0f) * 2;
                q = {(c[1][2] - c[2][1]) / sq, (c[2][0] - c[0][2]) / sq, (c[0][1] - c[1][0]) / sq, 0.25f * sq};
            } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
                const float sq = std::sqrt(1.0f + c[0][0] - c[1][1] - c[2][2]) * 2;
                q = {0.25f * sq, (c[1][0] + c[0][1]) / sq, (c[2][0] + c[0][2]) / sq, (c[1][2] - c[2][1]) / sq};
            } else if (c[1][1] > c[2][2]) {
                const float sq = std::sqrt(1.0f + c[1][1] - c[0][0] - c[2][2]) * 2;
                q = {(c[1][0] + c[0][1]) / sq, 0.25f * sq, (c[2][1] + c[1][2]) / sq, (c[2][0] - c[0][2]) / sq};
            } else {
                const float sq = std::sqrt(1.0f + c[2][2] - c[0][0] - c[1][1]) * 2;
                q = {(c[2][0] + c[0][2]) / sq, (c[2][1] + c[1][2]) / sq, 0.25f * sq, (c[0][1] - c[1][0]) / sq};
            }
            t.r = normalized(q);
        } else {
            if (n.has_translation)
                t.t = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation)
                t.r = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
            if (n.has_scale)
                t.s = {n.scale[0], n.scale[1], n.scale[2]};
        }
        tracks[n.name] = t;
        names[&n] = n.name;
    }
    float start = 1e30f, end = -1e30f;
    for (cgltf_size i = 0; i < anim.channels_count; ++i) {
        const cgltf_animation_channel& ch = anim.channels[i];
        const auto found = names.find(ch.target_node);
        if (found == names.end() || ch.sampler == nullptr || ch.sampler->input->count == 0)
            continue;
        NodeTracks& t = tracks[found->second];
        Channel c{ch.sampler, 3};
        if (ch.target_path == cgltf_animation_path_type_translation)
            t.translation = c;
        else if (ch.target_path == cgltf_animation_path_type_rotation)
            t.rotation = {ch.sampler, 4};
        else if (ch.target_path == cgltf_animation_path_type_scale)
            t.scale = c;
        else
            continue;
        float a = 0, b = 0;
        cgltf_accessor_read_float(ch.sampler->input, 0, &a, 1);
        cgltf_accessor_read_float(ch.sampler->input, ch.sampler->input->count - 1, &b, 1);
        start = std::min(start, a);
        end = std::max(end, b);
    }

    Clip clip;
    clip.name = anim.name != nullptr ? anim.name : "";
    clip.animations_in_file = data->animations_count;
    const std::size_t frames = original.size();
    clip.pose = original;
    std::vector<int> moved(nodes.size(), 0);
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const auto t = tracks.find(nodes[n]);
        if (t == tracks.end())
            continue;
        const NodeTracks& nt = t->second;
        if (nt.translation.sampler == nullptr && nt.rotation.sampler == nullptr && nt.scale.sampler == nullptr)
            continue;
        moved[n] = 1;
        for (std::size_t f = 0; f < frames; ++f) {
            const float time =
                frames > 1 ? start + (end - start) * static_cast<float>(f) / static_cast<float>(frames - 1) : start;
            Vec3 tr = nt.t, sc = nt.s;
            Quat ro = nt.r;
            if (nt.translation.sampler != nullptr) {
                const auto v = sample(nt.translation, time, false);
                tr = {v[0], v[1], v[2]};
            }
            if (nt.rotation.sampler != nullptr) {
                const auto v = sample(nt.rotation, time, true);
                ro = normalized({v[0], v[1], v[2], v[3]});
            }
            if (nt.scale.sampler != nullptr) {
                const auto v = sample(nt.scale, time, false);
                sc = {v[0], v[1], v[2]};
            }
            if (n < clip.pose[f].size())
                clip.pose[f][n] = compose(tr, ro, sc);
        }
    }
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        (moved[n] ? clip.animated : clip.left_alone).push_back(nodes[n]);
        clip.moved.push_back(moved[n] != 0);
    }
    if (clip.animated.empty())
        return fail("the clip moves none of the game model's nodes (named as the dump names them)");
    *out = std::move(clip);
    return true;
}

}  // namespace studio
