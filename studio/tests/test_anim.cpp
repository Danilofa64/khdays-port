// An animation replaced in a game file: LZ11 read back
// by the port's decoder, a P2 and a KAPH rebuilt with one piece changed and
// the rest as it was, a BCA0 animation replaced and read back by the port's
// reader within its fixed-point precision, a glTF clip sampled onto the
// game's nodes. Every file made here.
#include "test.h"

#include "core/models/anim_replace.h"
#include "core/models/bca0.h"
#include "core/models/clip_import.h"
#include "core/files/containers.h"
#include "core/files/lz.h"
#include "core/files/paths.h"

#include "khdays/assets/animation.h"
#include "khdays/assets/message.h"

#include <cmath>
#include <cstring>

using namespace studio;
namespace assets = khdays::assets;

namespace {

using Bytes = std::vector<std::uint8_t>;

void add16(Bytes& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v & 0xFF));
    d.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void add32(Bytes& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void put32(Bytes& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}
std::uint32_t get32(const Bytes& d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                      static_cast<std::uint32_t>(d[at + 3]) << 24);
}
void pad(Bytes& d, std::size_t a) {
    while (d.size() % a)
        d.push_back(0);
}

Bytes pattern(std::size_t n, std::uint32_t seed) {
    Bytes d(n);
    for (std::size_t i = 0; i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        // runs and repeats among noise, as real data has
        d[i] = (i / 37) % 3 == 0 ? static_cast<std::uint8_t>(i / 64) : static_cast<std::uint8_t>(seed >> 24);
    }
    return d;
}

// A J0AC of `frames` and `nodes`, every node unanimated (flags bit 0), its
// kept bytes `0x40 + node`.
Bytes plain_j0ac(int frames, int nodes) {
    Bytes j = {'J', 0, 'A', 'C'};
    add16(j, static_cast<std::uint32_t>(frames));
    add16(j, static_cast<std::uint32_t>(nodes));
    add32(j, 0x12345678);
    add32(j, 0);
    add32(j, 0);
    const std::size_t table = j.size();
    for (int n = 0; n < nodes; ++n)
        add16(j, 0);
    pad(j, 4);
    for (int n = 0; n < nodes; ++n) {
        j[table + 2 * static_cast<std::size_t>(n)] = static_cast<std::uint8_t>(j.size());
        add16(j, 1);
        j.push_back(static_cast<std::uint8_t>(0x40 + n));
        j.push_back(static_cast<std::uint8_t>(0x50 + n));
    }
    put32(j, 0x0C, static_cast<std::uint32_t>(j.size()));
    put32(j, 0x10, static_cast<std::uint32_t>(j.size()));
    return j;
}

// A BCA0 whose JNT0 dictionary names `names`, each a plain J0AC.
Bytes bca0(const std::vector<std::string>& names, int frames, int nodes) {
    const std::size_t count = names.size();
    Bytes jnt = {'J', 'N', 'T', '0'};
    add32(jnt, 0);  // size, below
    // the dictionary: revision, count, its size, a half-word, the entries' place
    const std::size_t dict = jnt.size();
    jnt.push_back(0);
    jnt.push_back(static_cast<std::uint8_t>(count));
    add16(jnt, 0);
    add16(jnt, 8);
    add16(jnt, static_cast<std::uint32_t>(8 + 4 * (count + 1)));
    for (std::size_t i = 0; i <= count; ++i)
        add32(jnt, 0x7F000000u + static_cast<std::uint32_t>(i));  // its tree
    const std::size_t entries = jnt.size();
    add16(jnt, 4);
    add16(jnt, static_cast<std::uint32_t>(4 + 4 * count));
    for (std::size_t i = 0; i < count; ++i)
        add32(jnt, 0);
    for (const auto& n : names) {
        char name[16] = {};
        std::memcpy(name, n.data(), std::min<std::size_t>(n.size(), 16));
        jnt.insert(jnt.end(), name, name + 16);
    }
    jnt[dict + 2] = static_cast<std::uint8_t>(jnt.size() - dict);
    pad(jnt, 4);
    for (std::size_t i = 0; i < count; ++i) {
        put32(jnt, entries + 4 + 4 * i, static_cast<std::uint32_t>(jnt.size()));
        const Bytes j = plain_j0ac(frames + static_cast<int>(i), nodes);
        jnt.insert(jnt.end(), j.begin(), j.end());
    }
    put32(jnt, 4, static_cast<std::uint32_t>(jnt.size()));
    Bytes b = {'B', 'C', 'A', '0'};
    add16(b, 0xFEFF);
    add16(b, 1);
    add32(b, 0);
    add16(b, 0x10);
    add16(b, 1);
    add32(b, 0x14);
    b.insert(b.end(), jnt.begin(), jnt.end());
    put32(b, 8, static_cast<std::uint32_t>(b.size()));
    return b;
}

// A KAPH with the BCA0s in section 0 and a stand-in model in section 7.
Bytes kaph(const std::vector<Bytes>& animations) {
    const std::size_t n = animations.size();
    Bytes k = {'K', 'A', 'P', 'H'};
    add32(k, 0);
    for (int s = 0; s < 8; ++s)
        add32(k, 0xFFFFFFFFu);
    const std::size_t section0 = k.size();
    put32(k, 8, static_cast<std::uint32_t>(section0));
    add32(k, static_cast<std::uint32_t>(n));
    for (std::size_t i = 0; i < 2 * n; ++i)
        add32(k, 0);
    const std::size_t section7 = k.size();
    put32(k, 8 + 4 * 7, static_cast<std::uint32_t>(section7));
    add32(k, 1);
    add32(k, 0);
    add32(k, 0);
    for (std::size_t i = 0; i < n; ++i) {
        pad(k, 0x20);
        put32(k, section0 + 4 + 4 * i, static_cast<std::uint32_t>(k.size()));
        put32(k, section0 + 4 + 4 * n + 4 * i, static_cast<std::uint32_t>(animations[i].size()));
        k.insert(k.end(), animations[i].begin(), animations[i].end());
    }
    pad(k, 0x20);
    const Bytes model = pattern(100, 7);
    put32(k, section7 + 4, static_cast<std::uint32_t>(k.size()));
    put32(k, section7 + 8, static_cast<std::uint32_t>(model.size()));
    k.insert(k.end(), model.begin(), model.end());
    pad(k, 0x20);
    return k;
}

// A P2 holding `entries`, the compressed ones LZ11 (as the game's own: zeros
// between entries, each on a sector, the file ending on one).
Bytes p2(const std::vector<std::pair<bool, Bytes>>& entries, std::uint8_t lz = 0x11) {
    const std::size_t n = entries.size();
    Bytes f = {'P', '2'};
    add16(f, static_cast<std::uint32_t>(n));
    add32(f, 0);
    add32(f, 0);
    add32(f, 0x200);
    const std::size_t sizes = f.size();
    for (std::size_t i = 0; i < (n + 1) / 2 * 2; ++i)
        add16(f, 0);
    const std::size_t desc = f.size();
    for (std::size_t i = 0; i < n; ++i)
        add32(f, 0);
    f.insert(f.end(), {'d', 'i', 'r'});  // other bytes before the data, kept
    f.resize(0x200, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const Bytes stored =
            entries[i].first && !entries[i].second.empty() ? lz_compress(lz, entries[i].second) : entries[i].second;
        f[sizes + 2 * i] = static_cast<std::uint8_t>((f.size() - 0x200) >> 9);
        put32(f, desc + 4 * i, static_cast<std::uint32_t>(stored.size()) | (entries[i].first ? 0x80000000u : 0));
        f.insert(f.end(), stored.begin(), stored.end());
        pad(f, 0x200);
    }
    return f;
}

Mat trs(float tx, float ty, float tz, float angle, float sx, float sy, float sz) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * sx, s * sx, 0, 0, -s * sy, c * sy, 0, 0, 0, 0, sz, 0, tx, ty, tz, 1};
}

bool close(const Mat& a, const Mat& b, float tolerance) {
    for (std::size_t i = 0; i < 16; ++i) {
        if (std::fabs(a[i] - b[i]) > tolerance)
            return false;
    }
    return true;
}

}  // namespace

TEST(lz11_reads_back_through_the_ports_decoder) {
    for (const std::size_t n :
         {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{17}, std::size_t{4096}, std::size_t{70000}}) {
        const Bytes data = pattern(n, static_cast<std::uint32_t>(n));
        const Bytes packed = lz11_compress(data);
        CHECK(!packed.empty() && packed[0] == 0x11);
        CHECK(assets::lz_decompress(packed) == data);
    }
    const Bytes same(100000, 0xAB);  // long runs: the 4-byte tokens
    const Bytes packed = lz11_compress(same);
    CHECK(packed.size() < 2000);
    CHECK(assets::lz_decompress(packed) == same);
}

TEST(p2_rebuilt_unchanged_is_byte_for_byte) {
    const Bytes file = p2({{false, pattern(300, 1)}, {true, pattern(5000, 2)}, {false, pattern(700, 3)}});
    P2File parsed;
    std::string why;
    CHECK(parse_p2(file, &parsed, &why));
    CHECK(parsed.entries.size() == 3);
    CHECK(parsed.entries[1].compressed && parsed.entries[1].data == pattern(5000, 2));
    CHECK(write_p2(parsed) == file);
}

TEST(lz10_reads_back_and_p2_entries_keep_their_type) {
    for (const std::size_t n : {std::size_t{0}, std::size_t{2}, std::size_t{18}, std::size_t{5000}}) {
        const Bytes data = pattern(n, static_cast<std::uint32_t>(n) + 3);
        const Bytes packed = lz_compress(0x10, data);
        CHECK(packed[0] == 0x10);
        CHECK(assets::lz_decompress(packed) == data);
    }
    // LZ10 entries and an empty one flagged compressed (the game's mi/wd archives have such)
    const Bytes file = p2({{true, pattern(900, 1)}, {true, {}}, {false, pattern(10, 2)}}, 0x10);
    P2File parsed;
    std::string why;
    CHECK(parse_p2(file, &parsed, &why));
    CHECK(parsed.entries[0].lz == 0x10 && parsed.entries[1].compressed && parsed.entries[1].data.empty());
    CHECK(write_p2(parsed) == file);
}

TEST(p2_with_a_bigger_entry_moves_the_rest) {
    const Bytes file = p2({{false, pattern(300, 1)}, {true, pattern(5000, 2)}, {false, pattern(700, 3)}});
    P2File parsed;
    CHECK(parse_p2(file, &parsed, nullptr));
    parsed.entries[0].data = pattern(3000, 9);  // grows past its sector
    const Bytes rebuilt = write_p2(parsed);
    CHECK(rebuilt.size() % 0x200 == 0);
    CHECK(rebuilt[0x10 + 8 + 12] == 'd');  // the bytes before the data (after both tables), kept
    P2File again;
    CHECK(parse_p2(rebuilt, &again, nullptr));
    CHECK(again.entries.size() == 3);
    CHECK(again.entries[0].data == pattern(3000, 9));
    CHECK(again.entries[1].compressed && again.entries[1].data == pattern(5000, 2));
    CHECK(again.entries[2].data == pattern(700, 3));
}

TEST(kaph_rebuilt_moves_later_blocks_and_patches_its_tables) {
    const Bytes file = kaph({pattern(200, 4)});
    KaphFile parsed;
    std::string why;
    CHECK(parse_kaph(file, &parsed, &why));
    CHECK(parsed.blocks.size() == 2);
    CHECK(write_kaph(parsed) == file);
    parsed.blocks[0].data = pattern(333, 5);
    const Bytes rebuilt = write_kaph(parsed);
    CHECK(rebuilt.size() % 0x20 == 0);
    KaphFile again;
    CHECK(parse_kaph(rebuilt, &again, nullptr));
    CHECK(again.blocks[0].offset == parsed.blocks[0].offset);
    CHECK(again.blocks[0].data == pattern(333, 5));
    CHECK(again.blocks[1].offset % 0x20 == 0 && again.blocks[1].offset > parsed.blocks[1].offset);
    CHECK(again.blocks[1].data == pattern(100, 7));
}

TEST(bca0_names_come_from_its_dictionary) {
    const Bytes b = bca0({"b_ro_A000", "b_ro_A001"}, 5, 3);
    const auto list = bca0_animations(b.data(), b.size());
    CHECK(list.size() == 2);
    CHECK(list[0].name == "b_ro_A000" && list[0].frames == 5 && list[0].nodes == 3);
    CHECK(list[1].name == "b_ro_A001" && list[1].frames == 6 && list[1].nodes == 3);
}

TEST(bca0_replaced_animation_reads_back_within_its_precision) {
    const Bytes original = bca0({"stand", "walk"}, 4, 3);
    AnimationPose pose;
    for (int f = 0; f < 5; ++f) {  // "walk": 5 frames
        const float t = static_cast<float>(f);
        pose.push_back({trs(1 + t, -2, 0.5f, 0.3f * t, 1, 1, 1), trs(0, 3, 0, -1.2f + 0.5f * t, 1.5f, 1, 0.75f),
                        trs(0.25f, 0, -t, 2.5f, 1, 1, -1)});  // the last one mirrored
    }
    Bytes out;
    std::string why;
    CHECK(replace_bca0_animation(original, 1, pose, {}, nullptr, &out, &why));
    // the other animation and the dictionary as they were
    const std::size_t entries = 0x14 + 8 + 8 + 4 * 3;
    CHECK(std::memcmp(out.data(), original.data(), 8) == 0);
    CHECK(std::memcmp(out.data() + 0x10, original.data() + 0x10, 8) == 0);                   // JNT0's place and magic
    CHECK(std::memcmp(out.data() + 0x1C, original.data() + 0x1C, entries + 4 - 0x1C) == 0);  // its dictionary
    CHECK(get32(out, entries + 8) != get32(original, entries + 8));
    const std::size_t stand = 0x14 + get32(original, entries + 4);
    CHECK(std::memcmp(out.data() + stand, original.data() + stand, 0x20) == 0);
    const auto kept = assets::load_nsbca(out.data(), out.size(), 0);
    CHECK(kept.frame_count == 4 && kept.bones.size() == 3 && !kept.bones[0].animated);

    const auto read = assets::load_nsbca(out.data(), out.size(), 1);
    CHECK(read.frame_count == 5 && read.bones.size() == 3);
    const std::vector<Mat> rest(3, Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    for (int f = 0; f < 5; ++f) {
        const auto m = assets::sample_animation(read, static_cast<float>(f), rest);
        for (std::size_t n = 0; n < 3; ++n)
            CHECK(close(m[n], pose[static_cast<std::size_t>(f)][n], 2e-3f));
    }
    // its kept bytes as the original's
    const std::size_t walk = 0x14 + get32(out, entries + 8);
    CHECK(get32(out, walk + 8) == 0x12345678);
    const std::size_t node1 = walk + (out[walk + 0x14 + 2] | out[walk + 0x14 + 3] << 8);
    CHECK(out[node1 + 2] == 0x41 && out[node1 + 3] == 0x51);
}

TEST(bca0_replacement_keeps_the_frame_and_node_counts) {
    const Bytes original = bca0({"stand"}, 4, 2);
    Bytes out;
    std::string why;
    const Mat one{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(!replace_bca0_animation(original, 0, AnimationPose(3, std::vector<Mat>(2, one)), {}, nullptr, &out, &why));
    CHECK(why.find("frame count") != std::string::npos);
    CHECK(!replace_bca0_animation(original, 0, AnimationPose(4, std::vector<Mat>(3, one)), {}, nullptr, &out, &why));
    CHECK(why.find("node count") != std::string::npos);
    CHECK(!replace_bca0_animation(original, 1, AnimationPose(4, std::vector<Mat>(2, one)), {}, nullptr, &out, &why));
}

TEST(animation_replaced_inside_kaph_p2_and_lz_files) {
    const Bytes animation = bca0({"idle"}, 3, 2);
    const Bytes model = kaph({animation});
    AnimationPose pose;
    for (int f = 0; f < 3; ++f)
        pose.push_back({trs(0, 0, 0, 0.4f * static_cast<float>(f), 1, 1, 1), trs(0, 1, 0, 0, 1, 1, 1)});
    struct Case {
        std::string label;
        Bytes file;
    };
    const std::vector<Case> cases = {
        {"ba/ch/xx/m.p", model},
        {"ba/ch/xx/m.p.z", lz11_compress(model)},
        {"ba/ch/xx/n.p.z", lz_compress(0x10, model)},
        {"ba/ch/xx/m.p2#1", p2({{false, pattern(64, 1)}, {true, model}, {false, pattern(64, 2)}})},
    };
    for (const auto& c : cases) {
        Bytes out;
        std::string why;
        std::size_t at = 0;
        CHECK(rebuild_with_animation(c.file, c.file, c.label, 0x40, 0, pose, {}, &out, &at, &why));
        CHECK(at == 0x40);
        assets::SkeletalAnimation read;
        CHECK(read_animation(out, c.label, at, 0, &read, &why));
        if (c.label.size() > 2 && c.label.substr(c.label.size() - 2) == ".z")
            CHECK(out[0] == c.file[0]);  // its LZ type
        const std::vector<Mat> rest(2, Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
        const auto m = assets::sample_animation(read, 2, rest);
        CHECK(close(m[0], pose[2][0], 2e-3f) && close(m[1], pose[2][1], 2e-3f));
    }
    // the P2's other entries as they were
    Bytes out;
    CHECK(rebuild_with_animation(cases[3].file, cases[3].file, cases[3].label, 0x40, 0, pose, {}, &out, nullptr,
                                 nullptr));
    P2File parsed;
    CHECK(parse_p2(out, &parsed, nullptr));
    CHECK(parsed.entries[0].data == pattern(64, 1) && parsed.entries[2].data == pattern(64, 2));
    CHECK(parsed.entries[1].compressed);
    CHECK(label_path("ba/ch/xx/m.p2#1") == "ba/ch/xx/m.p2");
    CHECK(label_path("ba/ch/xx/m.p.z") == "ba/ch/xx/m.p.z");
}

TEST(second_replacement_finds_its_block_after_an_earlier_one_grew) {
    const Bytes original = kaph({bca0({"a"}, 3, 2), bca0({"b"}, 4, 2)});
    KaphFile k;
    CHECK(parse_kaph(original, &k, nullptr));
    const std::size_t first = k.blocks[0].offset, second = k.blocks[1].offset;
    const auto pose = [](int frames, float turn) {
        AnimationPose p;
        for (int f = 0; f < frames; ++f) {
            p.push_back({trs(0, 0, 0, turn * static_cast<float>(f), 1, 1, 1), trs(1, 0, 0, 0, 1, 1, 1)});
        }
        return p;
    };
    Bytes once, twice;
    std::size_t at = 0;
    std::string why;
    CHECK(rebuild_with_animation(original, original, "m.p", first, 0, pose(3, 0.5f), {}, &once, &at, &why));
    CHECK(rebuild_with_animation(once, original, "m.p", second, 0, pose(4, -0.25f), {}, &twice, &at, &why));
    CHECK(at > second);  // the first block grew: the second moved
    assets::SkeletalAnimation a, b;
    CHECK(read_animation(twice, "m.p", first, 0, &a, &why));
    CHECK(read_animation(twice, "m.p", at, 0, &b, &why));
    CHECK(pose_difference(pose(3, 0.5f), a) < 2e-3f);
    CHECK(pose_difference(pose(4, -0.25f), b) < 2e-3f);
    // a copy laid out otherwise is refused, not guessed at
    CHECK(!rebuild_with_animation(kaph({bca0({"a"}, 3, 2)}), original, "m.p", second, 0, pose(4, 0), {}, &twice, &at,
                                  &why));
}

TEST(kept_nodes_keep_the_games_curves) {
    const auto pose = [](int frames, float turn, float y) {
        AnimationPose p;
        for (int f = 0; f < frames; ++f) {
            const float t = static_cast<float>(f);
            p.push_back({trs(0, y, 0, turn * t, 1, 1, 1), trs(1, 0, y, -turn * t, 1, 2, 1)});
        }
        return p;
    };
    // "the game's": every node sampled (pose A)
    Bytes game;
    std::string why;
    CHECK(replace_bca0_animation(bca0({"walk"}, 3, 2), 0, pose(3, 0.3f, 1), {}, nullptr, &game, &why));
    assets::SkeletalAnimation a, b;
    CHECK(read_animation(game, "x.bca", 0, 0, &a, &why));
    const std::vector<Mat> rest(2, Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    // node 0 kept, node 1 new (pose B)
    Bytes once;
    CHECK(replace_bca0_animation(game, 0, pose(3, -0.7f, 2), {true, false}, nullptr, &once, &why));
    CHECK(read_animation(once, "x.bca", 0, 0, &b, &why));
    for (int f = 0; f < 3; ++f) {
        const auto ma = assets::sample_animation(a, static_cast<float>(f), rest);
        const auto mb = assets::sample_animation(b, static_cast<float>(f), rest);
        CHECK(ma[0] == mb[0]);  // the very same curves
        CHECK(close(mb[1], pose(3, -0.7f, 2)[static_cast<std::size_t>(f)][1], 2e-3f));
    }
    // again on that copy, node 1 kept: it comes from the game's, not from pose B
    Bytes twice;
    CHECK(replace_bca0_animation(once, 0, pose(3, 0.9f, 3), {false, true}, &game, &twice, &why));
    CHECK(read_animation(twice, "x.bca", 0, 0, &b, &why));
    for (int f = 0; f < 3; ++f) {
        const auto ma = assets::sample_animation(a, static_cast<float>(f), rest);
        const auto mb = assets::sample_animation(b, static_cast<float>(f), rest);
        CHECK(mb[1] == ma[1]);
        CHECK(close(mb[0], pose(3, 0.9f, 3)[static_cast<std::size_t>(f)][0], 2e-3f));
    }
    // a file whose animation is not the game's (other counts) is refused
    Bytes other;
    CHECK(!replace_bca0_animation(bca0({"walk"}, 4, 2), 0, pose(3, 0, 0), {}, &game, &other, &why));
}

TEST(a_hidden_part_keeps_its_rotation) {
    // scaled to nothing along x (a part the animation hides), turned about z
    const Mat hidden = trs(0, 1, 0, 0.6f, 0, 1, 1);
    const Mat kept = bca0_trs(hidden);
    CHECK(close(kept, hidden, 1e-5f));
    const Mat mirrored = trs(2, 0, 0, -0.4f, 1, 1, -1);
    CHECK(close(bca0_trs(mirrored), mirrored, 1e-5f));
}

TEST(gltf_clip_sampled_onto_the_games_nodes) {
    // times 0, 1; the arm's rotation from none to 90 degrees about z
    Bytes buffer;
    auto addf = [&](float v) {
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        add32(buffer, u);
    };
    addf(0);
    addf(1);
    const float h = std::sqrt(0.5f);
    for (const float v : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, h, h})
        addf(v);
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (std::size_t i = 0; i < buffer.size(); i += 3) {
        const std::uint32_t v =
            static_cast<std::uint32_t>(buffer[i] << 16 | (i + 1 < buffer.size() ? buffer[i + 1] << 8 : 0) |
                                       (i + 2 < buffer.size() ? buffer[i + 2] : 0));
        b64 += digits[(v >> 18) & 63];
        b64 += digits[(v >> 12) & 63];
        b64 += i + 1 < buffer.size() ? digits[(v >> 6) & 63] : '=';
        b64 += i + 2 < buffer.size() ? digits[v & 63] : '=';
    }
    const std::string gltf =
        R"({"asset":{"version":"2.0"},"nodes":[{"name":"root","children":[1]},{"name":"arm","translation":[0,2,0]},)"
        R"({"name":"elsewhere"}],"buffers":[{"byteLength":40,"uri":"data:application/octet-stream;base64,)" +
        b64 +
        R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":32}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},)"
        R"({"bufferView":1,"componentType":5126,"count":2,"type":"VEC4"}],)"
        R"("animations":[{"name":"wave","samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}],)"
        R"("channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}]}]})";
    const fs::path file = studio_test::scratch("clip") / "wave.gltf";
    studio_test::write(file, gltf);

    const Mat root_motion = trs(5, 0, 0, 0, 1, 1, 1);
    const AnimationPose original(3, std::vector<Mat>{root_motion, trs(0, 2, 0, 0, 1, 1, 1)});
    Clip clip;
    std::string why;
    CHECK(load_clip(file, {"root", "arm"}, original, &clip, &why));
    CHECK(clip.name == "wave");
    CHECK(clip.animated == std::vector<std::string>{"arm"});
    CHECK(clip.left_alone == std::vector<std::string>{"root"});
    CHECK(clip.pose.size() == 3);
    const float pi = 3.14159265f;
    CHECK(close(clip.pose[0][1], trs(0, 2, 0, 0, 1, 1, 1), 1e-4f));
    CHECK(close(clip.pose[1][1], trs(0, 2, 0, pi / 4, 1, 1, 1), 1e-4f));
    CHECK(close(clip.pose[2][1], trs(0, 2, 0, pi / 2, 1, 1, 1), 1e-4f));
    CHECK(close(clip.pose[1][0], root_motion, 1e-6f));

    CHECK(!load_clip(file, {"other"}, original, &clip, &why));
    CHECK(why.find("none of the game") != std::string::npos);
}
