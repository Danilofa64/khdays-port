#include "core/mods/mods.h"

#include "core/models/model_files.h"
#include "core/files/paths.h"

#include "khdays/vfs/filesystem.h"

#include <algorithm>

namespace studio {

std::vector<Mod> list_mods(const fs::path& mods_root) {
    // The same walk and order as khdays::vfs::find_in_mods.
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(mods_root, ec)) {
        std::error_code type_ec;
        if (entry.is_directory(type_ec))
            dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    std::vector<Mod> mods;
    for (const auto& d : dirs)
        mods.push_back({u8(d.filename()), d});
    return mods;
}

bool valid_mod_name(const std::string& name, std::string* why) {
    auto no = [&](const char* reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    if (name.empty())
        return no("empty");
    if (name == "." || name == "..")
        return no("reserved");
    for (const unsigned char c : name) {
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            return no("character");
        }
    }
    if (name.back() == ' ' || name.back() == '.')
        return no("ending");
    return true;
}

bool create_mod(const fs::path& mods_root, const std::string& name, std::string* why) {
    if (!valid_mod_name(name, why))
        return false;
    const fs::path dir = mods_root / path_u8(name);
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        if (why != nullptr)
            *why = "exists";
        return false;
    }
    fs::create_directories(dir, ec);
    if (ec) {
        if (why != nullptr)
            *why = ec.message();
        return false;
    }
    return true;
}

std::vector<std::string> lookup_paths(const Item& item) {
    switch (item.kind) {
    case Kind::texture: {
        std::vector<std::string> paths{"graphics/" + item.key + ".png"};
        const std::string any = any_palette_key(item);
        if (!any.empty())
            paths.push_back("graphics/" + any + ".png");
        return paths;
    }
    case Kind::sheet: return {"graphics/" + item.key + ".png"};
    case Kind::model:
        return {"graphics/" + item.key + ".gltf", "graphics/" + item.key + ".glb", "graphics/" + item.key + ".fbx"};
    case Kind::sound:
        return {"sounds/" + item.key + ".wav", "sounds/" + item.key + ".flac", "sounds/" + item.key + ".ogg"};
    case Kind::text_file: return {"files/" + item.key};
    case Kind::movie:  // port/runtime/movies/video_mod.c, which wins; then movie_hd.c with the movies set to HD
        return {"movies/" + item.key + ".mpg", "movies/hd" + item.key + ".mp4"};
    case Kind::room: return {"files/" + item.group};   // the world's file
    case Kind::mission: return {"files/" + item.key};  // the mission's file
    }
    return {};
}

std::string replacement_path(const Item& item, const std::string& ext, bool any_palette) {
    switch (item.kind) {
    case Kind::texture: {
        const std::string any = any_palette ? any_palette_key(item) : std::string();
        return "graphics/" + (any.empty() ? item.key : any) + ".png";
    }
    case Kind::sheet: return "graphics/" + item.key + ".png";
    case Kind::model: return "graphics/" + item.key + ext;
    case Kind::sound: return "sounds/" + item.key + ext;
    case Kind::text_file: return "files/" + item.key;
    case Kind::movie: return ext == ".mp4" ? "movies/hd" + item.key + ".mp4" : "movies/" + item.key + ".mpg";
    case Kind::room: return "files/" + item.group;
    case Kind::mission: return "files/" + item.key;
    }
    return {};
}

std::vector<Holder> holders(const fs::path& mods_root, const Item& item) {
    // The port takes the first lookup path any mod has, in mod order, before
    // trying the next path; so the holders are listed path by path.
    khdays::vfs::set_mods_root(mods_root);
    const auto mods = list_mods(mods_root);
    std::vector<Holder> out;
    for (const auto& rel : lookup_paths(item)) {
        for (const auto& mod : mods) {
            const fs::path file = mod.dir / path_u8(rel);
            std::error_code ec;
            if (fs::is_regular_file(file, ec))
                out.push_back({mod.name, file});
        }
    }
    // The first must be what the port's own lookup answers.
    if (!out.empty()) {
        for (const auto& rel : lookup_paths(item)) {
            if (const auto found = khdays::vfs::find_in_mods(path_u8(rel))) {
                const auto it = std::find_if(out.begin(), out.end(), [&](const Holder& h) {
                    std::error_code ec;
                    return fs::equivalent(h.file, *found, ec);
                });
                if (it != out.end() && it != out.begin())
                    std::rotate(out.begin(), it, it + 1);
                break;
            }
        }
    }
    return out;
}

std::vector<fs::path> files_in_mod(const fs::path& mod_dir, const Item& item) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& rel : lookup_paths(item)) {
        const fs::path file = mod_dir / path_u8(rel);
        if (fs::is_regular_file(file, ec))
            out.push_back(file);
    }
    if (item.kind == Kind::model) {
        const fs::path bones = mod_dir / path_u8("graphics/" + item.key + ".bones.txt");
        if (fs::is_regular_file(bones, ec))
            out.push_back(bones);
    }
    return out;
}

namespace {

bool is_model_file(const fs::path& file) {
    std::string ext = u8(file.extension());
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".gltf" || ext == ".glb" || ext == ".fbx";
}

std::string compare_key(const fs::path& file) {
    std::string k = u8(file.lexically_normal());
    for (auto& c : k) {
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return k;
}

}  // namespace

std::vector<fs::path> used_companions(const fs::path& mod_dir) {
    std::vector<fs::path> used;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(mod_dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || !is_model_file(it->path()))
            continue;
        for (const auto& c : model_companions(it->path(), nullptr))
            used.push_back(c.source);
        fs::path bones = it->path();
        bones.replace_extension(".bones.txt");
        used.push_back(bones);
    }
    return used;
}

bool among(const std::vector<fs::path>& used, const fs::path& file) {
    const std::string k = compare_key(file);
    return std::any_of(used.begin(), used.end(), [&](const fs::path& u) { return compare_key(u) == k; });
}

bool remove_mod_file(const fs::path& mod_dir, const fs::path& file, std::string* why) {
    if (!inside(mod_dir, file)) {
        if (why != nullptr)
            *why = u8(file) + ": not in the mod";
        return false;
    }
    std::error_code ec;
    fs::remove(file, ec);
    if (ec) {
        if (why != nullptr)
            *why = u8(file) + ": " + ec.message();
        return false;
    }
    for (fs::path dir = file.parent_path(); inside(mod_dir, dir); dir = dir.parent_path()) {
        if (!fs::is_empty(dir, ec) || ec)
            break;
        fs::remove(dir, ec);
    }
    return true;
}

bool remove_replacement(const fs::path& mod_dir, const Item& item, std::string* why) {
    // the companions of the model files going: removed after them when nothing else names them
    std::vector<fs::path> companions;
    for (const auto& file : files_in_mod(mod_dir, item)) {
        if (!is_model_file(file))
            continue;
        for (const auto& c : model_companions(file, nullptr)) {
            if (inside(mod_dir, c.source))
                companions.push_back(c.source);
        }
    }
    for (const auto& file : files_in_mod(mod_dir, item)) {
        if (!inside(mod_dir, file))
            continue;
        std::error_code ec;
        fs::remove(file, ec);
        if (ec) {
            if (why != nullptr)
                *why = u8(file) + ": " + ec.message();
            return false;
        }
        // the folders it leaves empty, up to the mod's own
        for (fs::path dir = file.parent_path(); inside(mod_dir, dir); dir = dir.parent_path()) {
            if (!fs::is_empty(dir, ec) || ec)
                break;
            fs::remove(dir, ec);
        }
    }
    if (!companions.empty()) {
        const auto still = used_companions(mod_dir);
        for (const auto& c : companions) {
            std::error_code ec;
            if (!fs::is_regular_file(c, ec) || among(still, c))
                continue;
            if (!remove_mod_file(mod_dir, c, why))
                return false;
        }
    }
    return true;
}

}  // namespace studio
