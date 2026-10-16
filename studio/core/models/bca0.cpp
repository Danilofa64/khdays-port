#include "core/models/bca0.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>

namespace studio {

namespace {

using Vec3 = std::array<float, 3>;

std::uint32_t le16(const std::uint8_t* d, std::size_t size, std::size_t at) {
    return at + 2 <= size ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8) : 0;
}
std::uint32_t le32(const std::uint8_t* d, std::size_t size, std::size_t at) {
    return at + 4 <= size ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                       static_cast<std::uint32_t>(d[at + 3]) << 24)
                          : 0;
}
void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = static_cast<std::uint8_t>(v & 0xFF);
    d[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}
void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}
void add16(std::vector<std::uint8_t>& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v & 0xFF));
    d.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void add32(std::vector<std::uint8_t>& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void pad4(std::vector<std::uint8_t>& d) {
    while (d.size() % 4)
        d.push_back(0);
}

// 20.12, held to its range: the game's event cameras use its largest value
// (524287) as a scale, which a float's rounding can carry just past it.
std::int32_t fx32(float v) {
    const double q = std::round(static_cast<double>(v) * 4096.0);
    return static_cast<std::int32_t>(std::clamp(q, -2147483648.0, 2147483647.0));
}

// A 1.12 value in 13 bits (two's complement), as basis_mat reads it.
std::uint32_t q13(float v) {
    const long q = std::clamp(std::lround(static_cast<double>(v) * 4096.0), -4096L, 4095L);
    return static_cast<std::uint32_t>(q) & 0x1FFF;
}

// animation.cpp basis_mat() read backwards: its first column a = (out1, out2,
// out3), its second b = (out4, out0, out5), out[0..4] the five half-words'
// top 13 bits in the order in[4], in[0..3], out5 their low three bits.
std::array<std::uint16_t, 5> encode_basis(const Vec3& a, const Vec3& b) {
    const std::uint32_t v[6] = {q13(b[1]), q13(a[0]), q13(a[1]), q13(a[2]), q13(b[0]), q13(b[2])};
    const std::uint32_t low[5] = {(v[5] >> 12) & 1, (v[5] >> 9) & 7, (v[5] >> 6) & 7, (v[5] >> 3) & 7, v[5] & 7};
    std::uint16_t input[5];
    for (int i = 0; i < 5; ++i)
        input[i] = static_cast<std::uint16_t>((v[i] << 3) | low[i]);
    return {input[1], input[2], input[3], input[4], input[0]};
}

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
bool normalize(Vec3& v) {
    const float l = std::sqrt(dot(v, v));
    if (l < 1e-6f)
        return false;
    for (auto& x : v)
        x /= l;
    return true;
}
// Some unit vector perpendicular to `v` (a unit vector).
Vec3 perpendicular(const Vec3& v) {
    Vec3 p = std::fabs(v[0]) < 0.9f ? cross(v, {1, 0, 0}) : cross(v, {0, 1, 0});
    normalize(p);
    return p;
}

struct Trs {
    Vec3 t{}, s{1, 1, 1};
    Vec3 a{1, 0, 0}, b{0, 1, 0};  // the rotation's first two columns; the third is a x b
};

// A local matrix as translation * rotation * scale (sample_animation's
// order). The rotation follows the first two columns (Gram-Schmidt); a
// column scaled to nothing (a part hidden by a zero scale) takes its
// direction from the others; each scale is its column along the rotation's,
// so a mirror shows in the third.
Trs decompose(const Mat& m) {
    Trs out;
    out.t = {m[12], m[13], m[14]};
    Vec3 c[3];
    bool ok[3];
    for (int k = 0; k < 3; ++k) {
        c[k] = {m[static_cast<std::size_t>(4 * k)], m[static_cast<std::size_t>(4 * k + 1)],
                m[static_cast<std::size_t>(4 * k + 2)]};
        Vec3 u = c[k];
        ok[k] = normalize(u);
    }
    Vec3 a{1, 0, 0}, b{0, 1, 0};
    Vec3 x = c[0], y = c[1], z = c[2];
    normalize(x);
    normalize(y);
    normalize(z);
    if (ok[0] && ok[1]) {
        a = x;
        for (int i = 0; i < 3; ++i)
            y[static_cast<std::size_t>(i)] -= dot(y, a) * a[static_cast<std::size_t>(i)];
        if (!normalize(y))
            y = ok[2] ? cross(z, a) : perpendicular(a);
        normalize(y);
        b = y;
    } else if (ok[0]) {
        a = x;
        b = ok[2] ? cross(z, a) : perpendicular(a);
        if (!normalize(b))
            b = perpendicular(a);
    } else if (ok[1]) {
        b = y;
        a = ok[2] ? cross(b, z) : perpendicular(b);
        if (!normalize(a))
            a = perpendicular(b);
    } else if (ok[2]) {
        a = perpendicular(z);
        b = cross(z, a);
    }
    const Vec3 r[3] = {a, b, cross(a, b)};
    for (int k = 0; k < 3; ++k)
        out.s[static_cast<std::size_t>(k)] = dot(c[k], r[k]);
    out.a = a;
    out.b = b;
    return out;
}

Mat compose(const Trs& p) {
    const Vec3 c = cross(p.a, p.b);
    return {p.a[0] * p.s[0], p.a[1] * p.s[0], p.a[2] * p.s[0], 0, p.b[0] * p.s[1], p.b[1] * p.s[1], p.b[2] * p.s[1], 0,
            c[0] * p.s[2],   c[1] * p.s[2],   c[2] * p.s[2],   0, p.t[0],          p.t[1],          p.t[2],          1};
}

// A J0AC as a BCA0 holds it: where it is, the end of its JNT0, its counts.
// Its tables and curve data are not inside it: the game's BCA0s share them
// between their animations, after the J0ACs (offsets from each J0AC).
struct J0ac {
    std::size_t at = 0, end = 0;  // in the BCA0; `end` its JNT0's
    std::size_t frames = 0, nodes = 0;
    std::size_t entry = 0;  // its dictionary entry's data word
};

bool find_j0ac(const std::uint8_t* d, std::size_t size, std::size_t index, J0ac* out, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    if (size < 0x14 || d[0] != 'B' || d[1] != 'C' || d[2] != 'A' || d[3] != '0')
        return fail("not a BCA0");
    if (le16(d, size, 0xE) != 1)
        return fail("a BCA0 with sections besides JNT0 (the game's have none)");
    const std::size_t jnt = le32(d, size, 0x10);
    const std::size_t jnt_size = le32(d, size, jnt + 4);
    if (jnt + jnt_size > size || d[jnt] != 'J' || d[jnt + 1] != 'N')
        return fail("no JNT0");
    const std::size_t dict = jnt + 8;
    const std::size_t count = d[dict + 1];
    const std::size_t entries = dict + le16(d, size, dict + 6);
    const std::size_t unit = le16(d, size, entries);
    if (index >= count || unit < 4)
        return fail("no such animation");
    J0ac j;
    j.entry = entries + 4 + unit * index;
    j.at = jnt + le32(d, size, j.entry);
    if (j.at + 0x14 > size || d[j.at] != 'J' || d[j.at + 2] != 'A' || d[j.at + 3] != 'C')
        return fail("no J0AC block");
    j.end = jnt + jnt_size;
    j.frames = le16(d, size, j.at + 4);
    j.nodes = le16(d, size, j.at + 6);
    if (j.at + 0x14 + 2 * j.nodes > j.end)
        return fail("a J0AC shorter than its node table");
    *out = j;
    return true;
}

// The size of a node's block in a J0AC and where its data offsets are in it
// (relative to the block), as animation.cpp read_animation() walks it.
bool node_block(const std::uint8_t* d, std::size_t size, std::size_t at, std::size_t* bytes,
                std::vector<std::size_t>* offsets) {
    const std::uint32_t flags = le16(d, size, at);
    std::size_t cur = 4;
    if ((flags & 1) == 0) {
        if (((flags >> 1) & 3) == 0) {
            for (int i = 0; i < 3; ++i) {
                if ((flags >> (3 + i)) & 1)
                    cur += 4;
                else
                    offsets->push_back(cur + 4), cur += 8;
            }
        }
        if (((flags >> 6) & 3) == 0) {
            if ((flags >> 8) & 1)
                cur += 4;
            else
                offsets->push_back(cur + 4), cur += 8;
        }
        if (((flags >> 9) & 3) == 0) {
            for (int i = 0; i < 3; ++i) {
                if ((flags >> (11 + i)) & 1)
                    cur += 8;
                else
                    offsets->push_back(cur + 4), cur += 8;
            }
        }
    }
    *bytes = cur;
    return at + cur <= size;
}

}  // namespace

std::vector<Bca0Animation> bca0_animations(const std::uint8_t* d, std::size_t size) {
    std::vector<Bca0Animation> out;
    if (size < 0x14 || d[0] != 'B' || d[1] != 'C' || d[2] != 'A' || d[3] != '0')
        return out;
    const std::size_t jnt = le32(d, size, 0x10);
    if (jnt + 0x10 > size || d[jnt] != 'J' || d[jnt + 1] != 'N')
        return out;
    const std::size_t dict = jnt + 8;
    const std::size_t count = d[dict + 1];
    const std::size_t entries = dict + le16(d, size, dict + 6);
    const std::size_t unit = le16(d, size, entries);
    const std::size_t names = entries + le16(d, size, entries + 2);
    if (unit < 4 || names + 16 * count > size)
        return out;
    for (std::size_t i = 0; i < count; ++i) {
        Bca0Animation a;
        const char* text = reinterpret_cast<const char*>(d + names + 16 * i);
        a.name.assign(text, strnlen(text, 16));
        const std::size_t block = jnt + le32(d, size, entries + 4 + unit * i);
        a.frames = static_cast<int>(le16(d, size, block + 4));
        a.nodes = static_cast<int>(le16(d, size, block + 6));
        out.push_back(std::move(a));
    }
    return out;
}

Mat bca0_trs(const Mat& m) {
    return compose(decompose(m));
}

bool replace_bca0_animation(const std::vector<std::uint8_t>& bca0, std::size_t index, const AnimationPose& pose,
                            const std::vector<bool>& keep, const std::vector<std::uint8_t>* original,
                            std::vector<std::uint8_t>* out, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    const std::vector<std::uint8_t>& source = original != nullptr ? *original : bca0;
    const std::uint8_t* d = source.data();
    const std::size_t size = source.size();
    J0ac target, old;
    if (!find_j0ac(bca0.data(), bca0.size(), index, &target, why))
        return false;
    if (!find_j0ac(d, size, index, &old, why))
        return false;
    if (target.frames != old.frames || target.nodes != old.nodes) {
        return fail("the animation in the file is not the game's (other frame or node counts)");
    }
    const std::size_t frames = old.frames, nodes = old.nodes;
    if (pose.size() != frames)
        return fail("the new animation's frame count is not the original's");
    for (const auto& f : pose) {
        if (f.size() != nodes)
            return fail("the new animation's node count is not the original's");
    }
    if (!keep.empty() && keep.size() != nodes)
        return fail("the nodes to keep are not the animation's nodes");
    auto kept = [&](std::size_t n) { return !keep.empty() && keep[n]; };
    bool any_new = false;
    for (std::size_t n = 0; n < nodes; ++n)
        any_new = any_new || !kept(n);
    if (any_new && (frames == 0 || frames > 0x0FFF)) {
        return fail("frame count " + std::to_string(frames) + " outside a curve's 12 bits");
    }

    // the kept nodes' blocks and the data they point at: from the first of
    // their data, pivot and basis tables to the end of the original's JNT0,
    // copied whole (how many samples a curve sampled every 2nd or 4th frame
    // holds is not what the port's reader counts)
    struct Kept {
        std::size_t from = 0, bytes = 0;   // its block in the original
        std::vector<std::size_t> offsets;  // its data offsets' places in the block
    };
    std::vector<Kept> kept_blocks(nodes);
    const std::size_t old_pivot = le32(d, size, old.at + 0x0C), old_basis = le32(d, size, old.at + 0x10);
    std::size_t region = std::min(old_pivot, old_basis);  // from old.at
    bool any_kept = false;
    for (std::size_t n = 0; n < nodes; ++n) {
        if (!kept(n))
            continue;
        any_kept = true;
        Kept& k = kept_blocks[n];
        k.from = old.at + le16(d, size, old.at + 0x14 + 2 * n);
        if (!node_block(d, size, k.from, &k.bytes, &k.offsets))
            return fail("a node's curves run past the file");
        for (const std::size_t o : k.offsets)
            region = std::min<std::size_t>(region, le32(d, size, k.from + o));
    }
    if (any_kept && old.at + std::max(region, old_basis) > old.end) {
        return fail("the original's curves point past its JNT0");
    }
    const std::size_t region_bytes = any_kept ? old.end - old.at - region : 0;
    // the new rotations go after the original's basis table, which the copy ends with
    const std::size_t basis_base = any_kept ? (old.end - old.at - old_basis + 9) / 10 : 0;

    // the new nodes' rotations, each once
    std::map<std::array<std::uint16_t, 5>, std::uint32_t> basis_index;
    std::vector<std::array<std::uint16_t, 5>> basis;
    std::vector<std::vector<Trs>> trs(nodes, std::vector<Trs>(frames));
    std::vector<std::vector<std::uint32_t>> rot(nodes, std::vector<std::uint32_t>(frames));
    for (std::size_t n = 0; n < nodes; ++n) {
        if (kept(n))
            continue;
        for (std::size_t f = 0; f < frames; ++f) {
            trs[n][f] = decompose(pose[f][n]);
            const auto e = encode_basis(trs[n][f].a, trs[n][f].b);
            auto it = basis_index.find(e);
            if (it == basis_index.end()) {
                it = basis_index.emplace(e, static_cast<std::uint32_t>(basis_base + basis.size())).first;
                basis.push_back(e);
            }
            rot[n][f] = it->second;
        }
    }
    if (basis_base + basis.size() > 0x8000) {
        return fail("more rotations than a basis index holds (32768), with the file's own");
    }

    // the J0AC: header, node table, node blocks, the new samples, the
    // original's data copied, the new rotations after its basis table
    std::vector<std::uint8_t> j;
    j.insert(j.end(), {'J', 0, 'A', 'C'});
    add16(j, static_cast<std::uint32_t>(frames));
    add16(j, static_cast<std::uint32_t>(nodes));
    add32(j, le32(d, size, old.at + 8));  // kept as the original's
    add32(j, 0);                          // pivots: below
    add32(j, 0);                          // bases: below
    const std::size_t table = j.size();
    for (std::size_t n = 0; n < nodes; ++n)
        add16(j, 0);
    pad4(j);
    std::vector<std::size_t> data_fields;                      // the new nodes' data offsets, in order
    std::vector<std::pair<std::size_t, std::uint32_t>> moved;  // a kept node's data offset: its place, its value
    // start 0, end the frame count (as all 109,205 of the game's every-frame curves), 32-bit data
    const std::uint32_t curve = static_cast<std::uint32_t>(frames) << 16;
    for (std::size_t n = 0; n < nodes; ++n) {
        if (j.size() > 0xFFFF)
            return fail("node blocks past a node table's 16-bit offsets");
        put16(j, table + 2 * n, static_cast<std::uint32_t>(j.size()));
        if (kept(n)) {
            const Kept& k = kept_blocks[n];
            const std::size_t at = j.size();
            j.insert(j.end(), d + k.from, d + k.from + k.bytes);
            for (const std::size_t o : k.offsets)
                moved.emplace_back(at + o, le32(d, size, k.from + o));
            continue;
        }
        const std::size_t old_node = old.at + le16(d, size, old.at + 0x14 + 2 * n);
        add16(j, 0);                   // flags: translation, rotation and scale each sampled
        j.push_back(d[old_node + 2]);  // kept as the original's
        j.push_back(d[old_node + 3]);
        for (int k = 0; k < 7; ++k) {  // tx, ty, tz, r, sx, sy, sz
            add32(j, curve);
            data_fields.push_back(j.size());
            add32(j, 0);
        }
    }
    pad4(j);
    std::size_t field = 0;
    for (std::size_t n = 0; n < nodes; ++n) {
        if (kept(n))
            continue;
        for (int k = 0; k < 3; ++k) {
            put32(j, data_fields[field++], static_cast<std::uint32_t>(j.size()));
            for (std::size_t f = 0; f < frames; ++f)
                add32(j, static_cast<std::uint32_t>(fx32(trs[n][f].t[static_cast<std::size_t>(k)])));
        }
        put32(j, data_fields[field++], static_cast<std::uint32_t>(j.size()));
        for (std::size_t f = 0; f < frames; ++f)
            add16(j, rot[n][f]);
        pad4(j);
        for (int k = 0; k < 3; ++k) {
            put32(j, data_fields[field++], static_cast<std::uint32_t>(j.size()));
            for (std::size_t f = 0; f < frames; ++f) {
                const float s = trs[n][f].s[static_cast<std::size_t>(k)];
                add32(j, static_cast<std::uint32_t>(fx32(s)));
                add32(j, static_cast<std::uint32_t>(fx32(std::fabs(s) > 1e-8f ? 1.0f / s : 0.0f)));
            }
        }
    }
    if (any_kept) {
        while (j.size() % 4 != region % 4)
            j.push_back(0);  // the copy as aligned as the original
        const std::size_t copy = j.size();
        j.insert(j.end(), d + old.at + region, d + old.at + region + region_bytes);
        auto to_copy = [&](std::size_t value) { return static_cast<std::uint32_t>(copy + value - region); };
        for (const auto& [place, value] : moved)
            put32(j, place, to_copy(value));
        put32(j, 0x0C, to_copy(old_pivot));
        put32(j, 0x10, to_copy(old_basis));
        j.resize(copy - region + old_basis + 10 * basis_base, 0);  // up to the new rotations' first index
    } else {
        put32(j, 0x0C, static_cast<std::uint32_t>(j.size()));  // no pivot rotations
        put32(j, 0x10, static_cast<std::uint32_t>(j.size()));
    }
    for (const auto& e : basis) {
        for (const std::uint16_t w : e)
            add16(j, w);
    }
    pad4(j);

    // appended to JNT0, its dictionary entry pointed at it
    const std::uint8_t* t = bca0.data();
    const std::size_t jnt = le32(t, bca0.size(), 0x10);
    const std::size_t jnt_size = le32(t, bca0.size(), jnt + 4);
    std::vector<std::uint8_t> o(bca0.begin(), bca0.begin() + static_cast<std::ptrdiff_t>(jnt + jnt_size));
    while ((o.size() - jnt) % 4)
        o.push_back(0);
    const std::size_t at = o.size();
    o.insert(o.end(), j.begin(), j.end());
    put32(o, target.entry, static_cast<std::uint32_t>(at - jnt));
    put32(o, jnt + 4, static_cast<std::uint32_t>(o.size() - jnt));
    put32(o, 8, static_cast<std::uint32_t>(o.size()));
    *out = std::move(o);
    return true;
}

}  // namespace studio
