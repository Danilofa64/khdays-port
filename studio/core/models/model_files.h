// A mod's model and the files it needs beside it: a glTF's buffers and
// images, an FBX's textures -- found as the port's importers find them
// (assets/src/image.cpp find_texture: the stored path relative to the model's
// folder, then as it is, then its file name beside the model).
#pragma once

#include "khdays/assets/model_import.h"

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct Companion {
    std::filesystem::path source;  // where it is now
    std::string relative;          // where it goes, relative to the model's folder ('/' separators)
};

// The files a model file needs beside it; `why` holds what could not be
// found (each texture the port would draw white).
std::vector<Companion> model_companions(const std::filesystem::path& model, std::vector<std::string>* not_found);

// The model read as the port reads it (import_gltf / import_fbx by the
// extension); false with the reason.
bool import_model(const std::filesystem::path& model, khdays::assets::ImportedModel* out, std::string* why);

// Whether an extension is one the port takes for a model (.gltf, .glb, .fbx).
bool model_extension(const std::string& lower_extension);

}  // namespace studio
