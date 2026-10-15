// The mods under mods/, what each replaces, and which mod's file the port
// uses (the port's own khdays::vfs::find_in_mods).
#pragma once

#include "core/files/catalog.h"

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct Mod {
    std::string name;            // its folder's name
    std::filesystem::path dir;   // mods/<name>
};

// The mods in the port's order (src/vfs/filesystem.cpp find_in_mods: the
// folders' paths sorted).
std::vector<Mod> list_mods(const std::filesystem::path& mods_root);

// Whether `name` can be a mod folder's name; `why` says why not.
bool valid_mod_name(const std::string& name, std::string* why);

// Creates mods/<name>; false with the reason.
bool create_mod(const std::filesystem::path& mods_root, const std::string& name, std::string* why);

// The paths, relative to a mod, the port looks for an item at, in its order
// (graphics: textures.c lines 215-226, tiles2d.c line 89, models.cpp lines
// 573-576; sounds: sound_mods.c lines 155-160, .wav .flac .ogg; movies:
// video_mod.c line 177; text files: a whole game file under files/).
std::vector<std::string> lookup_paths(const Item& item);

// The path a new replacement of `item` is written at, relative to the mod,
// for a file of extension `ext` (".png", ".wav", ...). `any_palette`: a
// texture's form for all its palettes.
std::string replacement_path(const Item& item, const std::string& ext, bool any_palette = false);

struct Holder {
    std::string mod;              // the mod's name
    std::filesystem::path file;   // its file for the item
};

// Every mod holding a file for `item`, in the port's order of preference:
// the first is the one the port uses.
std::vector<Holder> holders(const std::filesystem::path& mods_root, const Item& item);

// The files a mod holds for `item` (any of its lookup paths, plus a
// model's companions: <model>.bones.txt).
std::vector<std::filesystem::path> files_in_mod(const std::filesystem::path& mod_dir, const Item& item);

// Removes those files (the caller has asked the modder), then the folders
// they leave empty up to the mod's own; a model's companions (the buffers and
// textures it names) go with it unless another model of the mod names them.
// False with the reason.
bool remove_replacement(const std::filesystem::path& mod_dir, const Item& item, std::string* why);

// The files the mod's models need beside them: what each model file of the
// mod names (model_companions) and each model's <model>.bones.txt. A file in
// a model's folder that is not among them is left over (its model removed).
std::vector<std::filesystem::path> used_companions(const std::filesystem::path& mod_dir);

// Whether `file` is among `used` (paths compared as Windows does: case aside).
bool among(const std::vector<std::filesystem::path>& used, const std::filesystem::path& file);

// Removes one file of the mod and the folders it leaves empty. False with the reason.
bool remove_mod_file(const std::filesystem::path& mod_dir, const std::filesystem::path& file, std::string* why);

}  // namespace studio
