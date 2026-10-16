// The map editor: a room of a mission world as the game has it -- its models, collision, markers and
// exits, any face or marker picked to read what it is -- and replaced by the modder's model, its
// collision made and shown as the game will read it (docs/STUDIO.md, "Rooms").
#include "../app.h"

#include "core/models/bones.h"
#include "core/models/game_model.h"
#include "core/models/model_files.h"
#include "core/files/paths.h"
#include "core/rooms/room_change.h"
#include "core/rooms/room_export.h"
#include "core/mods/settings.h"
#include "core/rooms/worlds.h"
#include "../preview3d.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"

#include <SDL3/SDL.h>

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace studio {

namespace {

namespace assets = khdays::assets;

constexpr float kFx = 1.0f / 4096.0f;  // the game's 20.12 to units

struct Texture {
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
};

struct RoomModel {
    int member = 0;
    std::string key;     // the dump's name, empty when not found
    std::string problem;  // why it is not drawn
    GameModel game;
    std::vector<Mat> palette;
};

// A replacement being made (or the mod's, opened): what the modder gave,
// placed, and the collision made from it.
struct Replacing {
    bool active = false;
    bool from_mod = false;  // the mod holds a change for this room
    RoomReplacement r;
    fs::path model_source, collision_source;  // the files as given (or the mod's copies)
    assets::ImportedModel raw, raw_collision;  // as read
    assets::ImportedModel placed;              // placed: what is drawn
    bool dirty = false, built_ok = false;
    RoomCollision built;
    CollisionReport report;
    std::vector<RoomWarning> warnings;
    std::string error, message;
    int view = 0;  // 0 yours, 1 the game's, 2 both
    bool next_is_collision = false;
    bool other_collision = false;  // the collision from another model, chosen (its file perhaps not yet)
    std::string placed_text;  // how the model was placed (lined up, as it is, or to place by hand)
    bool placed_ok = true;
};

struct MapView {
    int item = -1;
    int mod = -1;  // the mod chosen when the room was opened
    std::string error;
    World world;
    const WorldRoom* room = nullptr;
    bool has_collision = false;
    RoomCollision collision;
    std::vector<Marker> markers;
    std::vector<RoomModel> models;
    std::map<std::string, Texture> textures;

    // as the game shows it first; the collision laid over it when asked
    bool show_models = true, show_ground = false, show_walls = false, show_ceilings = false, show_markers = true;
    float yaw = 30.0f, pitch = 35.0f, distance = 40.0f;
    std::array<float, 3> target{0, 0, 0};
    int picked_list = -1, picked_face = -1, picked_marker = -1;
    std::unique_ptr<Preview> preview;
    Replacing rep;
};

MapView m;

// The collision drawn and picked: the one made when the modder's is shown
// (the game's when it is kept).
const RoomCollision& shown() {
    return m.rep.active && m.rep.view != 1 && m.rep.built_ok ? m.rep.built : m.collision;
}

bool showing_built() {
    return m.rep.active && m.rep.view != 1 && m.rep.built_ok;
}

// The names a face or material may be given: the room's own, then the surface tags.
std::vector<std::string> name_choices() {
    std::vector<std::string> out;
    for (const auto& n : m.collision.named) out.push_back(n.text());
    for (const char* t : {"nohit", "nocatch", "sea", "nocam", "slide", "sea2", "sicon"}) {
        if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    return out;
}

// The materials of the model the collision is made from, as the builder names them.
std::vector<std::string> collision_materials() {
    const assets::ImportedModel& model = m.rep.collision_source.empty() ? m.rep.raw : m.rep.raw_collision;
    std::vector<std::string> out;
    for (const auto& mesh : model.model.meshes) {
        const std::string name = !mesh.material_name.empty() ? mesh.material_name : mesh.texture_name;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    return out;
}

fs::path nitrofs() {
    return khdays::vfs::data_root() / "nitrofs";
}

const Texture* texture(const GameModel& g, const std::string& name) {
    const std::string key = g.label + "|" + name;
    const auto found = m.textures.find(key);
    if (found != m.textures.end()) return found->second.rgba.empty() ? nullptr : &found->second;
    Texture t;
    const auto file = g.textures.find(name);
    if (file != g.textures.end()) {
        const char* error = nullptr;
        std::uint8_t* px = khdays_png_read(u8(file->second).c_str(), &t.width, &t.height, &error);
        if (px != nullptr) {
            t.rgba.assign(px, px + static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4u);
            khdays_png_free(px);
        }
    }
    const auto it = m.textures.emplace(key, std::move(t)).first;
    return it->second.rgba.empty() ? nullptr : &it->second;
}

// The kind a named record is, by its name.
S named_kind(const std::string& name) {
    static const char* const tags[] = {"nohit", "nocatch", "sea", "nocam", "slide", "sea2", "sicon"};
    for (const char* t : tags) {
        if (name == t) return S::map_kind_tag;
    }
    if (name.rfind("gate", 0) == 0) return S::map_kind_gate;
    if (name.rfind("col_wall", 0) == 0) return S::map_kind_wall;
    return S::map_kind_other;
}

// What reads a surface tag, as found in the game's code (function names: not translated).
const char* tag_readers(const std::string& name) {
    if (name == "nohit") return "Ov022_SettlePointOnGround, Ov022_CastMove, Ov022_MayActOnHit, Ov022_StepChargeSequence";
    if (name == "nocatch") return "Ov022_FindClimbTarget";
    if (name == "sea" || name == "sea2") return "Ov022_StepGlideState";
    if (name == "slide") return "Ov022_StepGlideState, Ov022_StepJumpState";
    return nullptr;
}

std::string names_of(int list, const RoomFace& f) {
    std::string out;
    for (std::size_t p = 0; p < 4; ++p) {
        const auto s = f.slots[p];
        if (s == 0xff) continue;
        if (!slot_names(list, p)) {
            out += (out.empty() ? "" : ", ") + std::string("kind ") + std::to_string(s);
            continue;
        }
        if (s >= shown().named.size()) continue;
        out += (out.empty() ? "" : ", ") + shown().named[s].text();
    }
    return out;
}

void frame_camera() {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& list : shown().faces) {
        for (const auto& f : list) {
            for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                for (std::size_t c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], static_cast<float>(f.v[v][c]) * kFx);
                    hi[c] = std::max(hi[c], static_cast<float>(f.v[v][c]) * kFx);
                }
            }
        }
    }
    if (lo[0] > hi[0]) {  // no collision: around the models' nodes
        for (const auto& rm : m.models) {
            for (const auto& w : rm.game.rest) {
                for (std::size_t c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], w[12 + c]);
                    hi[c] = std::max(hi[c], w[12 + c]);
                }
            }
        }
    }
    if (lo[0] > hi[0]) return;
    m.target = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    m.distance = std::max({hi[0] - lo[0], hi[2] - lo[2], 2.0f}) * 1.3f;
}

void open(App& app) {
    const Item& item = app.catalog.items[static_cast<std::size_t>(app.selected)];
    MapView fresh;
    fresh.item = app.selected;
    fresh.mod = app.mod;
    fresh.preview = std::move(m.preview);
    m = std::move(fresh);
    // `mi/wd/wd_<code>/room/<index>`
    const auto at = item.key.rfind("/room/");
    const std::string file = item.group;
    const std::string code = file.size() > 9 ? file.substr(9) : std::string();
    if (at == std::string::npos || !load_world(nitrofs(), code, &m.world, &m.error)) {
        if (m.error.empty()) m.error = "not a room: " + item.key;
        return;
    }
    const int index = std::atoi(item.key.c_str() + at + 6);
    if (index < 0 || static_cast<std::size_t>(index) >= m.world.rooms.size()) {
        m.error = "no room " + std::to_string(index);
        return;
    }
    m.room = &m.world.rooms[static_cast<std::size_t>(index)];
    const auto& data = m.world.archive.entries[static_cast<std::size_t>(m.room->collision_member)].data;
    if (!data.empty()) {
        std::string why;
        m.has_collision = parse_collision(data, &m.collision, &why);
        if (!m.has_collision) m.error = why;
        else m.markers = markers(m.collision);
    }
    // the room's models: its objects, as the graphics dump names them
    const fs::path dump = resolve(app.settings, app.settings.graphics_dump);
    for (const auto& o : m.room->objects) {
        RoomModel rm;
        rm.member = o.member;
        if (o.member >= m.world.archive.entries.size() || m.world.archive.entries[o.member].data.empty()) {
            rm.problem = fmt(app.language, S::map_model_empty, {std::to_string(o.member)});
            m.models.push_back(std::move(rm));
            continue;
        }
        const std::string prefix = file + "#" + std::to_string(o.member) + "/mdl/";
        for (const auto& it : app.catalog.items) {
            if (it.kind == Kind::model && it.key.rfind(prefix, 0) == 0) {
                rm.key = it.key;
                break;
            }
        }
        std::string why;
        if (rm.key.empty()) {
            rm.problem = fmt(app.language, S::map_model_missing, {prefix});
        } else if (!load_game_model(nitrofs(), dump, rm.key, &rm.game, &why)) {
            rm.problem = rm.key + ": " + why;
        } else {
            std::vector<Mat> node_world;
            pose_game_model(rm.game, nullptr, 0.0f, &node_world, &rm.palette);
        }
        m.models.push_back(std::move(rm));
    }
    frame_camera();
    // the mod's replacement of this room, opened to change it
    if (const Mod* mod = app.current_mod()) {
        const fs::path change = change_file(mod->dir, m.world.code, index);
        std::error_code ec;
        if (fs::exists(change, ec)) {
            Replacing& p = m.rep;
            std::string why;
            if (!read_change(change, &p.r, &why)) {
                p.error = why;
                return;
            }
            p.active = p.from_mod = true;
            const fs::path folder = change_models(mod->dir, m.world.code, index);
            p.model_source = folder / path_u8(p.r.model);
            if (!import_model(p.model_source, &p.raw, &why)) p.error = why;
            if (!p.r.collision.empty()) {
                p.collision_source = folder / path_u8(p.r.collision);
                if (!import_model(p.collision_source, &p.raw_collision, &why)) p.error = why;
            }
            p.dirty = p.error.empty();
        }
    }
}

// The modder's model placed again and its collision made, as the save will.
void rebuild() {
    Replacing& p = m.rep;
    p.dirty = false;
    p.built_ok = false;
    p.error.clear();
    p.warnings.clear();
    if (p.model_source.empty()) return;
    p.placed = p.raw;
    place_model(&p.placed, p.r.place);
    assets::ImportedModel collision = p.collision_source.empty() ? p.placed : p.raw_collision;
    if (!p.collision_source.empty()) place_model(&collision, p.r.place);
    if (!m.has_collision) {
        p.error = "this room has no collision in the game's data to replace";
        return;
    }
    if (p.r.keep_collision) {
        // only the look replaced: the game's own floor and walls are the ones shown and saved
        p.built = m.collision;
        p.report = CollisionReport{};
        p.built_ok = true;
        p.r.world = m.world.code;
        p.r.room = m.room->index;
        for (auto& more : sharing_warnings(m.world, p.r)) {
            if (more.kind != RoomWarning::collision_shared) p.warnings.push_back(std::move(more));
        }
        return;
    }
    CollisionRules rules;
    rules.ground_limit = p.r.ground_limit;
    rules.names_by_material = p.r.names_by_material;
    rules.names_by_triangle = p.r.names_by_triangle;
    rules.drop_original = p.r.drop_original;
    rules.markers = p.r.markers;
    if (!build_collision(collision_source(collision, p.collision_source.empty()), m.collision, rules, &p.built, &p.report,
                         &p.error)) {
        return;
    }
    const std::string problem = check_collision(p.built, m.collision);
    if (!problem.empty()) {
        p.error = "the collision made is not one the game reads: " + problem;
        return;
    }
    p.built_ok = true;
    p.warnings = p.report.warnings;
    p.r.world = m.world.code;
    p.r.room = m.room->index;
    for (auto& more : sharing_warnings(m.world, p.r)) p.warnings.push_back(std::move(more));
}

std::array<float, 3> corner(const RoomFace& f, std::size_t v) {
    return {static_cast<float>(f.v[v][0]) * kFx, static_cast<float>(f.v[v][1]) * kFx, static_cast<float>(f.v[v][2]) * kFx};
}

std::array<float, 3> face_tint(int list, const RoomFace& f) {
    bool gate = false, wall = false, tag = false;
    for (std::size_t p = 0; p < 4; ++p) {
        const auto s = f.slots[p];
        if (s == 0xff || s >= shown().named.size() || !slot_names(list, p)) continue;
        const S kind = named_kind(shown().named[s].text());
        gate = gate || kind == S::map_kind_gate;
        wall = wall || kind == S::map_kind_wall;
        tag = tag || kind == S::map_kind_tag;
    }
    if (gate) return {0.95f, 0.85f, 0.2f};
    if (wall) return {0.9f, 0.3f, 0.9f};
    if (tag) return {0.3f, 0.9f, 0.9f};
    return list == kGround ? std::array<float, 3>{0.35f, 0.85f, 0.35f}
         : list == kWall   ? std::array<float, 3>{0.95f, 0.55f, 0.25f}
                           : std::array<float, 3>{0.4f, 0.55f, 1.0f};
}

void draw_room(App& app, ImVec2 size) {
    if (!m.preview) m.preview = std::make_unique<Preview>(app.device);
    Preview& p = *m.preview;
    p.collect();
    const int w = std::max(64, static_cast<int>(size.x)), h = std::max(64, static_cast<int>(size.y));
    const Mat vp = orbit_view_projection(m.target, m.yaw, m.pitch, m.distance, static_cast<float>(w) / static_cast<float>(h));
    p.begin(w, h, vp, orbit_eye_direction(m.yaw, m.pitch));

    const bool yours = m.rep.active && m.rep.view != 1 && !m.rep.model_source.empty();
    const bool games = !yours || m.rep.view == 2;
    if (m.show_models) {
        const auto uses = m.rep.active ? object_uses(*m.room, m.rep.r) : std::vector<std::pair<int, ObjectUse>>{};
        for (const auto& rm : m.models) {
            if (!rm.problem.empty()) continue;
            bool kept = games;
            for (const auto& [member, use] : uses) kept = kept || (member == rm.member && use == ObjectUse::keep);
            if (!kept) continue;
            for (const auto& mesh : rm.game.model.meshes) {
                if (mesh.material_alpha <= 0.0f) continue;  // the DS draws nothing of it
                const Texture* t = mesh.texture_name.empty() ? nullptr : texture(rm.game, mesh.texture_name);
                PreviewImage image;
                if (t != nullptr) image = {t->rgba.data(), t->width, t->height, mesh.repeat, mesh.mirror};
                const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
                const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
                auto vertex = [&](const assets::NeutralVertex& v) {
                    PreviewVertex o;
                    std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                    for (int k = 0; k < 4; ++k) {
                        const float wgt = v.weights[static_cast<std::size_t>(k)];
                        const std::uint32_t jt = v.joints[static_cast<std::size_t>(k)];
                        if (wgt <= 0.0f || jt >= rm.palette.size()) continue;
                        const auto q = mat_point(rm.palette[jt], v.position);
                        const auto n = mat_vec(rm.palette[jt], v.normal);
                        for (std::size_t c = 0; c < 3; ++c) pos[c] += wgt * q[c], nrm[c] += wgt * n[c];
                    }
                    o.position = pos;
                    o.normal = nrm;
                    o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                    // the vertex colour the room is lit with, its material's alpha
                    o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f,
                                v.color[3] / 255.0f * mesh.material_alpha};
                    return o;
                };
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                               vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
                }
            }
        }
        if (yours) {
            // the modder's, placed: as the port draws it (its vertex colours, its textures)
            for (const auto& mesh : m.rep.placed.model.meshes) {
                const assets::DecodedTexture* t = nullptr;
                for (const auto& [name, decoded] : m.rep.placed.textures) {
                    if (name == mesh.texture_name && !decoded.rgba.empty()) t = &decoded;
                }
                PreviewImage image;
                if (t != nullptr) image = {t->rgba.data(), t->width, t->height};
                const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
                const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
                auto vertex = [&](const assets::NeutralVertex& v) {
                    PreviewVertex o;
                    o.position = v.position;
                    o.normal = v.normal;
                    o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                    o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f, v.color[3] / 255.0f};
                    return o;
                };
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                               vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
                }
            }
        }
    }
    const bool show[3] = {m.show_ground, m.show_walls, m.show_ceilings};
    // over the models: see-through, brought a little towards the viewer so
    // the surface it follows does not show through it in stripes
    const float alpha = m.show_models ? 0.5f : 1.0f;
    const auto eye_dir = orbit_eye_direction(m.yaw, m.pitch);
    const std::array<float, 3> eye{m.target[0] + eye_dir[0] * m.distance, m.target[1] + eye_dir[1] * m.distance,
                                   m.target[2] + eye_dir[2] * m.distance};
    const float lift = m.show_models ? m.distance * 0.002f : 0.0f;
    for (int l = 0; l < 3; ++l) {
        if (!show[l]) continue;
        const auto& faces = shown().faces[static_cast<std::size_t>(l)];
        for (std::size_t j = 0; j < faces.size(); ++j) {
            const RoomFace& f = faces[j];
            if (f.corners < 3) continue;
            std::array<float, 3> tint = face_tint(l, f);
            if (l == m.picked_list && static_cast<int>(j) == m.picked_face) tint = {1, 1, 1};
            // the face's normal from its corners, for the light
            const auto a = corner(f, 0), b = corner(f, 1), c = corner(f, 2);
            const std::array<float, 3> e0{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e1{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const std::array<float, 3> n{e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
            auto vertex = [&](std::size_t v) {
                PreviewVertex o;
                o.position = corner(f, v);
                const std::array<float, 3> to{eye[0] - o.position[0], eye[1] - o.position[1], eye[2] - o.position[2]};
                const float d = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
                if (d > 0.0f) {
                    for (std::size_t k = 0; k < 3; ++k) o.position[k] += to[k] / d * lift;
                }
                o.normal = n;
                o.colour[3] = alpha;
                return o;
            };
            // a face's corners turn so that (v1-v0)x(v2-v0) points to the side the
            // player stands on (4119 of the 4124 grounds up, every ceiling down):
            // seen from behind, it is left out, as a level editor shows a room
            p.triangle(vertex(0), vertex(1), vertex(2), PreviewImage{}, true, tint);
            if (f.corners == 4) p.triangle(vertex(0), vertex(2), vertex(3), PreviewImage{}, true, tint);
        }
    }

    SDL_GPUTexture* tex = p.finish();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (tex != nullptr) {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(tex))), ImVec2(static_cast<float>(w), static_cast<float>(h)));
    } else {
        ImGui::Dummy(ImVec2(static_cast<float>(w), static_cast<float>(h)));
    }
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto project = [&](const std::array<float, 3>& q, float* x, float* y) {
        if (!p.project(q, x, y)) return false;
        *x += at.x;
        *y += at.y;
        return true;
    };

    // the markers: a dot and the way it faces
    int near_marker = -1;
    float best = 9.0f * ImGui::GetStyle().FontScaleDpi;
    const std::vector<Marker> shown_markers = showing_built() ? markers(m.rep.built) : m.markers;
    if (m.show_markers) {
        for (std::size_t k = 0; k < shown_markers.size(); ++k) {
            const Marker& mk = shown_markers[k];
            const std::array<float, 3> q{static_cast<float>(mk.place[0]) * kFx, static_cast<float>(mk.place[1]) * kFx,
                                         static_cast<float>(mk.place[2]) * kFx};
            float x, y;
            if (!project(q, &x, &y)) continue;
            const float turn = static_cast<float>(mk.extra) / 65536.0f * 6.2831853f;
            float fx, fy;
            if (project({q[0] + std::sin(turn), q[1], q[2] + std::cos(turn)}, &fx, &fy)) {
                list->AddLine(ImVec2(x, y), ImVec2(fx, fy), IM_COL32(255, 80, 80, 220), 2.0f);
            }
            const bool picked = static_cast<int>(k) == m.picked_marker;
            list->AddCircleFilled(ImVec2(x, y), picked ? 6.0f : 4.0f, picked ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 60, 60, 255));
            const float d = std::hypot(x - mouse.x, y - mouse.y);
            if (hovered && d < best) best = d, near_marker = static_cast<int>(k);
        }
    }
    if (near_marker >= 0) ImGui::SetTooltip("%s", shown_markers[static_cast<std::size_t>(near_marker)].name.c_str());
    // a mission's points in this room: spawners green, the players' starts blue, chests gold, emblems
    // pink, labels on hover
    const MissionOverlay& mo = mission_overlay();
    if (mo.world == m.world.code && mo.room == m.room->index) {
        for (const auto& pt : mo.points) {
            float x, y;
            if (!project({static_cast<float>(pt.at[0]) * kFx, static_cast<float>(pt.at[1]) * kFx, static_cast<float>(pt.at[2]) * kFx}, &x, &y)) continue;
            static const ImU32 colors[] = {IM_COL32(80, 255, 120, 255), IM_COL32(90, 170, 255, 255), IM_COL32(255, 200, 60, 255),
                                           IM_COL32(255, 110, 210, 255)};
            list->AddCircle(ImVec2(x, y), 7.0f, colors[pt.kind], 0, 2.5f);
            if (hovered && std::hypot(x - mouse.x, y - mouse.y) < 9.0f) ImGui::SetTooltip("%s", pt.label.c_str());
        }
    }
    // the exits' models (the world's gate models of this room's links), where the change puts them
    if (m.show_markers) {
        for (const auto& link : m.room->links) {
            for (const auto& g : m.world.gate_models) {
                if (g.key != static_cast<std::int8_t>(link.key)) continue;
                for (std::size_t part = 0; part < 5; ++part) {
                    if (g.parts[part].member == 0) continue;
                    auto point = g.parts[part].position;
                    const auto moved = m.rep.r.gate_points.find({g.key, static_cast<int>(part)});
                    if (m.rep.active && moved != m.rep.r.gate_points.end()) point = moved->second;
                    float x, y;
                    if (!project({static_cast<float>(point[0]) * kFx, static_cast<float>(point[1]) * kFx, static_cast<float>(point[2]) * kFx}, &x, &y)) {
                        continue;
                    }
                    const float r = 5.0f;
                    list->AddQuadFilled(ImVec2(x, y - r), ImVec2(x + r, y), ImVec2(x, y + r), ImVec2(x - r, y), IM_COL32(255, 210, 40, 230));
                }
            }
        }
    }

    // a click picks the nearest marker, else the nearest face under the pointer
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f) {
        m.picked_marker = near_marker;
        m.picked_list = m.picked_face = -1;
        if (near_marker < 0) {
            float nearest = 1e30f;
            for (int l = 0; l < 3; ++l) {
                if (!show[l]) continue;
                const auto& faces = shown().faces[static_cast<std::size_t>(l)];
                for (std::size_t j = 0; j < faces.size(); ++j) {
                    const RoomFace& f = faces[j];
                    float xs[4], ys[4];
                    bool ok = true;
                    for (std::size_t v = 0; v < f.corners && v < 4 && ok; ++v) ok = project(corner(f, v), &xs[v], &ys[v]);
                    if (!ok || f.corners < 3) continue;
                    // not seen from behind (as drawn)
                    const auto a = corner(f, 0), b = corner(f, 1), c2 = corner(f, 2);
                    const std::array<float, 3> e0{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e1{c2[0] - a[0], c2[1] - a[1], c2[2] - a[2]};
                    const std::array<float, 3> n{e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
                    if (n[0] * (eye[0] - a[0]) + n[1] * (eye[1] - a[1]) + n[2] * (eye[2] - a[2]) <= 0.0f) continue;
                    // inside the projected polygon (either winding)
                    int sign = 0;
                    bool inside = true;
                    for (std::size_t v = 0; v < f.corners && inside; ++v) {
                        const std::size_t u = (v + 1) % f.corners;
                        const float cross = (xs[u] - xs[v]) * (mouse.y - ys[v]) - (ys[u] - ys[v]) * (mouse.x - xs[v]);
                        const int s = cross > 0 ? 1 : cross < 0 ? -1 : 0;
                        if (s != 0 && sign != 0 && s != sign) inside = false;
                        if (s != 0) sign = s;
                    }
                    if (!inside) continue;
                    std::array<float, 3> centre{0, 0, 0};
                    for (std::size_t v = 0; v < f.corners; ++v) {
                        const auto q = corner(f, v);
                        for (std::size_t c = 0; c < 3; ++c) centre[c] += q[c] / static_cast<float>(f.corners);
                    }
                    const float d = std::hypot(centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2]);
                    if (d < nearest) nearest = d, m.picked_list = l, m.picked_face = static_cast<int>(j);
                }
            }
        }
    } else if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        m.yaw -= d.x * 0.4f;
        m.pitch = std::clamp(m.pitch + d.y * 0.4f, -85.0f, 85.0f);
    }
    orbit_move(m.target, m.yaw, m.pitch, m.distance, static_cast<float>(h), hovered);
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) m.distance *= std::pow(0.9f, ImGui::GetIO().MouseWheel);
}

void draw_details(App& app) {
    const auto lang = app.language;
    const std::vector<Marker> shown_markers = showing_built() ? markers(m.rep.built) : m.markers;
    if (m.picked_marker >= static_cast<int>(shown_markers.size())) m.picked_marker = -1;
    if (m.picked_marker >= 0 && showing_built()) {
        // moved on the new room: where the change puts it
        Replacing& p = m.rep;
        const Marker& mk = shown_markers[static_cast<std::size_t>(m.picked_marker)];
        float at[3] = {static_cast<float>(mk.place[0]) * kFx, static_cast<float>(mk.place[1]) * kFx, static_cast<float>(mk.place[2]) * kFx};
        float facing = static_cast<float>(mk.extra) * 360.0f / 65536.0f;
        bool moved = false;
        ImGui::TextUnformatted(mk.name.c_str());
        if (mk.name.rfind("pent", 0) == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(lang, S::map_marker_entry));
        }
        ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
        ImGui::DragFloat3(tr(lang, S::map_marker_place), at, 0.02f, -100000.0f, 100000.0f, "%.2f");
        moved |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
        ImGui::DragFloat(tr(lang, S::map_marker_facing), &facing, 1.0f, -360.0f, 360.0f, "%.0f");
        moved |= ImGui::IsItemDeactivatedAfterEdit();
        same_line_if_room(button_width(tr(lang, S::map_to_ground)));
        if (ImGui::SmallButton(tr(lang, S::map_to_ground))) {
            double y = 0;
            if (ground_under(p.built, at[0] * 4096.0, at[2] * 4096.0, at[1] * 4096.0 + 4096.0, &y) ||
                ground_under(p.built, at[0] * 4096.0, at[2] * 4096.0, 1e18, &y)) {
                at[1] = static_cast<float>(y / 4096.0 + 0.1);  // as the game's sit, a tenth above
                moved = true;
            }
        }
        if (moved) {
            p.r.markers[mk.name] = {static_cast<std::int32_t>(std::lround(at[0] * 4096.0)), static_cast<std::int32_t>(std::lround(at[1] * 4096.0)),
                                    static_cast<std::int32_t>(std::lround(at[2] * 4096.0)),
                                    static_cast<std::int32_t>(std::lround(std::fmod(facing + 360.0f, 360.0f) * 65536.0f / 360.0f))};
            p.dirty = true;
        }
    } else if (m.picked_marker >= 0) {
        const Marker& mk = m.markers[static_cast<std::size_t>(m.picked_marker)];
        ImGui::TextUnformatted(fmt(lang, S::map_marker_picked,
                                   {mk.name, std::to_string(static_cast<float>(mk.place[0]) * kFx), std::to_string(static_cast<float>(mk.place[1]) * kFx),
                                    std::to_string(static_cast<float>(mk.place[2]) * kFx),
                                    std::to_string(static_cast<double>(mk.extra) * 360.0 / 65536.0)})
                                   .c_str());
        if (mk.name.rfind("pent", 0) == 0) ImGui::TextDisabled("%s", tr(lang, S::map_marker_entry));
    } else if (m.picked_face >= 0) {
        const RoomFace& f = shown().faces[static_cast<std::size_t>(m.picked_list)][static_cast<std::size_t>(m.picked_face)];
        const S list = m.picked_list == kGround ? S::map_grounds : m.picked_list == kWall ? S::map_walls : S::map_ceilings;
        const std::string names = names_of(m.picked_list, f);
        ImGui::TextUnformatted(fmt(lang, S::map_face_picked, {tr(lang, list), std::to_string(m.picked_face), std::to_string(f.corners),
                                                              names.empty() ? std::string("-") : names})
                                   .c_str());
        if (showing_built()) {
            // the face of the new room: its triangles take a name
            Replacing& p = m.rep;
            const auto& lists = p.report.face_triangles[static_cast<std::size_t>(m.picked_list)];
            const std::vector<std::size_t> triangles =
                static_cast<std::size_t>(m.picked_face) < lists.size() ? lists[static_cast<std::size_t>(m.picked_face)] : std::vector<std::size_t>{};
            if (triangles.empty()) {
                ImGui::TextDisabled("%s", tr(lang, S::map_face_kept));
            } else {
                ImGui::TextDisabled("%s", fmt(lang, S::map_face_made, {std::to_string(triangles.size())}).c_str());
                const auto choices = name_choices();
                ImGui::SetNextItemWidth(10 * ImGui::GetFontSize());
                if (ImGui::BeginCombo(tr(lang, S::map_give_name), tr(lang, S::map_add_name))) {
                    for (const auto& c : choices) {
                        if (ImGui::Selectable(c.c_str())) {
                            for (const auto t : triangles) {
                                auto& given = p.r.names_by_triangle[t];
                                if (std::find(given.begin(), given.end(), c) == given.end()) given.push_back(c);
                            }
                            p.dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                same_line_if_room(button_width(tr(lang, S::map_clear_names)));
                if (ImGui::SmallButton(tr(lang, S::map_clear_names))) {
                    for (const auto t : triangles) p.r.names_by_triangle.erase(t);
                    p.dirty = true;
                }
            }
        }
    } else {
        ImGui::TextDisabled("%s", tr(lang, S::map_pick_hint));
    }
}

void draw_lists(App& app) {
    const auto lang = app.language;
    if (ImGui::CollapsingHeader(fmt(lang, S::map_named_list, {std::to_string(shown().named.size())}).c_str())) {
        for (std::size_t k = 0; k < shown().named.size(); ++k) {
            const std::string name = shown().named[k].text();
            int on[3] = {0, 0, 0};
            for (int l = 0; l < 3; ++l) {
                for (const auto& f : shown().faces[static_cast<std::size_t>(l)]) {
                    for (std::size_t p = 0; p < 4; ++p) on[l] += f.slots[p] == k && slot_names(l, p) ? 1 : 0;
                }
            }
            ImGui::BulletText("%s", fmt(lang, S::map_named_row, {name, tr(lang, named_kind(name)), std::to_string(on[0]), std::to_string(on[1]),
                                                                std::to_string(on[2])})
                                        .c_str());
            if (const char* readers = tag_readers(name)) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", fmt(lang, S::map_read_by, {readers}).c_str());
            } else if (named_kind(name) == S::map_kind_tag || named_kind(name) == S::map_kind_other) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", tr(lang, S::map_not_read));
            }
        }
    }
    if (ImGui::CollapsingHeader(fmt(lang, S::map_markers_list, {std::to_string(m.markers.size())}).c_str())) {
        for (std::size_t k = 0; k < m.markers.size(); ++k) {
            const Marker& mk = m.markers[k];
            if (ImGui::Selectable((mk.name + "##mk" + std::to_string(k)).c_str(), static_cast<int>(k) == m.picked_marker)) {
                m.picked_marker = static_cast<int>(k);
                m.picked_face = m.picked_list = -1;
            }
        }
    }
    if (ImGui::CollapsingHeader(fmt(lang, S::map_exits_list, {std::to_string(m.room->links.size())}).c_str())) {
        // the row's target as the data holds it: which room it names is not read yet
        for (const auto& l : m.room->links) {
            ImGui::BulletText("%s", fmt(lang, S::map_exit_row, {std::to_string(l.key), std::to_string(l.target)}).c_str());
        }
    }
}

// The names (the original's exits and walls that open, the materials' names) and the exits' models.
void draw_names_and_exits(App& app) {
    const auto lang = app.language;
    Replacing& p = m.rep;
    // names: the original's exits and walls that open, the materials' names
    if (ImGui::TreeNode(tr(lang, S::map_names_title))) {
        ImGui::TextWrapped("%s", tr(lang, S::map_names_hint));
        for (std::size_t k = 0; k < m.collision.named.size(); ++k) {
            const std::string name = m.collision.named[k].text();
            const S kind = named_kind(name);
            if (kind != S::map_kind_gate && kind != S::map_kind_wall) continue;
            bool yours = false;
            for (const auto& [material, names] : p.r.names_by_material) yours = yours || std::find(names.begin(), names.end(), name) != names.end();
            for (const auto& [t, names] : p.r.names_by_triangle) yours = yours || std::find(names.begin(), names.end(), name) != names.end();
            if (yours) {
                ImGui::BulletText("%s", fmt(lang, S::map_on_your_faces, {name}).c_str());
                continue;
            }
            bool keep = p.r.drop_original.count(name) == 0;
            if (ImGui::Checkbox(fmt(lang, S::map_keep_original, {name}).c_str(), &keep)) {
                if (keep) p.r.drop_original.erase(name);
                else p.r.drop_original.insert(name);
                p.dirty = true;
            }
        }
        const auto choices = name_choices();
        for (const auto& material : collision_materials()) {
            ImGui::PushID(material.c_str());
            auto& names = p.r.names_by_material[material];
            std::string list;
            for (const auto& n : names) list += (list.empty() ? "" : ", ") + n;
            ImGui::TextUnformatted(fmt(lang, S::map_material_names, {material.empty() ? std::string("-") : material,
                                                                     list.empty() ? std::string(tr(lang, S::map_none)) : list})
                                       .c_str());
            same_line_if_room(9 * ImGui::GetFontSize());
            ImGui::SetNextItemWidth(9 * ImGui::GetFontSize());
            if (ImGui::BeginCombo("##add", tr(lang, S::map_add_name))) {
                for (const auto& c : choices) {
                    if (ImGui::Selectable(c.c_str()) && std::find(names.begin(), names.end(), c) == names.end()) {
                        names.push_back(c);
                        p.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (!names.empty()) {
                same_line_if_room(button_width(tr(lang, S::map_clear_names)));
                if (ImGui::SmallButton(tr(lang, S::map_clear_names))) {
                    names.clear();
                    p.dirty = true;
                }
            }
            if (names.empty()) p.r.names_by_material.erase(material);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // the exits' models of this room: where they stand
    if (ImGui::TreeNode(tr(lang, S::map_gate_models))) {
        for (const auto& link : m.room->links) {
            for (const auto& g : m.world.gate_models) {
                if (g.key != static_cast<std::int8_t>(link.key)) continue;
                for (std::size_t part = 0; part < 5; ++part) {
                    if (g.parts[part].member == 0) continue;
                    const std::pair<int, int> id{g.key, static_cast<int>(part)};
                    const auto moved = p.r.gate_points.find(id);
                    const auto at = moved != p.r.gate_points.end() ? moved->second : g.parts[part].position;
                    float v[3] = {static_cast<float>(at[0]) * kFx, static_cast<float>(at[1]) * kFx, static_cast<float>(at[2]) * kFx};
                    ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
                    if (ImGui::DragFloat3(fmt(lang, S::map_gate_part, {std::to_string(g.key), std::to_string(part), std::to_string(g.parts[part].member)}).c_str(),
                                          v, 0.02f, -100000.0f, 100000.0f, "%.2f")) {
                        p.r.gate_points[id] = {static_cast<std::int32_t>(std::lround(v[0] * 4096.0)), static_cast<std::int32_t>(std::lround(v[1] * 4096.0)),
                                               static_cast<std::int32_t>(std::lround(v[2] * 4096.0))};
                    }
                }
            }
        }
        ImGui::TreePop();
    }
}

// The size of what a model spans, words for a person: "12.4 wide, 3.1 high, 9.0 deep".
std::string span_text(App& app, const std::array<float, 3>& lo, const std::array<float, 3>& hi) {
    char w[32], h[32], d[32];
    std::snprintf(w, sizeof(w), "%.1f", hi[0] - lo[0]);
    std::snprintf(h, sizeof(h), "%.1f", hi[1] - lo[1]);
    std::snprintf(d, sizeof(d), "%.1f", hi[2] - lo[2]);
    return fmt(app.language, S::map_span, {w, h, d});
}

// The room's own points (its models at rest, as drawn): what a model made from it is lined up with.
std::vector<std::array<float, 3>> game_points() {
    std::vector<std::array<float, 3>> out;
    for (const auto& rm : m.models) {
        if (!rm.problem.empty()) continue;
        for (const auto& mesh : rm.game.model.meshes) {
            for (const auto& v : mesh.vertices) {
                std::array<float, 3> pos{0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    const float wgt = v.weights[static_cast<std::size_t>(k)];
                    const std::uint32_t jt = v.joints[static_cast<std::size_t>(k)];
                    if (wgt <= 0.0f || jt >= rm.palette.size()) continue;
                    const auto qp = mat_point(rm.palette[jt], v.position);
                    for (std::size_t c = 0; c < 3; ++c) pos[c] += wgt * qp[c];
                }
                out.push_back(pos);
            }
        }
    }
    return out;
}

// The modder's model lined up with the room when it is made from it, and the person told what was done.
void line_up(App& app) {
    Replacing& p = m.rep;
    if (p.raw.model.meshes.empty()) return;
    const RoomAlignment a = align_with_room(game_points(), p.raw);
    char share[16];
    std::snprintf(share, sizeof(share), "%.0f", a.share * 100.0);
    if (a.verdict == RoomAlignment::turned) {
        p.r.place = a.place;
        char turn[64], scale[32];
        std::snprintf(turn, sizeof(turn), "%.0f, %.0f, %.0f", a.place.turn[0], a.place.turn[1], a.place.turn[2]);
        std::snprintf(scale, sizeof(scale), "%g", a.place.scale);
        p.placed_text = fmt(app.language, S::map_align_turned, {turn, scale, share});
        p.placed_ok = true;
    } else if (a.verdict == RoomAlignment::in_place) {
        p.r.place = RoomPlacement{};
        p.placed_text = fmt(app.language, S::map_align_in_place, {share});
        p.placed_ok = true;
    } else {
        // the sizes, to place it by hand
        std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f}, rlo = lo, rhi = hi;
        assets::ImportedModel base = p.raw;
        place_model(&base, RoomPlacement{});
        for (const auto& mesh : base.model.meshes) {
            for (const auto& v : mesh.vertices) {
                for (std::size_t c = 0; c < 3; ++c) lo[c] = std::min(lo[c], v.position[c]), hi[c] = std::max(hi[c], v.position[c]);
            }
        }
        for (const auto& list : m.collision.faces) {
            for (const auto& f : list) {
                for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                    for (std::size_t c = 0; c < 3; ++c) {
                        rlo[c] = std::min(rlo[c], static_cast<float>(f.v[v][c]) * kFx);
                        rhi[c] = std::max(rhi[c], static_cast<float>(f.v[v][c]) * kFx);
                    }
                }
            }
        }
        p.placed_text = fmt(app.language, S::map_align_unrelated, {span_text(app, lo, hi), span_text(app, rlo, rhi)});
        p.placed_ok = false;
    }
    p.dirty = true;
}

void choose_room_export(App& app) {
    static const SDL_DialogFileFilter gltf[] = {{"glTF", "gltf"}};
    SDL_ShowSaveFileDialog(
        [](void* user, const char* const* files, int) {
            if (files == nullptr || files[0] == nullptr) return;
            App* a = static_cast<App*>(user);
            std::lock_guard lock(a->dialog_mutex);
            a->dialog_room_exports.emplace_back(files[0]);
        },
        &app, app.window, gltf, 1, nullptr);
}

void draw_replace(App& app, const Item& item) {
    const auto lang = app.language;
    Replacing& p = m.rep;
    const ImVec4 warn(0.95f, 0.8f, 0.35f, 1), good(0.55f, 0.9f, 0.55f, 1);
    if (!ImGui::CollapsingHeader(tr(lang, S::map_replace), p.active ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    const Mod* mod = app.current_mod();

    // 1. the model: the person's, or the game's room exported to start from
    ImGui::SeparatorText(tr(lang, S::map_step_model));
    ImGui::TextWrapped("%s", tr(lang, S::map_replace_hint));
    if (ImGui::Button(tr(lang, S::choose_file))) {
        p.next_is_collision = false;
        choose_file(app, item);
    }
    same_line_if_room(button_width(tr(lang, S::map_export)));
    if (ImGui::Button(tr(lang, S::map_export))) choose_room_export(app);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(lang, S::map_export_hint));
    if (mod == nullptr) {
        ImGui::TextColored(warn, "%s", tr(lang, S::choose_mod_first));
        return;
    }
    if (!p.active) return;
    ImGui::TextUnformatted(fmt(lang, S::map_model_file, {p.model_source.empty() ? std::string("-") : u8(p.model_source.filename())}).c_str());
    const char* views[3] = {tr(lang, S::map_show_new), tr(lang, S::map_show_game), tr(lang, S::map_show_both)};
    same_line_if_room(12 * ImGui::GetFontSize() + check_width(tr(lang, S::map_show)));
    ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
    ImGui::Combo(tr(lang, S::map_show), &p.view, views, 3);

    // 2. where it sits: lined up by the tool when it can, the person's hand otherwise
    ImGui::SeparatorText(tr(lang, S::map_step_place));
    if (!p.placed_text.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.placed_ok ? good : warn);
        ImGui::TextWrapped("%s", p.placed_text.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::Button(tr(lang, S::map_align_button))) line_up(app);
    bool changed = false;
    ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
    ImGui::DragFloat(tr(lang, S::map_scale), &p.r.place.scale, 0.01f, 0.001f, 1000.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
    ImGui::DragFloat3(tr(lang, S::map_turn), p.r.place.turn.data(), 0.5f, -360.0f, 360.0f, "%.1f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
    ImGui::DragFloat3(tr(lang, S::map_move), p.r.place.move.data(), 0.05f, -100000.0f, 100000.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (p.r.place.scale <= 0.0f) p.r.place.scale = 0.001f;

    // 3. the floor and walls: the game's kept, made from the model, or from a simpler one
    ImGui::SeparatorText(tr(lang, S::map_step_collision));
    int source = p.r.keep_collision ? 0 : p.other_collision || !p.collision_source.empty() ? 2 : 1;
    const int before = source;
    ImGui::RadioButton(tr(lang, S::map_collision_keep), &source, 0);
    ImGui::RadioButton(tr(lang, S::map_collision_mine), &source, 1);
    ImGui::RadioButton(tr(lang, S::map_collision_other), &source, 2);
    if (source == 2) {
        ImGui::SameLine();
        ImGui::TextUnformatted(p.collision_source.empty() ? "-" : u8(p.collision_source.filename()).c_str());
        same_line_if_room(button_width(tr(lang, S::map_choose_collision)));
        if (ImGui::SmallButton(tr(lang, S::map_choose_collision))) {
            p.next_is_collision = true;
            choose_file(app, item);
        }
    }
    if (source != before) {
        p.r.keep_collision = source == 0;
        p.other_collision = source == 2;
        if (source != 2) {
            p.collision_source.clear();
            p.raw_collision = {};
            p.r.collision.clear();
        }
        p.dirty = true;
    }

    // advanced: the steepest ground, the room's other models, names, the exits' models
    if (ImGui::TreeNode(tr(lang, S::map_advanced))) {
        float degrees = std::acos(std::clamp(p.r.ground_limit, -1.0f, 1.0f)) * 57.2957795f;
        ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
        if (ImGui::DragFloat(tr(lang, S::map_ground_limit), &degrees, 0.1f, 1.0f, 89.0f, "%.1f")) {
            p.r.ground_limit = std::cos(std::clamp(degrees, 1.0f, 89.0f) / 57.2957795f);
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        const char* uses[3] = {tr(lang, S::map_use_model), tr(lang, S::map_use_hide), tr(lang, S::map_use_keep)};
        for (const auto& [member, use] : object_uses(*m.room, p.r)) {
            std::string name = std::to_string(member);
            for (const auto& rm : m.models) {
                if (rm.member == member && !rm.key.empty()) name = rm.key.substr(rm.key.rfind('/') + 1);
            }
            int choice = use == ObjectUse::model ? 0 : use == ObjectUse::hide ? 1 : 2;
            ImGui::SetNextItemWidth(10 * ImGui::GetFontSize());
            if (ImGui::Combo(fmt(lang, S::map_object, {std::to_string(member), name}).c_str(), &choice, uses, 3)) {
                p.r.objects[member] = choice == 0 ? ObjectUse::model : choice == 1 ? ObjectUse::hide : ObjectUse::keep;
                p.dirty = true;
            }
        }
        draw_names_and_exits(app);
        ImGui::TreePop();
    }
    if (changed) p.dirty = true;
    if (p.dirty) rebuild();

    if (p.built_ok && !p.r.keep_collision) {
        ImGui::TextDisabled("%s", fmt(lang, S::map_built, {std::to_string(p.report.faces[kGround]), std::to_string(p.report.faces[kWall]),
                                                           std::to_string(p.report.faces[kCeiling]), std::to_string(p.report.nodes),
                                                           std::to_string(p.report.bytes)})
                                      .c_str());
    } else if (p.built_ok) {
        ImGui::TextDisabled("%s", tr(lang, S::map_collision_kept_note));
    }
    for (const auto& w : p.warnings) {
        // in RoomWarning::Kind's order
        static const S words[] = {S::map_warn_dropped,        S::map_warn_too_big,       S::map_warn_no_ground,
                                  S::map_warn_gate_unplaced,  S::map_warn_wall_unplaced, S::map_warn_name_kept,
                                  S::map_warn_marker_floating, S::map_warn_collision_shared, S::map_warn_model_shared,
                                  S::map_warn_passable};
        static_assert(sizeof(words) / sizeof(words[0]) == RoomWarning::passable + 1, "a word per warning");
        std::string text = tr(lang, words[w.kind]);
        for (std::size_t k = 0; k < w.args.size(); ++k) {
            const std::string key = "{" + std::to_string(k) + "}";
            for (auto at = text.find(key); at != std::string::npos; at = text.find(key)) text.replace(at, key.size(), w.args[k]);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, warn);
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    }
    if (!p.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
        ImGui::TextWrapped("%s", p.error.c_str());
        ImGui::PopStyleColor();
    }

    // 4. saved into the mod (or the room given back to the game)
    ImGui::SeparatorText(tr(lang, S::map_step_save));
    const fs::path dump = resolve(app.settings, app.settings.graphics_dump);
    ImGui::BeginDisabled(!p.built_ok);
    if (ImGui::Button(tr(lang, S::map_save))) {
        WorldOutcome o;
        std::string why;
        p.r.world = m.world.code;
        p.r.room = m.room->index;
        if (save_change(nitrofs(), dump, mod->dir, p.r, p.model_source, p.collision_source, &o, &why)) {
            // from now on, the mod's copies
            const fs::path folder = change_models(mod->dir, m.world.code, m.room->index);
            p.model_source = folder / p.model_source.filename();
            if (!p.collision_source.empty()) p.collision_source = folder / p.collision_source.filename();
            p.from_mod = true;
            p.message = fmt(lang, S::map_saved, {mod->name});
            app.tell(p.message + "  " + tr(lang, S::restart_note), true);
        } else {
            p.error = why;
            app.tell(fmt(lang, S::save_failed, {why}), false);
        }
    }
    ImGui::EndDisabled();
    if (p.from_mod) {
        same_line_if_room(button_width(tr(lang, S::map_revert)));
        if (ImGui::Button(tr(lang, S::map_revert))) {
            WorldOutcome o;
            std::string why;
            if (revert_change(nitrofs(), dump, mod->dir, m.world.code, m.room->index, &o, &why)) {
                m.rep = Replacing{};
                m.rep.message = fmt(lang, S::map_reverted, {mod->name});
                app.tell(m.rep.message, true);
            } else {
                p.error = why;
                app.tell(fmt(lang, S::save_failed, {why}), false);
            }
        }
    }
    if (!m.rep.message.empty()) ImGui::TextDisabled("%s", m.rep.message.c_str());
}

}  // namespace

void room_offer(App& app, const Item& item, const fs::path& file) {
    (void)item;
    if (m.item != app.selected || m.mod != app.mod) open(app);
    if (m.room == nullptr) return;
    Replacing& p = m.rep;
    const bool collision = p.next_is_collision;
    p.next_is_collision = false;
    assets::ImportedModel model;
    std::string why;
    if (!import_model(file, &model, &why)) {
        p.error = u8(file.filename()) + ": " + why;
        return;
    }
    if (collision && p.active) {
        p.collision_source = file;
        p.raw_collision = std::move(model);
        p.other_collision = true;
    } else {
        p.model_source = file;
        p.raw = std::move(model);
    }
    if (!p.active) {
        p.active = true;
        p.r.world = m.world.code;
        p.r.room = m.room->index;
    }
    p.message.clear();
    p.dirty = true;
    if (!collision) line_up(app);
}

void room_export_to(App& app, const fs::path& file) {
    if (m.room == nullptr) return;
    std::vector<std::string> notes;
    std::string why;
    fs::path out = file;
    if (lower(u8(out.extension())) != ".gltf") out += ".gltf";
    if (!export_room(nitrofs(), resolve(app.settings, app.settings.graphics_dump), m.world, m.room->index, out, &notes, &why)) {
        app.tell(fmt(app.language, S::save_failed, {why}), false);
        return;
    }
    app.tell(fmt(app.language, S::map_exported, {u8(out)}), true);
    for (const auto& n : notes) app.tell(n, false);
}

void draw_map_view(App& app, const Item& item) {
    (void)item;
    if (m.item != app.selected || m.mod != app.mod) open(app);
    const auto lang = app.language;
    if (!m.error.empty() && m.room == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", m.error.c_str());
        return;
    }
    ImGui::TextUnformatted(fmt(lang, S::map_room, {std::to_string(m.room->index), std::to_string(m.room->id), world_path(m.world.code)}).c_str());
    if (m.has_collision) {
        ImGui::TextDisabled("%s", fmt(lang, S::map_counts, {std::to_string(shown().faces[kGround].size()), std::to_string(shown().faces[kWall].size()),
                                                            std::to_string(shown().faces[kCeiling].size()), std::to_string(m.markers.size()),
                                                            std::to_string(shown().named.size())})
                                      .c_str());
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1), "%s", m.error.empty() ? tr(lang, S::map_no_collision) : m.error.c_str());
    }
    for (const auto& rm : m.models) {
        if (!rm.problem.empty()) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1), "%s", rm.problem.c_str());
    }
    ImGui::Checkbox(tr(lang, S::map_models), &m.show_models);
    same_line_if_room(check_width(tr(lang, S::map_grounds)));
    ImGui::Checkbox(tr(lang, S::map_grounds), &m.show_ground);
    same_line_if_room(check_width(tr(lang, S::map_walls)));
    ImGui::Checkbox(tr(lang, S::map_walls), &m.show_walls);
    same_line_if_room(check_width(tr(lang, S::map_ceilings)));
    ImGui::Checkbox(tr(lang, S::map_ceilings), &m.show_ceilings);
    same_line_if_room(check_width(tr(lang, S::map_markers)));
    ImGui::Checkbox(tr(lang, S::map_markers), &m.show_markers);
    if (m.rep.dirty) rebuild();  // the modder's model placed before the picture draws it
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    draw_room(app, ImVec2(avail.x, std::max(260.0f, avail.y * 0.5f)));
    ImGui::TextDisabled("%s", tr(lang, S::map_controls));
    ImGui::BeginChild("##room_controls", ImVec2(0, 0), ImGuiChildFlags_None);
    draw_replace(app, item);
    draw_details(app);
    draw_lists(app);
    ImGui::EndChild();
}

}  // namespace studio
