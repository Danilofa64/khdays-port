#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "khdays/assets/texture.h"  // DecodedTexture

namespace khdays::assets {

// A mod model's texture: PNG, JPEG, TGA or BMP, decoded to top-down RGBA
// (stb_image). Throws std::runtime_error with the reason on failure.
DecodedTexture read_image(const std::filesystem::path& path);
DecodedTexture read_image(const std::uint8_t* bytes, std::size_t size, const std::string& name);

// Where a model's texture is: the path the model
// stores, relative to the model's folder, then as it is (absolute), then its
// file name in the model's folder -- a model often keeps the path of the
// machine it was made on. nullopt when none is a file.
std::optional<std::filesystem::path> find_texture(const std::filesystem::path& model_folder,
                                                  const std::string& stored_path);

}  // namespace khdays::assets
