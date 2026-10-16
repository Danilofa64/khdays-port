#include "core/models/model_files.h"

#include "core/files/paths.h"

#include "khdays/assets/fbx.h"
#include "khdays/assets/gltf.h"
#include "khdays/assets/image.h"

#include "cgltf.h"
#include "ufbx.h"

#include <cstring>
#include <memory>
#include <set>

namespace studio {

namespace {

std::string text(ufbx_string s) {
    return std::string(s.data, s.length);
}

}  // namespace

bool model_extension(const std::string& ext) {
    return ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj";
}

bool import_model(const fs::path& model, khdays::assets::ImportedModel* out, std::string* why) {
    try {
        const std::string ext = lower(u8(model.extension()));
        // ufbx reads an OBJ (with its .mtl) as it reads an FBX
        *out = ext == ".fbx" || ext == ".obj" ? khdays::assets::import_fbx(model) : khdays::assets::import_gltf(model);
        return true;
    } catch (const std::exception& e) {
        if (why != nullptr)
            *why = e.what();
        return false;
    }
}

std::vector<Companion> model_companions(const fs::path& model, std::vector<std::string>* not_found) {
    std::vector<Companion> out;
    std::set<std::string> taken;
    const fs::path folder = model.parent_path();
    const std::string ext = lower(u8(model.extension()));
    auto add = [&](const fs::path& source, const std::string& relative) {
        if (taken.insert(relative).second)
            out.push_back({source, relative});
    };

    if (ext == ".gltf" || ext == ".glb") {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        const std::string path = u8(model);
        if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
            return out;
        const std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);
        auto external = [&](const char* uri) {
            if (uri == nullptr || std::strncmp(uri, "data:", 5) == 0)
                return;
            std::string relative(uri);
            cgltf_decode_uri(relative.data());
            relative.resize(std::strlen(relative.c_str()));
            const auto file = khdays::assets::find_texture(folder, relative);
            if (file) {
                // kept at the place the file names it, so the model finds it there
                add(*file, fs::path(path_u8(relative)).is_absolute() ? u8(file->filename()) : relative);
            } else if (not_found != nullptr) {
                not_found->push_back(relative);
            }
        };
        for (cgltf_size i = 0; i < data->buffers_count; ++i)
            external(data->buffers[i].uri);
        for (cgltf_size i = 0; i < data->images_count; ++i)
            external(data->images[i].uri);
        return out;
    }

    if (ext == ".fbx") {
        ufbx_load_opts options{};
        ufbx_error error{};
        const std::string path = u8(model);
        ufbx_scene* scene = ufbx_load_file_len(path.data(), path.size(), &options, &error);
        if (scene == nullptr)
            return out;
        const std::unique_ptr<ufbx_scene, void (*)(ufbx_scene*)> guard(scene, ufbx_free_scene);
        for (std::size_t i = 0; i < scene->textures.count; ++i) {
            const ufbx_texture* t = scene->textures.data[i];
            if (t->content.size > 0)
                continue;  // inside the file
            // fbx.cpp's order: relative, absolute, the bare file name
            auto file = khdays::assets::find_texture(folder, text(t->relative_filename));
            if (!file)
                file = khdays::assets::find_texture(folder, text(t->absolute_filename));
            if (!file)
                file = khdays::assets::find_texture(folder, text(t->filename));
            if (file) {
                add(*file, u8(file->filename()));  // beside the model: found there by its name
            } else if (not_found != nullptr && !text(t->filename).empty()) {
                not_found->push_back(text(t->filename));
            }
        }
    }
    return out;
}

}  // namespace studio
