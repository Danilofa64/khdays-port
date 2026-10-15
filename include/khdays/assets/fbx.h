#pragma once

#include <filesystem>

#include "khdays/assets/model_import.h"

namespace khdays::assets {

// Import a binary or ASCII .fbx file (ufbx) into the same neutral
// form as import_gltf, in glTF's frame: its units and axes converted to metres
// and Y up whatever the file says. Its textures embedded, or from files found
// by image.h's search. Its animations, cameras and lights are not read. Throws
// std::runtime_error on failure.
ImportedModel import_fbx(const std::filesystem::path& input_path);

}  // namespace khdays::assets
