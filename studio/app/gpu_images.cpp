#include "gpu_images.h"

#include "core/files/paths.h"

extern "C" {
#include "render/png_io.h"
}

#include <cstring>
#include <vector>

namespace studio {

GpuImages::GpuImages(SDL_GPUDevice* device, std::size_t budget_bytes) : device_(device), budget_(budget_bytes) {}

GpuImages::~GpuImages() {
    collect();
    for (auto& [key, e] : entries_) {
        if (e.image.texture != nullptr)
            SDL_ReleaseGPUTexture(device_, e.image.texture);
    }
}

void GpuImages::collect() {
    for (SDL_GPUTexture* t : released_)
        SDL_ReleaseGPUTexture(device_, t);
    released_.clear();
}

SDL_GPUTexture* GpuImages::upload(const std::uint8_t* rgba, int width, int height) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &info);
    if (texture == nullptr)
        return nullptr;

    const Uint32 bytes = static_cast<Uint32>(width) * static_cast<Uint32>(height) * 4u;
    SDL_GPUTransferBufferCreateInfo tinfo{};
    tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tinfo.size = bytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tinfo);
    if (transfer == nullptr) {
        SDL_ReleaseGPUTexture(device_, texture);
        return nullptr;
    }
    void* map = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (map == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTexture(device_, texture);
        return nullptr;
    }
    std::memcpy(map, rgba, bytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.pixels_per_row = static_cast<Uint32>(width);
    src.rows_per_layer = static_cast<Uint32>(height);
    SDL_GPUTextureRegion dst{};
    dst.texture = texture;
    dst.w = static_cast<Uint32>(width);
    dst.h = static_cast<Uint32>(height);
    dst.d = 1;
    SDL_UploadToGPUTexture(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return texture;
}

const GpuImage& GpuImages::get(const fs::path& png) {
    const std::string key = u8(png);
    std::error_code ec;
    const auto stamp = fs::last_write_time(png, ec);
    const auto size = ec ? 0 : fs::file_size(png, ec);
    auto it = entries_.find(key);
    if (it != entries_.end() && (it->second.stamp != stamp || it->second.size != size)) {
        forget(png);
        it = entries_.end();
    }
    if (it == entries_.end()) {
        Entry e;
        e.stamp = stamp;
        e.size = size;
        const char* error = nullptr;
        int w = 0, h = 0;
        std::uint8_t* pixels = khdays_png_read(key.c_str(), &w, &h, &error);
        if (pixels == nullptr) {
            e.image.error = error != nullptr ? error : "unreadable";
        } else {
            e.image.texture = upload(pixels, w, h);
            e.image.width = w;
            e.image.height = h;
            if (e.image.texture == nullptr)
                e.image.error = SDL_GetError();
            e.bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
            khdays_png_free(pixels);
        }
        total_ += e.bytes;
        it = entries_.emplace(key, std::move(e)).first;
        trim();
        it = entries_.find(key);
    }
    it->second.used = ++clock_;
    return it->second.image;
}

const GpuImage& GpuImages::pixels(const std::string& name, std::uint64_t version, const std::vector<std::uint8_t>& rgba,
                                  int width, int height) {
    const std::string key = "memory:" + name;
    auto it = entries_.find(key);
    if (it != entries_.end() && it->second.version != version) {
        if (it->second.image.texture != nullptr)
            released_.push_back(it->second.image.texture);
        total_ -= it->second.bytes;
        entries_.erase(it);
        it = entries_.end();
    }
    if (it == entries_.end()) {
        Entry e;
        e.version = version;
        if (width <= 0 || height <= 0 ||
            rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
            e.image.error = "no picture";
        } else {
            e.image.texture = upload(rgba.data(), width, height);
            e.image.width = width;
            e.image.height = height;
            if (e.image.texture == nullptr)
                e.image.error = SDL_GetError();
            e.bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        }
        total_ += e.bytes;
        entries_.emplace(key, std::move(e));
        trim();
        it = entries_.find(key);
    }
    it->second.used = ++clock_;
    return it->second.image;
}

void GpuImages::forget(const fs::path& png) {
    const auto it = entries_.find(u8(png));
    if (it == entries_.end())
        return;
    if (it->second.image.texture != nullptr)
        released_.push_back(it->second.image.texture);
    total_ -= it->second.bytes;
    entries_.erase(it);
}

void GpuImages::trim() {
    // Drop the least recently shown until within the budget; the newest
    // (used == 0, just added) stays whatever its size.
    while (total_ > budget_ && entries_.size() > 1) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.used == 0)
                continue;
            if (oldest == entries_.end() || it->second.used < oldest->second.used)
                oldest = it;
        }
        if (oldest == entries_.end())
            break;
        if (oldest->second.image.texture != nullptr)
            released_.push_back(oldest->second.image.texture);
        total_ -= oldest->second.bytes;
        entries_.erase(oldest);
    }
}

}  // namespace studio
