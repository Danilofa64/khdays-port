#include "core/rooms/kh2_source.h"

#include "core/files/paths.h"

#include "khdays/assets/gltf_export.h"
#include "khdays/assets/kh2_room.h"

extern "C" {
#include "render/png_io.h"
}

#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace studio {

fs::path kh2_folder(const fs::path& root) {
    return root / "saves" / "collection" / "kh2";
}

bool make_kh2_room(const fs::path& folder, const std::string& map, const fs::path& out, Kh2Made* made,
                   std::string* why) {
    const fs::path file = folder / "original" / "map" / path_u8(map + ".map");
    const fs::path hd = folder / "remastered" / "map" / path_u8(map + ".map");
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        *why = "no " + u8(file) + " (the map is taken out of the collection first)";
        return false;
    }
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::error_code ec;
    if (!fs::is_directory(hd, ec))
        made->report.push_back("no remastered pictures in " + u8(hd) + ": surfaces in their vertex colours");
    fs::create_directories(out.parent_path(), ec);
    std::string failed;
    khdays::assets::kh2::Room room;
    try {
        room = khdays::assets::kh2::room_model(
            data.data(), data.size(), hd, map,
            [&](const std::string& name, const khdays::assets::DecodedTexture& picture) {
                const char* e = khdays_png_write(u8(out.parent_path() / path_u8(name)).c_str(), picture.rgba.data(),
                                                 picture.width, picture.height);
                if (e != nullptr && failed.empty())
                    failed = name + ": " + e;
                ++made->pictures;
            });
    } catch (const std::exception& e) {
        *why = u8(file) + ": " + e.what();
        return false;
    }
    if (!failed.empty()) {
        *why = "a picture cannot be written: " + failed;
        return false;
    }
    std::ofstream gltf(out, std::ios::binary);
    gltf << khdays::assets::scene_to_gltf(room.scene);
    if (!gltf) {
        *why = "cannot write " + u8(out);
        return false;
    }
    for (const auto& mesh : room.scene.meshes) {
        for (const auto& p : mesh.primitives)
            made->triangles += p.indices.size() / 3;
        made->pieces += mesh.joints.empty() ? 0 : 1;
    }
    made->animations = room.scene.animations.size();
    for (const auto& m : room.scene.materials) {
        made->scrolling += m.scroll[0] != 0.0F || m.scroll[1] != 0.0F ? 1 : 0;
        made->framed += m.frames.empty() ? 0 : 1;
    }
    made->report.insert(made->report.end(), room.report.begin(), room.report.end());
    return true;
}

}  // namespace studio
