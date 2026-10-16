// Pictures on the graphics card for the previews: PNGs read as the port reads
// them (png_io.c), uploaded once and kept while the memory budget allows,
// the least recently shown dropped first.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace studio {

struct GpuImage {
    SDL_GPUTexture* texture = nullptr;
    int width = 0;
    int height = 0;
    std::string error;  // why it could not be read; texture null then
};

class GpuImages {
public:
    GpuImages(SDL_GPUDevice* device, std::size_t budget_bytes);
    ~GpuImages();
    GpuImages(const GpuImages&) = delete;
    GpuImages& operator=(const GpuImages&) = delete;

    // The picture of a PNG file (read again when the file changed).
    const GpuImage& get(const std::filesystem::path& png);

    // Forgets a file's picture (it was replaced).
    void forget(const std::filesystem::path& png);

    // A picture made in memory (RGBA), under a name of its own: uploaded
    // again when `version` changes.
    const GpuImage& pixels(const std::string& name, std::uint64_t version, const std::vector<std::uint8_t>& rgba,
                           int width, int height);

    void set_budget(std::size_t bytes) {
        budget_ = bytes;
    }

    // Releases the pictures dropped during the last frame: called at the
    // start of a frame, once the last frame's drawing (which may still name
    // them) has been submitted.
    void collect();

private:
    struct Entry {
        GpuImage image;
        std::filesystem::file_time_type stamp{};
        std::uintmax_t size = 0;
        std::uint64_t used = 0;
        std::size_t bytes = 0;
        std::uint64_t version = 0;  // pictures made in memory
    };
    void trim();
    SDL_GPUTexture* upload(const std::uint8_t* rgba, int width, int height);

    SDL_GPUDevice* device_;
    std::size_t budget_;
    std::size_t total_ = 0;
    std::uint64_t clock_ = 0;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<SDL_GPUTexture*> released_;
};

}  // namespace studio
