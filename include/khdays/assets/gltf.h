#pragma once

#include <filesystem>

#include "khdays/assets/model_import.h"

namespace khdays::assets {

// Import a .gltf or .glb file: its textures from files beside it (PNG, JPEG,
// TGA, BMP; image.h) or embedded (a buffer view or a data URI). Throws
// std::runtime_error on failure.
ImportedModel import_gltf(const std::filesystem::path& input_path);

}  // namespace khdays::assets
