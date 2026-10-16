#include "core/models/anim_replace.h"

#include "core/models/bones.h"
#include "core/models/clip_import.h"
#include "core/files/containers.h"
#include "core/files/file_write.h"
#include "core/files/lz.h"
#include "core/models/model_export.h"
#include "core/files/paths.h"

#include "khdays/assets/message.h"  // lz_decompress

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

namespace studio {

namespace {

namespace assets = khdays::assets;

const Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// The difference at element `i` of two local matrices, against the size of
// its column in `want` (at least 1): a column's rotation is rounded to 1/4096
// of its length, so a large scale carries a larger error with it.
float element_difference(const Mat& want, const Mat& got, std::size_t i) {
    const std::size_t c = 4 * (i / 4);
    const float length = std::sqrt(want[c] * want[c] + want[c + 1] * want[c + 1] + want[c + 2] * want[c + 2]);
    return std::fabs(got[i] - want[i]) / std::max(1.0f, length);
}

bool ends_with_z(const std::string& path) {
    return path.size() > 2 && path.compare(path.size() - 2, 2, ".z") == 0;
}

bool is_kaph(const std::vector<std::uint8_t>& d) {
    return d.size() >= 4 && d[0] == 'K' && d[1] == 'A' && d[2] == 'P' && d[3] == 'H';
}

std::vector<std::uint8_t> read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 4 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                           static_cast<std::uint32_t>(d[at + 3]) << 24)
                              : 0;
}

// The BCA0s of some data, found as the dump finds them (every 4 bytes, the
// magic and a size that fits).
std::vector<std::size_t> bca0s(const std::vector<std::uint8_t>& d) {
    std::vector<std::size_t> out;
    for (std::size_t at = 0; at + 0x18 <= d.size(); at += 4) {
        if (std::memcmp(&d[at], "BCA0", 4) == 0 && le32(d, at + 8) <= d.size() - at)
            out.push_back(at);
    }
    return out;
}

int label_entry(const std::string& label) {
    const auto hash = label.rfind('#');
    return hash == std::string::npos ? -1 : std::atoi(label.c_str() + hash + 1);
}

// The data `label` names in `file`, and how to put it back.
bool unwrap(const std::vector<std::uint8_t>& file, const std::string& label, P2File* p2,
            std::vector<std::uint8_t>* data, std::string* why) {
    const int entry = label_entry(label);
    if (entry >= 0) {
        if (!parse_p2(file, p2, why))
            return false;
        if (static_cast<std::size_t>(entry) >= p2->entries.size()) {
            if (why != nullptr)
                *why = "no entry " + std::to_string(entry);
            return false;
        }
        *data = p2->entries[static_cast<std::size_t>(entry)].data;
        return true;
    }
    if (ends_with_z(label)) {
        try {
            *data = assets::lz_decompress(file);
        } catch (const std::exception& e) {
            if (why != nullptr)
                *why = e.what();
            return false;
        }
        return true;
    }
    *data = file;
    return true;
}

// `lz`: the type of the file `unwrap` took it from (a ".z" file keeps it).
std::vector<std::uint8_t> wrap(const std::string& label, P2File& p2, std::uint8_t lz, std::vector<std::uint8_t> data) {
    const int entry = label_entry(label);
    if (entry >= 0) {
        p2.entries[static_cast<std::size_t>(entry)].data = std::move(data);
        return write_p2(p2);
    }
    return ends_with_z(label) ? lz_compress(lz, data) : data;
}

}  // namespace

std::string label_path(const std::string& label) {
    const auto hash = label.rfind('#');
    return hash == std::string::npos ? label : label.substr(0, hash);
}

AnimationPose animation_pose(const GameModel& g, const assets::SkeletalAnimation& a) {
    AnimationPose pose;
    for (int f = 0; f < a.frame_count; ++f) {
        const auto m = assets::sample_animation(a, static_cast<float>(f), g.model.object_matrices);
        pose.emplace_back(m.begin(), m.end());
        pose.back().resize(a.bones.size(), kIdentity);
    }
    return pose;
}

bool rebuild_with_animation(const std::vector<std::uint8_t>& file, const std::vector<std::uint8_t>& original,
                            const std::string& label, std::size_t offset, std::size_t index, const AnimationPose& pose,
                            const std::vector<bool>& keep, std::vector<std::uint8_t>* out, std::size_t* new_offset,
                            std::string* why) {
    P2File p2, original_p2;
    std::vector<std::uint8_t> data, original_data;
    if (!unwrap(original, label, &original_p2, &original_data, why))
        return false;
    if (!unwrap(file, label, &p2, &data, why))
        return false;
    std::vector<std::uint8_t> rebuilt;
    std::size_t at = 0;
    if (is_kaph(original_data)) {
        // the block by its order in the game's own file
        KaphFile kaph, original_kaph;
        if (!parse_kaph(original_data, &original_kaph, why))
            return false;
        std::size_t order = original_kaph.blocks.size();
        for (std::size_t b = 0; b < original_kaph.blocks.size(); ++b) {
            if (original_kaph.blocks[b].offset == offset)
                order = b;
        }
        if (order == original_kaph.blocks.size()) {
            if (why != nullptr)
                *why = "no KAPH block at the animation's place";
            return false;
        }
        if (!is_kaph(data) || !parse_kaph(data, &kaph, why) || kaph.blocks.size() != original_kaph.blocks.size()) {
            if (why != nullptr)
                *why = "the mod's copy of the file is not laid out as the game's";
            return false;
        }
        std::vector<std::uint8_t> bca0;
        if (!replace_bca0_animation(kaph.blocks[order].data, index, pose, keep, &original_kaph.blocks[order].data,
                                    &bca0, why)) {
            return false;
        }
        kaph.blocks[order].data = std::move(bca0);
        rebuilt = write_kaph(kaph);
        KaphFile written;
        if (!parse_kaph(rebuilt, &written, why))
            return false;
        at = written.blocks[order].offset;
    } else if (offset == 0) {
        if (!replace_bca0_animation(data, index, pose, keep, &original_data, &rebuilt, why))
            return false;
    } else {
        if (why != nullptr)
            *why = "the animation is inside data the tool cannot rebuild (not a KAPH)";
        return false;
    }
    *out = wrap(label, p2, original[0], std::move(rebuilt));
    if (new_offset != nullptr)
        *new_offset = at;
    return true;
}

bool read_animation(const std::vector<std::uint8_t>& file, const std::string& label, std::size_t offset,
                    std::size_t index, assets::SkeletalAnimation* out, std::string* why) {
    P2File p2;
    std::vector<std::uint8_t> data;
    if (!unwrap(file, label, &p2, &data, why))
        return false;
    if (offset + 0x10 > data.size()) {
        if (why != nullptr)
            *why = "no animation there";
        return false;
    }
    const std::size_t size = le32(data, offset + 8);
    if (offset + size > data.size()) {
        if (why != nullptr)
            *why = "the animation runs past its data";
        return false;
    }
    try {
        *out = assets::load_nsbca(&data[offset], size, index);
    } catch (const std::exception& e) {
        if (why != nullptr)
            *why = e.what();
        return false;
    }
    return true;
}

float pose_difference(const AnimationPose& pose, const assets::SkeletalAnimation& a) {
    float worst = 0;
    const std::vector<Mat> rest(a.bones.size(), kIdentity);
    for (std::size_t f = 0; f < pose.size(); ++f) {
        const auto m = assets::sample_animation(a, static_cast<float>(f), rest);
        for (std::size_t n = 0; n < pose[f].size() && n < m.size(); ++n) {
            for (std::size_t i = 0; i < 16; ++i) {
                worst = std::max(worst, element_difference(pose[f][n], m[n], i));
            }
        }
    }
    return worst;
}

namespace {

// Where pose_difference finds its largest difference, and how far that
// frame's original matrix is from a rotation times a scale (its columns'
// largest cosine between each other).
std::string where_worst(const AnimationPose& pose, const assets::SkeletalAnimation& a) {
    float worst = -1, want_at = 0, got_at = 0;
    std::size_t wf = 0, wn = 0, wi = 0;
    const std::vector<Mat> rest(a.bones.size(), kIdentity);
    for (std::size_t f = 0; f < pose.size(); ++f) {
        const auto m = assets::sample_animation(a, static_cast<float>(f), rest);
        for (std::size_t n = 0; n < pose[f].size() && n < m.size(); ++n) {
            for (std::size_t i = 0; i < 16; ++i) {
                const float d = element_difference(pose[f][n], m[n], i);
                if (d > worst) {
                    worst = d;
                    wf = f, wn = n, wi = i, want_at = pose[f][n][i], got_at = m[n][i];
                }
            }
        }
    }
    const Mat& o = pose[wf][wn];
    float skew = 0;
    for (int p = 0; p < 3; ++p) {
        for (int q = p + 1; q < 3; ++q) {
            float dot = 0, lp = 0, lq = 0;
            for (int k = 0; k < 3; ++k) {
                dot += o[static_cast<std::size_t>(4 * p + k)] * o[static_cast<std::size_t>(4 * q + k)];
                lp += o[static_cast<std::size_t>(4 * p + k)] * o[static_cast<std::size_t>(4 * p + k)];
                lq += o[static_cast<std::size_t>(4 * q + k)] * o[static_cast<std::size_t>(4 * q + k)];
            }
            if (lp > 1e-12f && lq > 1e-12f)
                skew = std::max(skew, std::fabs(dot) / std::sqrt(lp * lq));
        }
    }
    return " (frame " + std::to_string(wf) + " node " + std::to_string(wn) + " element " + std::to_string(wi) + ": " +
           std::to_string(want_at) + " read " + std::to_string(got_at) + "; its columns' cosine " +
           std::to_string(skew) + ")";
}

}  // namespace

std::vector<std::string> nodes_not_plain(const GameModel& g) {
    std::vector<std::string> out;
    if (g.model.skinning == nullptr)
        return out;  // its rest is its nodes' own matrices: nothing to compare
    for (std::size_t b = 0; b < g.rest.size() && b < g.model.object_matrices.size(); ++b) {
        const int p = g.parents[b];
        const Mat local = p >= 0 && static_cast<std::size_t>(p) < g.rest.size()
                              ? mat_mul(mat_inverse(g.rest[static_cast<std::size_t>(p)]), g.rest[b])
                              : g.rest[b];
        float worst = 0;
        for (std::size_t i = 0; i < 16; ++i)
            worst = std::max(worst, std::fabs(local[i] - g.model.object_matrices[b][i]));
        if (worst > 1e-3f)
            out.push_back(g.model.object_names[b]);
    }
    return out;
}

bool replace_animation_in_mod(const fs::path& nitrofs, const fs::path& mod_dir, const GameModel& g,
                              const GameAnimation& a, const fs::path& clip_file, AnimationReplacement* out,
                              std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    assets::SkeletalAnimation original_animation;
    if (!load_game_animation(nitrofs, a, &original_animation, why))
        return false;
    Clip clip;
    std::string reason;
    if (!load_clip(clip_file, g.model.object_names, animation_pose(g, original_animation), &clip, &reason,
                   export_animation_name(g, a))) {
        return fail(reason);
    }

    const std::string game_path = label_path(a.label);
    const std::vector<std::uint8_t> original = read_all(nitrofs / path_u8(game_path));
    if (original.empty())
        return fail("cannot read the game's " + game_path);
    const fs::path destination = mod_dir / "files" / path_u8(game_path);
    std::error_code ec;
    const bool on_copy = fs::is_regular_file(destination, ec);
    const std::vector<std::uint8_t> base = on_copy ? read_all(destination) : original;
    std::vector<std::uint8_t> rebuilt;
    std::size_t at = 0;
    std::vector<bool> keep(clip.moved.size());
    for (std::size_t n = 0; n < keep.size(); ++n)
        keep[n] = !clip.moved[n];
    if (!rebuild_with_animation(base, original, a.label, a.offset, a.index, clip.pose, keep, &rebuilt, &at, &reason)) {
        return fail(reason);
    }

    // what it reads back as: the nodes kept as the original has them, the clip's as the format holds them
    AnimationPose expected = clip.pose;
    for (auto& frame : expected) {
        for (std::size_t n = 0; n < frame.size(); ++n) {
            if (!keep[n])
                frame[n] = bca0_trs(frame[n]);
        }
    }
    float difference = 0;
    const WriteResult w = write_into_mod(
        mod_dir, destination,
        [&](const fs::path& temporary) -> std::string {
            std::ofstream o(temporary, std::ios::binary);
            o.write(reinterpret_cast<const char*>(rebuilt.data()), static_cast<std::streamsize>(rebuilt.size()));
            return o ? std::string() : "cannot write " + u8(temporary);
        },
        [&](const fs::path& temporary) -> std::string {
            assets::SkeletalAnimation read;
            std::string problem;
            if (!read_animation(read_all(temporary), a.label, at, a.index, &read, &problem)) {
                return "the written file does not read back: " + problem;
            }
            difference = pose_difference(expected, read);
            if (difference > 2e-3f)
                return "the written animation reads back different from the clip";
            return {};
        });
    if (!w.ok)
        return fail(w.why);
    AnimationReplacement r;
    r.written = destination;
    r.clip_name = clip.name;
    r.animated = clip.animated;
    r.left_alone = clip.left_alone;
    r.not_plain = nodes_not_plain(g);
    r.on_mod_copy = on_copy;
    r.difference = difference;
    *out = std::move(r);
    return true;
}

bool check_all_animations(const fs::path& nitrofs, const std::function<void(const std::string&)>& line) {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(nitrofs, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_regular_file(type_ec))
            files.push_back(u8(it->path().lexically_relative(nitrofs)));
    }
    std::sort(files.begin(), files.end());
    int animations = 0, problems = 0, not_rotations = 0, not_replaceable = 0, labels = 0, label_problems = 0;
    std::map<std::string, int> unchanged;  // KAPHs rebuilt with nothing changed, by outcome
    float worst_kept = 0, worst_new = 0;
    auto read_back = [](const std::vector<std::uint8_t>& bca0, std::size_t i, assets::SkeletalAnimation* a,
                        std::string* why) {
        try {
            *a = assets::load_nsbca(bca0.data(), bca0.size(), i);
        } catch (const std::exception& e) {
            *why = std::string("does not read back: ") + e.what();
            return false;
        }
        return true;
    };
    for (const auto& file : files) {
        const std::vector<std::uint8_t> whole = read_all(nitrofs / path_u8(file));
        for (const auto& data : game_file_data(nitrofs, file)) {
            const auto found = bca0s(data.bytes);
            if (found.empty())
                continue;
            ++labels;
            AnimationPose first_pose;
            for (const std::size_t at : found) {
                const std::vector<std::uint8_t> bca0(data.bytes.begin() + static_cast<std::ptrdiff_t>(at),
                                                     data.bytes.begin() +
                                                         static_cast<std::ptrdiff_t>(at + le32(data.bytes, at + 8)));
                for (std::size_t i = 0;; ++i) {
                    assets::SkeletalAnimation a;
                    try {
                        a = assets::load_nsbca(bca0.data(), bca0.size(), i);
                    } catch (const std::exception&) {
                        break;
                    }
                    ++animations;
                    const std::string where =
                        data.label + " @" + std::to_string(at) + " animation " + std::to_string(i);
                    AnimationPose pose;
                    const std::vector<Mat> rest(a.bones.size(), kIdentity);
                    for (int f = 0; f < a.frame_count; ++f) {
                        const auto m = assets::sample_animation(a, static_cast<float>(f), rest);
                        pose.emplace_back(m.begin(), m.end());
                    }
                    if (at == found.front() && i == 0)
                        first_pose = pose;
                    std::vector<std::uint8_t> replaced;
                    std::string why;
                    assets::SkeletalAnimation read;
                    // every node kept: it reads back as it was
                    if (!replace_bca0_animation(bca0, i, pose, std::vector<bool>(a.bones.size(), true), nullptr,
                                                &replaced, &why) ||
                        !read_back(replaced, i, &read, &why)) {
                        ++problems;
                        line(where + ", every node kept: " + why);
                        continue;
                    }
                    const float kept = pose_difference(pose, read);
                    worst_kept = std::max(worst_kept, kept);
                    if (kept > 1e-6f) {
                        ++problems;
                        line(where + ", every node kept: reads back different by " + std::to_string(kept));
                    }
                    // every node replaced by itself: it reads back as the format holds it
                    if (a.frame_count == 0 || a.frame_count > 0x0FFF) {
                        ++not_replaceable;
                        line(where + ": " + std::to_string(a.frame_count) +
                             " frames, not replaceable (a curve's 12 bits)");
                        continue;
                    }
                    AnimationPose nearest = pose;
                    bool all_rotations = true;
                    for (auto& f : nearest) {
                        for (auto& m : f) {
                            const Mat n = bca0_trs(m);
                            for (std::size_t e = 0; e < 16; ++e) {
                                if (element_difference(m, n, e) > 2e-3f)
                                    all_rotations = false;
                            }
                            m = n;
                        }
                    }
                    if (!all_rotations)
                        ++not_rotations;
                    if (!replace_bca0_animation(bca0, i, pose, {}, nullptr, &replaced, &why) ||
                        !read_back(replaced, i, &read, &why)) {
                        ++problems;
                        line(where + ", every node replaced: " + why);
                        continue;
                    }
                    const float d = pose_difference(nearest, read);
                    worst_new = std::max(worst_new, d);
                    if (d > 2e-3f) {
                        ++problems;
                        line(where + ", every node replaced: reads back different by " + std::to_string(d) +
                             where_worst(nearest, read));
                    }
                }
            }
            // its KAPH rebuilt with nothing changed; its file with its first animation half kept, half replaced
            P2File p2;
            std::vector<std::uint8_t> inner;
            std::string why;
            if (unwrap(whole, data.label, &p2, &inner, &why) && is_kaph(inner)) {
                KaphFile k;
                if (!parse_kaph(inner, &k, &why))
                    ++unchanged["KAPH not parsed"];
                else
                    ++unchanged[write_kaph(k) == inner ? "KAPH the same bytes" : "KAPH different bytes"];
            }
            if (first_pose.empty() || first_pose.size() > 0x0FFF)
                continue;
            std::vector<bool> keep(first_pose.front().size());
            AnimationPose expected = first_pose;
            for (std::size_t n = 0; n < keep.size(); ++n) {
                keep[n] = n % 2 == 0;
                if (!keep[n]) {
                    for (auto& f : expected)
                        f[n] = bca0_trs(f[n]);
                }
            }
            std::vector<std::uint8_t> rebuilt;
            std::size_t at = 0;
            assets::SkeletalAnimation read;
            bool ok = rebuild_with_animation(whole, whole, data.label, found.front(), 0, first_pose, keep, &rebuilt,
                                             &at, &why) &&
                      read_animation(rebuilt, data.label, at, 0, &read, &why);
            if (ok && pose_difference(expected, read) > 2e-3f) {
                ok = false;
                why = "reads back different";
            }
            if (!ok) {
                ++label_problems;
                line(data.label + ": not rebuilt: " + why);
            }
        }
    }
    for (const auto& [what, count] : unchanged)
        line(std::to_string(count) + " " + what + " when rebuilt unchanged");
    line(std::to_string(animations) + " animations: " + std::to_string(problems) +
         " problems; kept nodes' largest difference " + std::to_string(worst_kept) + ", replaced nodes' " +
         std::to_string(worst_new) + "; " + std::to_string(not_rotations) +
         " with frames that are not a rotation times a scale (written as the nearest), " +
         std::to_string(not_replaceable) + " not replaceable");
    line(std::to_string(labels) +
         " files' data rebuilt with an animation half replaced: " + std::to_string(label_problems) + " problems");
    return problems == 0 && label_problems == 0;
}

}  // namespace studio
