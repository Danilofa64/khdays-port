#include "core/rooms/worlds.h"

#include "core/files/paths.h"
#include "core/rooms/room_collision.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace studio {

namespace {

std::uint32_t le16(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 2 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8) : 0;
}
std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 4 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                           static_cast<std::uint32_t>(d[at + 3]) << 24)
                              : 0;
}
void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = static_cast<std::uint8_t>(v);
    d[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}
void add16(std::vector<std::uint8_t>& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v));
    d.push_back(static_cast<std::uint8_t>(v >> 8));
}
void add32(std::vector<std::uint8_t>& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// Collision header fields.
constexpr std::size_t kHeader = 0xb0;
constexpr std::size_t kRecords18 = 0x76, kPointers = 0x78, kNodes = 0x7a, kCounts = 0x7c, kNamedCount = 0x82;
constexpr std::size_t kRoot = 0x84, kField94 = 0x94, kPointerTable = 0x98, kTree = 0x9c, kLists = 0xa0, kNamed = 0xac;
constexpr std::size_t kFaceSize[3] = {0x88, 0x84, 0x84};

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

std::string NamedRecord::text() const {
    std::size_t n = 0;
    while (n < name.size() && name[n] != '\0')
        ++n;
    return std::string(name.data(), n);
}

std::int32_t RoomCollision::root(int word) const {
    return static_cast<std::int32_t>(le32(header, kRoot + 4 * static_cast<std::size_t>(word)));
}

void RoomCollision::set_root(std::int32_t centre_x, std::int32_t centre_z, std::int32_t size, std::int32_t smallest) {
    if (header.size() < kHeader)
        header.resize(kHeader, 0);
    put32(header, kRoot, static_cast<std::uint32_t>(centre_x));
    put32(header, kRoot + 4, static_cast<std::uint32_t>(centre_z));
    put32(header, kRoot + 8, static_cast<std::uint32_t>(size));
    put32(header, kRoot + 12, static_cast<std::uint32_t>(smallest));
}

std::vector<Marker> markers(const RoomCollision& c) {
    std::vector<Marker> out;
    for (std::size_t at = 0; at + 0x18 <= c.records18.size(); at += 0x18) {
        Marker m;
        std::size_t n = 0;
        while (n < 8 && c.records18[at + n] != 0)
            ++n;
        m.name.assign(reinterpret_cast<const char*>(&c.records18[at]), n);
        for (std::size_t a = 0; a < 3; ++a)
            m.place[a] = static_cast<std::int32_t>(le32(c.records18, at + 8 + 4 * a));
        m.extra = static_cast<std::int32_t>(le32(c.records18, at + 0x14));
        out.push_back(std::move(m));
    }
    return out;
}

void set_markers(RoomCollision* c, const std::vector<Marker>& m) {
    for (std::size_t k = 0; k < m.size() && 0x18 * (k + 1) <= c->records18.size(); ++k) {
        const std::size_t at = 0x18 * k;
        for (std::size_t a = 0; a < 3; ++a)
            put32(c->records18, at + 8 + 4 * a, static_cast<std::uint32_t>(m[k].place[a]));
        put32(c->records18, at + 0x14, static_cast<std::uint32_t>(m[k].extra));
    }
}

bool parse_collision(const std::vector<std::uint8_t>& d, RoomCollision* out, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    if (d.size() < kHeader)
        return fail("shorter than a collision header");
    RoomCollision c;
    c.header.assign(d.begin(), d.begin() + kHeader);
    if (le16(d, kPointers) != 0)
        return fail("a pointer table (none in the game's rooms; not read)");
    const std::size_t n18 = le16(d, kRecords18), nodes = le16(d, kNodes), named = le16(d, kNamedCount);
    const std::size_t counts[3] = {le16(d, kCounts), le16(d, kCounts + 2), le16(d, kCounts + 4)};
    const std::size_t at18 = le32(d, kField94), tree = le32(d, kTree), named_at = le32(d, kNamed);
    auto inside = [&](std::size_t at, std::size_t bytes) { return at <= d.size() && bytes <= d.size() - at; };
    if (!inside(at18, n18 * 0x18) || !inside(tree, nodes * 0x20) || !inside(named_at, named * 0x14)) {
        return fail("a section outside the data");
    }
    c.records18.assign(d.begin() + static_cast<std::ptrdiff_t>(at18),
                       d.begin() + static_cast<std::ptrdiff_t>(at18 + n18 * 0x18));
    for (std::size_t k = 0; k < nodes; ++k) {
        const std::size_t o = tree + 0x20 * k;
        RoomNode n;
        n.flags = static_cast<std::uint16_t>(le16(d, o));
        for (int l = 0; l < 3; ++l)
            n.first[static_cast<std::size_t>(l)] =
                static_cast<std::int16_t>(le16(d, o + 2 + 2 * static_cast<std::size_t>(l)));
        n.objects = le32(d, o + 8);
        n.parent = static_cast<std::int32_t>(le32(d, o + 0xc));
        for (std::size_t ch = 0; ch < 4; ++ch)
            n.children[ch] = static_cast<std::int32_t>(le32(d, o + 0x10 + 4 * ch));
        c.nodes.push_back(n);
    }
    for (int l = 0; l < 3; ++l) {
        const std::size_t base = le32(d, kLists + 4 * static_cast<std::size_t>(l)), size = kFaceSize[l];
        if (!inside(base, counts[l] * size))
            return fail("a face list outside the data");
        for (std::size_t k = 0; k < counts[l]; ++k) {
            const std::size_t o = base + size * k;
            RoomFace f;
            for (std::size_t b = 0; b < 4; ++b)
                f.bounds[b] = static_cast<std::int32_t>(le32(d, o + 4 * b));
            f.flags = static_cast<std::uint16_t>(le16(d, o + 0x10));
            f.corners = static_cast<std::uint16_t>(le16(d, o + 0x12));
            std::copy(d.begin() + static_cast<std::ptrdiff_t>(o + 0x14),
                      d.begin() + static_cast<std::ptrdiff_t>(o + 0x50), f.planes.begin());
            for (std::size_t v = 0; v < 4; ++v) {
                for (std::size_t a = 0; a < 3; ++a)
                    f.v[v][a] = static_cast<std::int32_t>(le32(d, o + 0x50 + 12 * v + 4 * a));
            }
            for (std::size_t s = 0; s < 4; ++s)
                f.slots[s] = d[o + 0x80 + s];
            if (size == 0x88)
                f.extra = le32(d, o + 0x84);
            c.faces[static_cast<std::size_t>(l)].push_back(f);
        }
    }
    for (std::size_t k = 0; k < named; ++k) {
        const std::size_t o = named_at + 0x14 * k;
        NamedRecord r;
        std::memcpy(r.name.data(), &d[o], 8);
        r.kept = le32(d, o + 8);
        for (std::size_t t = 0; t < 4; ++t)
            r.tags[t] = d[o + 0xc + t];
        r.pointer = le32(d, o + 0x10);
        c.named.push_back(r);
    }
    *out = std::move(c);
    return true;
}

std::vector<std::uint8_t> write_collision(const RoomCollision& c) {
    // header, the 0x18-byte records, tree, grounds, walls, ceilings, named records
    std::vector<std::uint8_t> d = c.header;
    d.resize(kHeader, 0);
    put16(d, kRecords18, static_cast<std::uint32_t>(c.records18.size() / 0x18));
    put16(d, kPointers, 0);
    put16(d, kNodes, static_cast<std::uint32_t>(c.nodes.size()));
    for (std::size_t l = 0; l < 3; ++l)
        put16(d, kCounts + 2 * l, static_cast<std::uint32_t>(c.faces[l].size()));
    put16(d, kNamedCount, static_cast<std::uint32_t>(c.named.size()));
    put32(d, kField94, kHeader);
    d.insert(d.end(), c.records18.begin(), c.records18.end());
    put32(d, kPointerTable, static_cast<std::uint32_t>(d.size()));  // empty, where the tree starts
    put32(d, kTree, static_cast<std::uint32_t>(d.size()));
    for (const auto& n : c.nodes) {
        add16(d, n.flags);
        for (const auto f : n.first)
            add16(d, static_cast<std::uint16_t>(f));
        add32(d, n.objects);
        add32(d, static_cast<std::uint32_t>(n.parent));
        for (const auto ch : n.children)
            add32(d, static_cast<std::uint32_t>(ch));
    }
    for (std::size_t l = 0; l < 3; ++l) {
        put32(d, kLists + 4 * l, static_cast<std::uint32_t>(d.size()));
        for (const auto& f : c.faces[l]) {
            for (const auto b : f.bounds)
                add32(d, static_cast<std::uint32_t>(b));
            add16(d, f.flags);
            add16(d, f.corners);
            d.insert(d.end(), f.planes.begin(), f.planes.end());
            for (const auto& v : f.v) {
                for (const auto a : v)
                    add32(d, static_cast<std::uint32_t>(a));
            }
            d.insert(d.end(), f.slots.begin(), f.slots.end());
            if (kFaceSize[l] == 0x88)
                add32(d, f.extra);
        }
    }
    put32(d, kNamed, static_cast<std::uint32_t>(d.size()));
    for (const auto& r : c.named) {
        d.insert(d.end(), r.name.begin(), r.name.end());
        add32(d, r.kept);
        d.insert(d.end(), r.tags.begin(), r.tags.end());
        add32(d, r.pointer);
    }
    return d;
}

std::string world_path(const std::string& code) {
    return "mi/wd/wd_" + code;
}

std::vector<std::string> world_codes(const fs::path& nitrofs) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(nitrofs / "mi" / "wd", ec)) {
        const std::string name = u8(e.path().filename());
        if (name.rfind("wd_", 0) == 0 && name.size() > 3)
            out.push_back(name.substr(3));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool parse_world(const std::vector<std::uint8_t>& file, const std::string& code, World* out, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = world_path(code) + ": " + reason;
        return false;
    };
    World w;
    w.code = code;
    std::string problem;
    if (!parse_p2(file, &w.archive, &problem))
        return fail(problem);
    if (w.archive.entries.size() < 2)
        return fail("fewer than two entries");
    const auto& t = w.archive.entries[0].data;
    if (t.empty())
        return fail("an empty room table");
    const std::size_t count = t[0];
    for (std::size_t r = 0; r < count; ++r) {
        const std::size_t off = le32(t, 4 + 4 * r);
        if (off + 0x20 > t.size())
            return fail("room " + std::to_string(r) + " outside its table");
        WorldRoom room;
        room.index = static_cast<int>(r);
        room.id = static_cast<std::int8_t>(t[off]);
        const std::size_t links = t[off + 1], objects = t[off + 2];
        room.collision_member = t[off + 3];
        room.budget = static_cast<std::int32_t>(le32(t, off + 0x14));
        const std::size_t rows = off + le32(t, off + 0x1c);
        if (off + 0x20 + 4 * objects > t.size() || rows + 8 * links > t.size())
            return fail("room " + std::to_string(r) + "'s rows outside its table");
        for (std::size_t k = 0; k < links; ++k) {
            const std::size_t o = rows + 8 * k;
            LinkRow l;
            l.key = t[o];
            l.target = t[o + 1];
            l.pad = t[o + 2];
            l.gate_state = static_cast<std::int8_t>(t[o + 3]);
            l.flags = static_cast<std::uint16_t>(le16(t, o + 4));
            l.radius = static_cast<std::int16_t>(le16(t, o + 6));
            room.links.push_back(l);
        }
        for (std::size_t k = 0; k < objects; ++k) {
            const std::size_t o = off + le32(t, off + 0x20 + 4 * k);
            if (o + 4 > t.size())
                return fail("room " + std::to_string(r) + "'s object outside its table");
            RoomObject ob;
            ob.flags = t[o];
            ob.member = t[o + 1];
            ob.rest = static_cast<std::uint16_t>(le16(t, o + 2));
            room.objects.push_back(ob);
        }
        w.rooms.push_back(std::move(room));
    }
    const auto& g = w.archive.entries[1].data;
    const std::size_t models = le32(g, 0);
    if (4 + models * 0x90 > g.size())
        return fail("gate models outside their entry");
    for (std::size_t k = 0; k < models; ++k) {
        const std::size_t o = 4 + 0x90 * k;
        GateModel m;
        m.key = static_cast<std::int8_t>(g[o]);
        for (std::size_t p = 0; p < 3; ++p)
            m.pad[p] = g[o + 1 + p];
        for (std::size_t p = 0; p < 5; ++p) {
            const std::size_t q = o + 4 + 0x1c * p;
            GateModelPart& part = m.parts[p];
            part.kind = static_cast<std::int8_t>(g[q]);
            part.member = g[q + 1];
            part.turn = static_cast<std::uint16_t>(le16(g, q + 2));
            for (std::size_t a = 0; a < 3; ++a)
                part.position[a] = static_cast<std::int32_t>(le32(g, q + 4 + 4 * a));
            for (std::size_t a = 0; a < 3; ++a)
                part.scale[a] = static_cast<std::int32_t>(le32(g, q + 0x10 + 4 * a));
        }
        w.gate_models.push_back(m);
    }
    w.gate_tail.assign(g.begin() + static_cast<std::ptrdiff_t>(4 + models * 0x90), g.end());
    *out = std::move(w);
    return true;
}

bool load_world(const fs::path& nitrofs, const std::string& code, World* out, std::string* why) {
    const auto file = read_file(nitrofs / path_u8(world_path(code)));
    if (file.empty()) {
        if (why != nullptr)
            *why = "no file " + world_path(code);
        return false;
    }
    return parse_world(file, code, out, why);
}

std::vector<std::uint8_t> write_gate_models(const World& w) {
    std::vector<std::uint8_t> g;
    add32(g, static_cast<std::uint32_t>(w.gate_models.size()));
    for (const auto& m : w.gate_models) {
        g.push_back(static_cast<std::uint8_t>(m.key));
        g.insert(g.end(), m.pad.begin(), m.pad.end());
        for (const auto& part : m.parts) {
            g.push_back(static_cast<std::uint8_t>(part.kind));
            g.push_back(part.member);
            add16(g, part.turn);
            for (const auto a : part.position)
                add32(g, static_cast<std::uint32_t>(a));
            for (const auto a : part.scale)
                add32(g, static_cast<std::uint32_t>(a));
        }
    }
    g.insert(g.end(), w.gate_tail.begin(), w.gate_tail.end());
    return g;
}

std::vector<std::uint8_t> write_world(const World& w, const std::map<int, std::vector<std::uint8_t>>& collisions) {
    P2File p2 = w.archive;
    p2.entries[1].data = write_gate_models(w);
    for (const auto& [member, data] : collisions) {
        if (member >= 0 && static_cast<std::size_t>(member) < p2.entries.size())
            p2.entries[static_cast<std::size_t>(member)].data = data;
    }
    return write_p2(p2);
}

bool check_all_worlds(const fs::path& nitrofs, const std::function<void(const std::string&)>& line) {
    int worlds = 0, rooms = 0, problems = 0, trees_ok = 0, rebuilt_ok = 0;
    int plane_normal_off = 0, planes_off = 0, faces_seen = 0;
    std::int64_t plane_distance_off = 0;
    for (const auto& code : world_codes(nitrofs)) {
        World w;
        std::string why;
        if (!load_world(nitrofs, code, &w, &why)) {
            line(why);
            ++problems;
            continue;
        }
        ++worlds;
        // the archive again, nothing changed: every entry the same bytes as the game uses
        const auto again = write_world(w, {});
        P2File back;
        if (!parse_p2(again, &back, &why) || back.entries.size() != w.archive.entries.size()) {
            line(world_path(code) + ": rebuilt, not read back: " + why);
            ++problems;
            continue;
        }
        for (std::size_t e = 0; e < back.entries.size(); ++e) {
            if (back.entries[e].data != w.archive.entries[e].data) {
                line(world_path(code) + " entry " + std::to_string(e) + ": rebuilt differently");
                ++problems;
            }
        }
        for (const auto& room : w.rooms) {
            const auto& data = w.archive.entries[static_cast<std::size_t>(room.collision_member)].data;
            if (data.empty())
                continue;
            ++rooms;
            const std::string where =
                world_path(code) + " room " + std::to_string(room.index) + " (id " + std::to_string(room.id) + ")";
            RoomCollision c;
            if (!parse_collision(data, &c, &why)) {
                line(where + ": " + why);
                ++problems;
                continue;
            }
            if (write_collision(c) != data) {
                line(where + ": collision written back differently");
                ++problems;
            }
            std::string problem = check_tree(c);
            if (!problem.empty()) {
                line(where + ": the game's tree: " + problem);
                ++problems;
                continue;
            }
            ++trees_ok;
            // the planes stored on disk against the game's arithmetic (RoomPoly_BuildPlanes)
            for (int l = 0; l < 3; ++l) {
                for (const auto& f : c.faces[static_cast<std::size_t>(l)]) {
                    RoomFace g = f;
                    compute_planes(l, &g);
                    bool off = false;
                    for (int slot = 0; slot < 5; ++slot) {
                        const std::uint8_t* a = f.planes.data() + 12 * slot;
                        const std::uint8_t* b = g.planes.data() + 12 * slot;
                        for (int k = 0; k < 3; ++k) {
                            const int na = static_cast<std::int16_t>(a[2 * k] | a[2 * k + 1] << 8);
                            const int nb = static_cast<std::int16_t>(b[2 * k] | b[2 * k + 1] << 8);
                            plane_normal_off = std::max(plane_normal_off, std::abs(na - nb));
                            off = off || std::abs(na - nb) > 2;
                        }
                        const std::int64_t da = static_cast<std::int32_t>(a[8] | a[9] << 8 | a[10] << 16 |
                                                                          static_cast<std::uint32_t>(a[11]) << 24);
                        const std::int64_t db = static_cast<std::int32_t>(b[8] | b[9] << 8 | b[10] << 16 |
                                                                          static_cast<std::uint32_t>(b[11]) << 24);
                        plane_distance_off = std::max(plane_distance_off, std::llabs(da - db));
                        off = off || std::llabs(da - db) > 256;
                    }
                    if (off)
                        ++planes_off;
                    ++faces_seen;
                }
            }
            RoomCollision built = c;
            build_tree(&built, c.root(0), c.root(1), c.root(2), c.root(3));
            problem = check_tree(built);
            if (problem.empty())
                problem = compare_walks(c, built);
            if (!problem.empty()) {
                line(where + ": the tree rebuilt from its faces: " + problem);
                ++problems;
                continue;
            }
            ++rebuilt_ok;
        }
    }
    line(std::to_string(faces_seen) + " faces' planes computed as the game does: " + std::to_string(planes_off) +
         " differ from the stored ones by more than 2/4096 in a normal or 256/4096 in a distance (largest: " +
         std::to_string(plane_normal_off) + ", " + std::to_string(plane_distance_off) + ")");
    line(std::to_string(worlds) + " worlds, " + std::to_string(rooms) + " rooms with collision; " +
         std::to_string(trees_ok) + " game trees as the walk reads them, " + std::to_string(rebuilt_ok) +
         " rebuilt from their faces finding the same faces; " + std::to_string(problems) + " problems");
    return problems == 0;
}

}  // namespace studio
