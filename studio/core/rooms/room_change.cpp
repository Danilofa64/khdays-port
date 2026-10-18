#include "core/rooms/room_change.h"

#include "core/files/file_write.h"
#include "core/models/model_files.h"
#include "core/files/paths.h"

#include "khdays/assets/gltf.h"
#include "khdays/assets/gltf_export.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace studio {

namespace {

namespace assets = khdays::assets;

constexpr const char* kHeading = "khdays-studio room 1";

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

std::string use_word(ObjectUse u) {
    return u == ObjectUse::model ? "model" : u == ObjectUse::hide ? "hide" : "keep";
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names)
        out += (out.empty() ? "" : ",") + n;
    return out;
}

std::vector<std::string> split_names(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        if (ch == ',') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

using M4 = std::array<double, 16>;  // column-major

M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            double s = 0;
            for (int k = 0; k < 4; ++k)
                s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    }
    return r;
}

M4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// scale, then turned about x, y, z (degrees), then moved
M4 placement_matrix(const RoomPlacement& p) {
    const double k = 3.14159265358979323846 / 180.0;
    const double ax = p.turn[0] * k, ay = p.turn[1] * k, az = p.turn[2] * k;
    const M4 rx{1, 0, 0, 0, 0, std::cos(ax), std::sin(ax), 0, 0, -std::sin(ax), std::cos(ax), 0, 0, 0, 0, 1};
    const M4 ry{std::cos(ay), 0, -std::sin(ay), 0, 0, 1, 0, 0, std::sin(ay), 0, std::cos(ay), 0, 0, 0, 0, 1};
    const M4 rz{std::cos(az), std::sin(az), 0, 0, -std::sin(az), std::cos(az), 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    M4 s = identity();
    s[0] = s[5] = s[10] = p.scale;
    M4 t = identity();
    t[12] = p.move[0];
    t[13] = p.move[1];
    t[14] = p.move[2];
    return mul(t, mul(rz, mul(ry, mul(rx, s))));
}

std::array<float, 3> apply(const M4& m, const std::array<float, 3>& v, double w) {
    std::array<float, 3> r{};
    for (int row = 0; row < 3; ++row) {
        r[static_cast<std::size_t>(row)] =
            static_cast<float>(m[static_cast<std::size_t>(row)] * v[0] + m[static_cast<std::size_t>(4 + row)] * v[1] +
                               m[static_cast<std::size_t>(8 + row)] * v[2] + m[static_cast<std::size_t>(12 + row)] * w);
    }
    return r;
}

std::string member_label(const std::string& code, int member) {
    return world_path(code) + "#" + std::to_string(member);
}

// The texture files a written room model uses: `<model>-room-<n>.png`.
std::string texture_file(const std::string& model, std::size_t n) {
    return model + "-room-" + std::to_string(n) + ".png";
}

// What the studio wrote for a world's rooms (paths under the mod, '/'
// separators), so that only those are ever removed -- never a model the
// modder replaced by hand.
std::filesystem::path written_list(const std::filesystem::path& mod_dir, const std::string& code) {
    return rooms_folder(mod_dir, code) / "written.list";
}

std::set<std::string> read_written(const std::filesystem::path& mod_dir, const std::string& code) {
    std::set<std::string> out;
    std::ifstream in(written_list(mod_dir, code), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            out.insert(line);
    }
    return out;
}

// Removes a written file and the folders it leaves empty under the mod.
void remove_written(const std::filesystem::path& mod_dir, const std::string& relative) {
    std::error_code ec;
    const auto file = mod_dir / path_u8(relative);
    if (!inside(mod_dir, file))
        return;
    std::filesystem::remove(file, ec);
    for (auto f = file.parent_path(); f != mod_dir && inside(mod_dir, f); f = f.parent_path()) {
        if (!std::filesystem::is_directory(f, ec) || !std::filesystem::is_empty(f, ec))
            break;
        std::filesystem::remove(f, ec);
    }
}

// Writes `model` (placed) as the glTF replacing `key`'s model, its textures beside it.
bool write_room_model(const std::filesystem::path& mod_dir, const std::string& key, const assets::ImportedModel& model,
                      std::set<std::string>* written, std::string* why) {
    const std::filesystem::path gltf = mod_dir / "graphics" / path_u8(key + ".gltf");
    const std::string name = key.substr(key.rfind('/') + 1);
    std::map<std::string, assets::GltfTextureRef> refs;
    std::size_t n = 0;
    for (const auto& [texture, decoded] : model.textures) {
        if (decoded.rgba.empty() || decoded.width <= 0 || decoded.height <= 0)
            continue;
        const std::string file = texture_file(name, n++);
        const auto result = write_into_mod(
            mod_dir, gltf.parent_path() / path_u8(file),
            [&](const std::filesystem::path& temporary) -> std::string {
                const char* e =
                    khdays_png_write(u8(temporary).c_str(), decoded.rgba.data(), decoded.width, decoded.height);
                return e != nullptr ? std::string(e) : std::string();
            },
            [](const std::filesystem::path&) { return std::string(); });
        if (!result.ok)
            return fail_with(why, file + ": " + result.why);
        written->insert("graphics/" + key.substr(0, key.rfind('/') + 1) + file);
        refs[texture] = {file, decoded.width, decoded.height,
                         assets::texels_translucent(decoded.rgba.data(), decoded.rgba.size() / 4)};
    }
    const std::string text = assets::to_gltf(model.model, refs);
    const auto result = write_into_mod(
        mod_dir, gltf,
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            return out ? std::string() : std::string("could not be written");
        },
        [&](const std::filesystem::path& temporary) -> std::string {
            // read as the port reads it; the textures are beside the destination, not the temporary
            try {
                (void)assets::import_gltf(temporary);
            } catch (const std::exception& e) {
                return std::string("the port cannot read it: ") + e.what();
            }
            return {};
        });
    if (!result.ok)
        return fail_with(why, key + ".gltf: " + result.why);
    written->insert("graphics/" + key + ".gltf");
    return true;
}

}  // namespace

std::filesystem::path rooms_folder(const std::filesystem::path& mod_dir, const std::string& code) {
    return mod_dir / "studio" / "rooms" / path_u8(code);
}

std::filesystem::path change_file(const std::filesystem::path& mod_dir, const std::string& code, int room) {
    return rooms_folder(mod_dir, code) / (std::to_string(room) + ".txt");
}

std::filesystem::path change_models(const std::filesystem::path& mod_dir, const std::string& code, int room) {
    return rooms_folder(mod_dir, code) / std::to_string(room);
}

std::string change_text(const RoomReplacement& r) {
    std::ostringstream o;
    o << kHeading << "\n";
    o << "world " << r.world << "\n";
    o << "room " << r.room << "\n";
    o << "model " << r.model << "\n";
    if (!r.collision.empty())
        o << "collision " << r.collision << "\n";
    if (r.keep_collision)
        o << "collision-kept\n";
    o << "scale " << r.place.scale << "\n";
    o << "turn " << r.place.turn[0] << " " << r.place.turn[1] << " " << r.place.turn[2] << "\n";
    o << "move " << r.place.move[0] << " " << r.place.move[1] << " " << r.place.move[2] << "\n";
    o << "ground-limit " << r.ground_limit << "\n";
    for (const auto& [member, use] : r.objects)
        o << "object " << member << " " << use_word(use) << "\n";
    for (const auto& [material, names] : r.names_by_material)
        o << "material-names " << joined(names) << " " << material << "\n";
    for (const auto& [triangle, names] : r.names_by_triangle)
        o << "triangle-names " << triangle << " " << joined(names) << "\n";
    for (const auto& name : r.drop_original)
        o << "drop-original " << name << "\n";
    for (const auto& [name, at] : r.markers)
        o << "marker " << name << " " << at[0] << " " << at[1] << " " << at[2] << " " << at[3] << "\n";
    for (const auto& [key, at] : r.gate_points) {
        o << "gate-point " << key.first << " " << key.second << " " << at[0] << " " << at[1] << " " << at[2] << "\n";
    }
    return o.str();
}

bool read_change(const std::filesystem::path& file, RoomReplacement* out, std::string* why) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return fail_with(why, "cannot open " + u8(file));
    RoomReplacement r;
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (number == 1) {
            if (line != kHeading)
                return fail_with(why, u8(file) + ": not a room change of this studio");
            continue;
        }
        if (line.empty() || line[0] == '#')
            continue;
        const auto space = line.find(' ');
        const std::string word = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        std::istringstream v(rest);
        const std::string at = u8(file) + " line " + std::to_string(number);
        if (word == "world")
            r.world = rest;
        else if (word == "room")
            v >> r.room;
        else if (word == "model")
            r.model = rest;
        else if (word == "collision")
            r.collision = rest;
        else if (word == "collision-kept")
            r.keep_collision = true;
        else if (word == "scale")
            v >> r.place.scale;
        else if (word == "turn")
            v >> r.place.turn[0] >> r.place.turn[1] >> r.place.turn[2];
        else if (word == "move")
            v >> r.place.move[0] >> r.place.move[1] >> r.place.move[2];
        else if (word == "ground-limit")
            v >> r.ground_limit;
        else if (word == "object") {
            int member = -1;
            std::string use;
            v >> member >> use;
            if (use != "model" && use != "hide" && use != "keep")
                return fail_with(why, at + ": an object is model, hide or keep");
            r.objects[member] = use == "model" ? ObjectUse::model : use == "hide" ? ObjectUse::hide : ObjectUse::keep;
        } else if (word == "material-names") {
            const auto gap = rest.find(' ');
            if (gap == std::string::npos)
                return fail_with(why, at + ": names, then the material");
            r.names_by_material[rest.substr(gap + 1)] = split_names(rest.substr(0, gap));
        } else if (word == "triangle-names") {
            std::size_t t = 0;
            std::string names;
            v >> t >> names;
            r.names_by_triangle[t] = split_names(names);
        } else if (word == "drop-original") {
            r.drop_original.insert(rest);
        } else if (word == "marker") {
            std::string name;
            std::array<std::int32_t, 4> place{};
            v >> name >> place[0] >> place[1] >> place[2] >> place[3];
            r.markers[name] = place;
        } else if (word == "gate-point") {
            int key = 0, part = 0;
            std::array<std::int32_t, 3> place{};
            v >> key >> part >> place[0] >> place[1] >> place[2];
            if (part < 0 || part > 4)
                return fail_with(why, at + ": a gate model's point is 0 to 4");
            r.gate_points[{key, part}] = place;
        } else {
            return fail_with(why, at + ": \"" + word + "\" is not known");
        }
        if (v.fail() && word != "world" && word != "model" && word != "collision" && word != "collision-kept" &&
            word != "material-names" && word != "drop-original") {
            return fail_with(why, at + ": not a number where one goes");
        }
    }
    if (r.world.empty() || r.room < 0 || r.model.empty())
        return fail_with(why, u8(file) + ": no world, room or model");
    if (!(r.place.scale > 0))
        return fail_with(why, u8(file) + ": the scale must be above 0");
    *out = std::move(r);
    return true;
}

std::vector<RoomReplacement> changes_of(const std::filesystem::path& mod_dir, const std::string& code,
                                        std::vector<std::string>* problems) {
    std::vector<RoomReplacement> out;
    std::error_code ec;
    const auto folder = rooms_folder(mod_dir, code);
    if (!std::filesystem::is_directory(folder, ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (!e.is_regular_file() || lower(u8(e.path().extension())) != ".txt")
            continue;
        RoomReplacement r;
        std::string why;
        if (read_change(e.path(), &r, &why)) {
            out.push_back(std::move(r));
        } else if (problems != nullptr) {
            problems->push_back(why);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.room < b.room; });
    return out;
}

std::vector<std::string> changed_worlds(const std::filesystem::path& mod_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    const auto folder = mod_dir / "studio" / "rooms";
    if (!std::filesystem::is_directory(folder, ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (e.is_directory())
            out.push_back(u8(e.path().filename()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

RoomAlignment align_with_room(const std::vector<std::array<float, 3>>& room_points,
                              const assets::ImportedModel& model) {
    RoomAlignment best;
    if (room_points.empty())
        return best;
    // the model's points as the file places them, a sample of them
    assets::ImportedModel base = model;
    place_model(&base, RoomPlacement{});
    std::vector<std::array<float, 3>> points;
    std::size_t total = 0;
    for (const auto& mesh : base.model.meshes)
        total += mesh.vertices.size();
    const std::size_t step = std::max<std::size_t>(1, total / 4000);
    std::size_t at = 0;
    for (const auto& mesh : base.model.meshes) {
        for (const auto& v : mesh.vertices) {
            if (at++ % step == 0)
                points.push_back(v.position);
        }
    }
    if (points.empty())
        return best;
    // the room's points in cells of a hundredth of a unit (the game's own places: within 1/4096)
    const double cell = 0.01;
    auto key = [&](double x, double y, double z) {
        const auto ix = static_cast<std::int64_t>(std::floor(x / cell)),
                   iy = static_cast<std::int64_t>(std::floor(y / cell)),
                   iz = static_cast<std::int64_t>(std::floor(z / cell));
        return (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791);
    };
    std::set<std::int64_t> cells;
    for (const auto& q : room_points)
        cells.insert(key(q[0], q[1], q[2]));
    auto on_room = [&](const std::array<float, 3>& q) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (cells.count(key(q[0] + dx * cell, q[1] + dy * cell, q[2] + dz * cell)) != 0)
                        return true;
                }
            }
        }
        return false;
    };
    // every quarter-turn orientation once (24), as the turns that make it
    std::vector<std::array<float, 3>> turns;
    std::set<std::array<int, 9>> seen;
    for (int x = 0; x < 360; x += 90) {
        for (int y = 0; y < 360; y += 90) {
            for (int z = 0; z < 360; z += 90) {
                RoomPlacement p;
                p.turn = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                const M4 m = placement_matrix(p);
                std::array<int, 9> r{};
                for (int c = 0; c < 3; ++c) {
                    for (int row = 0; row < 3; ++row)
                        r[static_cast<std::size_t>(c * 3 + row)] =
                            static_cast<int>(std::lround(m[static_cast<std::size_t>(c * 4 + row)]));
                }
                if (seen.insert(r).second)
                    turns.push_back(p.turn);
            }
        }
    }
    for (const double scale : {1.0, 0.01, 100.0, 0.1, 10.0}) {
        for (const auto& turn : turns) {
            RoomPlacement p;
            p.scale = static_cast<float>(scale);
            p.turn = turn;
            const M4 m = placement_matrix(p);
            std::size_t hits = 0;
            for (const auto& q : points)
                hits += on_room(apply(m, q, 1)) ? 1 : 0;
            const double share = static_cast<double>(hits) / static_cast<double>(points.size());
            if (scale == 1.0 && turn[0] == 0 && turn[1] == 0 && turn[2] == 0)
                best.as_is = share;
            if (share > best.share + 1e-9) {
                best.share = share;
                best.place = p;
            }
        }
    }
    const bool moved =
        best.place.scale != 1.0f || best.place.turn[0] != 0 || best.place.turn[1] != 0 || best.place.turn[2] != 0;
    if (moved && best.share >= 0.05 && best.share >= 2 * best.as_is) {
        best.verdict = RoomAlignment::turned;
    } else if (best.as_is >= 0.03) {
        best.verdict = RoomAlignment::in_place;
        best.place = RoomPlacement{};
        best.share = best.as_is;
    } else {
        best.verdict = RoomAlignment::unrelated;
    }
    return best;
}

bool placed_model(const std::filesystem::path& model, const RoomPlacement& place, assets::ImportedModel* out,
                  std::string* why) {
    assets::ImportedModel m;
    if (!import_model(model, &m, why))
        return false;
    place_model(&m, place);
    *out = std::move(m);
    return true;
}

void place_model(assets::ImportedModel* model, const RoomPlacement& place) {
    assets::ImportedModel& m = *model;
    // the palette as the file poses it: each joint's global times its inverse bind
    std::map<std::uint32_t, M4> palette;
    for (const auto& j : m.joints) {
        M4 world{}, ibm{};
        for (std::size_t k = 0; k < 16; ++k)
            world[k] = j.world[k], ibm[k] = j.inverse_bind[k];
        palette[j.palette_index] = mul(world, ibm);
    }
    const M4 p = placement_matrix(place);
    for (auto& mesh : m.model.meshes) {
        for (auto& v : mesh.vertices) {
            std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
            float total = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                if (v.weights[k] <= 0)
                    continue;
                const auto found = palette.find(v.joints[k]);
                const M4& s = found != palette.end() ? found->second : identity();
                const auto q = apply(s, v.position, 1), n = apply(s, v.normal, 0);
                for (std::size_t c = 0; c < 3; ++c)
                    pos[c] += v.weights[k] * q[c], nrm[c] += v.weights[k] * n[c];
                total += v.weights[k];
            }
            if (total <= 0) {
                pos = v.position;
                nrm = v.normal;
            } else if (std::fabs(total - 1.0f) > 1e-4f) {
                for (auto& c : pos)
                    c /= total;
            }
            v.position = apply(p, pos, 1);
            v.normal = apply(p, nrm, 0);
            v.joints = {0, 0, 0, 0};
            v.weights = {1, 0, 0, 0};
        }
    }
    m.joints.clear();
    m.model.palette.clear();
    m.model.palette_bones.clear();
    m.model.object_matrices.clear();
    m.model.object_names.clear();
    m.model.object_parents.clear();
    m.model.skinning = nullptr;
}

CollisionSource collision_source(const assets::ImportedModel& placed, bool visible) {
    CollisionSource s;
    for (const auto& mesh : placed.model.meshes) {
        const std::string material = !mesh.material_name.empty() ? mesh.material_name : mesh.texture_name;
        const char passable = visible && mesh.alpha_mode == assets::NeutralMesh::AlphaMode::blend ? 1 : 0;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto& a = mesh.vertices[mesh.indices[i]];
            const auto& b = mesh.vertices[mesh.indices[i + 1]];
            const auto& c = mesh.vertices[mesh.indices[i + 2]];
            s.triangles.push_back({a.position, b.position, c.position});
            s.materials.push_back(material);
            s.passable.push_back(passable);
            // drawn both sides (every FBX mesh is): a steep face stops from both
            // sides too; a floor's other side would be a ceiling under it, so not
            const float e0[3] = {b.position[0] - a.position[0], b.position[1] - a.position[1],
                                 b.position[2] - a.position[2]};
            const float e1[3] = {c.position[0] - a.position[0], c.position[1] - a.position[1],
                                 c.position[2] - a.position[2]};
            const float n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                e0[0] * e1[1] - e0[1] * e1[0]};
            const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (mesh.double_sided && length > 0 && std::fabs(n[1] / length) < 0.5f) {
                s.triangles.push_back({a.position, c.position, b.position});
                s.materials.push_back(material);
                s.passable.push_back(passable);
            }
        }
    }
    return s;
}

std::vector<std::pair<int, ObjectUse>> object_uses(const WorldRoom& room, const RoomReplacement& r) {
    std::vector<std::pair<int, ObjectUse>> out;
    bool first = true;
    for (const auto& o : room.objects) {
        const auto set = r.objects.find(o.member);
        out.push_back({o.member, set != r.objects.end() ? set->second : first ? ObjectUse::model : ObjectUse::hide});
        first = false;
    }
    return out;
}

std::vector<std::string> member_models(const std::filesystem::path& graphics_dump, const std::string& code,
                                       int member) {
    std::vector<std::string> out;
    std::error_code ec;
    const std::string label = member_label(code, member);
    const auto folder = graphics_dump / path_u8(label) / "mdl";
    if (!std::filesystem::is_directory(folder, ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (lower(u8(e.path().extension())) == ".gltf")
            out.push_back(label + "/mdl/" + u8(e.path().stem()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> rooms_sharing(const World& w, int room, int member, bool collision) {
    std::vector<int> out;
    for (const auto& r : w.rooms) {
        if (r.index == room)
            continue;
        bool uses = collision ? r.collision_member == member : false;
        for (const auto& o : r.objects)
            uses = uses || (!collision && o.member == member);
        if (uses)
            out.push_back(r.index);
    }
    return out;
}

bool build_room(const World& w, const std::filesystem::path& mod_dir, const RoomReplacement& r, BuiltRoom* out,
                std::string* why) {
    if (r.room < 0 || static_cast<std::size_t>(r.room) >= w.rooms.size())
        return fail_with(why, "no room " + std::to_string(r.room));
    const WorldRoom& room = w.rooms[static_cast<std::size_t>(r.room)];
    const auto& data = w.archive.entries[static_cast<std::size_t>(room.collision_member)].data;
    if (data.empty())
        return fail_with(why, "room " + std::to_string(r.room) + " has no collision in the game's data to replace");
    RoomCollision original;
    if (!parse_collision(data, &original, why))
        return false;
    const auto folder = change_models(mod_dir, r.world, r.room);
    assets::ImportedModel placed;
    if (!placed_model(folder / path_u8(r.collision.empty() ? r.model : r.collision), r.place, &placed, why))
        return false;
    CollisionRules rules;
    rules.ground_limit = r.ground_limit;
    rules.names_by_material = r.names_by_material;
    rules.names_by_triangle = r.names_by_triangle;
    rules.drop_original = r.drop_original;
    rules.markers = r.markers;
    BuiltRoom b;
    if (!build_collision(collision_source(placed, r.collision.empty()), original, rules, &b.collision, &b.report,
                         why)) {
        return false;
    }
    const std::string problem = check_collision(b.collision, original);
    if (!problem.empty())
        return fail_with(why, "the collision made is not one the game reads: " + problem);
    b.warnings = b.report.warnings;
    for (auto& more : sharing_warnings(w, r))
        b.warnings.push_back(std::move(more));
    *out = std::move(b);
    return true;
}

void apply_gate_points(World* w, const RoomReplacement& r) {
    for (const auto& [key, at] : r.gate_points) {
        for (auto& g : w->gate_models) {
            if (g.key == key.first)
                g.parts[static_cast<std::size_t>(key.second)].position = at;
        }
    }
}

std::vector<RoomWarning> sharing_warnings(const World& w, const RoomReplacement& r) {
    std::vector<RoomWarning> out;
    if (r.room < 0 || static_cast<std::size_t>(r.room) >= w.rooms.size())
        return out;
    const WorldRoom& room = w.rooms[static_cast<std::size_t>(r.room)];
    for (const int other : rooms_sharing(w, r.room, room.collision_member, true)) {
        out.push_back({RoomWarning::collision_shared, {std::to_string(other)}});
    }
    for (const auto& [member, use] : object_uses(room, r)) {
        if (use == ObjectUse::keep)
            continue;
        for (const int other : rooms_sharing(w, r.room, member, false)) {
            out.push_back({RoomWarning::model_shared, {std::to_string(other), std::to_string(member)}});
        }
    }
    return out;
}

bool write_world_outputs(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                         const std::filesystem::path& mod_dir, const std::string& code, WorldOutcome* out,
                         std::string* why) {
    WorldOutcome o;
    World w;
    if (!load_world(nitrofs, code, &w, why))
        return false;  // the player's copy, never the mod's
    std::vector<std::string> problems;
    const auto changes = changes_of(mod_dir, code, &problems);
    if (!problems.empty())
        return fail_with(why, problems.front());
    std::error_code ec;
    const auto archive = mod_dir / "files" / path_u8(world_path(code));

    const std::set<std::string> before = read_written(mod_dir, code);
    if (changes.empty()) {
        for (const auto& file : before)
            remove_written(mod_dir, file);
        std::filesystem::remove(written_list(mod_dir, code), ec);
        std::filesystem::remove(rooms_folder(mod_dir, code), ec);  // only when empty
        o.removed = true;
        o.lines.push_back(world_path(code) + ": no room replaced; its outputs removed");
        if (out != nullptr)
            *out = std::move(o);
        return true;
    }

    // the collisions
    std::map<int, std::vector<std::uint8_t>> collisions;
    std::map<int, int> collision_of;  // member -> the room that made it
    for (const auto& r : changes) {
        if (r.keep_collision) {
            o.lines.push_back("room " + std::to_string(r.room) + ": the game's own grounds, walls and ceilings kept");
            continue;
        }
        BuiltRoom b;
        std::string problem;
        if (!build_room(w, mod_dir, r, &b, &problem))
            return fail_with(why, "room " + std::to_string(r.room) + ": " + problem);
        const int member = w.rooms[static_cast<std::size_t>(r.room)].collision_member;
        if (collision_of.count(member) != 0) {
            return fail_with(why, "rooms " + std::to_string(collision_of[member]) + " and " + std::to_string(r.room) +
                                      " share one collision: replace only one of them");
        }
        collision_of[member] = r.room;
        collisions[member] = write_collision(b.collision);
        o.lines.push_back("room " + std::to_string(r.room) + ": " + std::to_string(b.report.faces[kGround]) +
                          " grounds, " + std::to_string(b.report.faces[kWall]) + " walls, " +
                          std::to_string(b.report.faces[kCeiling]) + " ceilings, " + std::to_string(b.report.nodes) +
                          " nodes, " + std::to_string(b.report.bytes) + " bytes");
        for (const auto& warning : b.warnings)
            o.lines.push_back("room " + std::to_string(r.room) + ": " + warning_text(warning));
    }

    // the archive, checked before it replaces anything: the gate models' points moved too
    World moved = w;
    for (const auto& r : changes)
        apply_gate_points(&moved, r);
    const auto bytes = write_world(moved, collisions);
    const auto gate_entry = write_gate_models(moved);
    const auto result = write_into_mod(
        mod_dir, archive,
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return f ? std::string() : std::string("could not be written");
        },
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ifstream f(temporary, std::ios::binary);
            const std::vector<std::uint8_t> back((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            World again;
            std::string problem;
            if (!parse_world(back, code, &again, &problem))
                return "it does not read back: " + problem;
            if (again.archive.entries.size() != w.archive.entries.size())
                return "it holds another number of entries";
            for (std::size_t e = 0; e < w.archive.entries.size(); ++e) {
                const auto made = collisions.find(static_cast<int>(e));
                if (e == 1) {
                    if (again.archive.entries[e].data != gate_entry)
                        return "the gate models are not as moved";
                    continue;
                }
                if (made == collisions.end()) {
                    if (again.archive.entries[e].data != w.archive.entries[e].data)
                        return "entry " + std::to_string(e) + " changed";
                    continue;
                }
                RoomCollision c, original;
                if (!parse_collision(again.archive.entries[e].data, &c, &problem))
                    return "entry " + std::to_string(e) + ": " + problem;
                if (!parse_collision(w.archive.entries[e].data, &original, &problem))
                    return problem;
                problem = check_collision(c, original);
                if (!problem.empty())
                    return "entry " + std::to_string(e) + ": " + problem;
            }
            return {};
        });
    if (!result.ok)
        return fail_with(why, world_path(code) + ": " + result.why);
    std::set<std::string> now{"files/" + world_path(code)};
    o.lines.push_back(u8(archive) + " written");

    // the models: the modder's, nothing, or the game's
    std::map<int, std::pair<const RoomReplacement*, ObjectUse>> use_of;
    for (const auto& r : changes) {
        for (const auto& [member, use] : object_uses(w.rooms[static_cast<std::size_t>(r.room)], r))
            use_of[member] = {&r, use};
    }
    std::map<std::string, assets::ImportedModel> placed;  // by change
    for (const auto& [member, change] : use_of) {
        const RoomReplacement& r = *change.first;
        if (change.second == ObjectUse::keep)
            continue;
        const auto keys = member_models(graphics_dump, code, member);
        if (keys.empty()) {
            return fail_with(why, "the graphics dump has no model of " + member_label(code, member) +
                                      ": make the dump first");
        }
        assets::ImportedModel model;
        if (change.second == ObjectUse::model) {
            const std::string id = r.world + "/" + std::to_string(r.room);
            auto it = placed.find(id);
            if (it == placed.end()) {
                assets::ImportedModel m;
                if (!placed_model(change_models(mod_dir, r.world, r.room) / path_u8(r.model), r.place, &m, why))
                    return false;
                it = placed.emplace(id, std::move(m)).first;
            }
            model = it->second;
        }
        for (const auto& key : keys) {
            if (!write_room_model(mod_dir, key, model, &now, why))
                return false;
            o.lines.push_back(key + ".gltf written" + (change.second == ObjectUse::hide ? " (nothing drawn)" : ""));
        }
    }
    // what an earlier save wrote and this one did not: removed; the list kept
    for (const auto& file : before) {
        if (now.count(file) == 0)
            remove_written(mod_dir, file);
    }
    std::string list;
    for (const auto& file : now)
        list += file + "\n";
    const auto saved = write_into_mod(
        mod_dir, written_list(mod_dir, code),
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f << list;
            return f ? std::string() : std::string("could not be written");
        },
        [](const std::filesystem::path&) { return std::string(); });
    if (!saved.ok)
        return fail_with(why, saved.why);
    if (out != nullptr)
        *out = std::move(o);
    return true;
}

bool save_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                 const std::filesystem::path& mod_dir, RoomReplacement r, const std::filesystem::path& model_source,
                 const std::filesystem::path& collision_source_file, WorldOutcome* out, std::string* why) {
    std::error_code ec;
    const auto folder = change_models(mod_dir, r.world, r.room);
    // the model files and what they need, beside the change
    auto bring = [&](const std::filesystem::path& source, std::string* name) {
        if (source.empty())
            return true;
        *name = u8(source.filename());
        const auto destination = folder / source.filename();
        if (std::filesystem::equivalent(source, destination, ec))
            return true;
        std::filesystem::create_directories(folder, ec);
        std::string problem = copy_bytes(source, destination);
        if (!problem.empty())
            return fail_with(why, problem);
        std::vector<std::string> missing;
        for (const auto& c : model_companions(source, &missing)) {
            const auto to = folder / path_u8(c.relative);
            std::filesystem::create_directories(to.parent_path(), ec);
            problem = copy_bytes(c.source, to);
            if (!problem.empty())
                return fail_with(why, problem);
        }
        return true;
    };
    if (!bring(model_source, &r.model))
        return false;
    if (collision_source_file.empty())
        r.collision.clear();
    else if (!bring(collision_source_file, &r.collision))
        return false;
    // the change, then everything made from it
    const std::string text = change_text(r);
    const auto result = write_into_mod(
        mod_dir, change_file(mod_dir, r.world, r.room),
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f << text;
            return f ? std::string() : std::string("could not be written");
        },
        [&](const std::filesystem::path& temporary) -> std::string {
            RoomReplacement back;
            std::string problem;
            return read_change(temporary, &back, &problem) ? std::string() : problem;
        });
    if (!result.ok)
        return fail_with(why, result.why);
    return write_world_outputs(nitrofs, graphics_dump, mod_dir, r.world, out, why);
}

bool revert_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                   const std::filesystem::path& mod_dir, const std::string& code, int room, WorldOutcome* out,
                   std::string* why) {
    std::error_code ec;
    const auto file = change_file(mod_dir, code, room);
    if (!std::filesystem::exists(file, ec))
        return fail_with(why, "room " + std::to_string(room) + " of " + code + " is not replaced in this mod");
    std::filesystem::remove(file, ec);
    std::filesystem::remove_all(change_models(mod_dir, code, room), ec);
    return write_world_outputs(nitrofs, graphics_dump, mod_dir, code, out, why);
}

}  // namespace studio
