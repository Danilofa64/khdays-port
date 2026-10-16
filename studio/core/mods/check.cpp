#include "core/mods/check.h"

#include "core/files/paths.h"

#include "khdays/assets/image.h"

extern "C" {
#include "render/png_io.h"
#include "replace.h"
}

#include <cstring>
#include <fstream>

namespace studio {

namespace {

std::string str(long long v) {
    return std::to_string(v);
}

}  // namespace

bool is_png(const fs::path& file) {
    static const unsigned char magic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::ifstream in(file, std::ios::binary);
    unsigned char head[8] = {};
    in.read(reinterpret_cast<char*>(head), 8);
    return in.gcount() == 8 && std::memcmp(head, magic, 8) == 0;
}

bool png_size(const fs::path& png, int* width, int* height, std::string* why) {
    const char* error = nullptr;
    uint8_t* pixels = khdays_png_read(u8(png).c_str(), width, height, &error);
    if (pixels == nullptr) {
        if (why != nullptr)
            *why = error != nullptr ? error : "unreadable";
        return false;
    }
    khdays_png_free(pixels);
    return true;
}

CheckResult check_texture(const fs::path& png, int ow, int oh) {
    CheckResult r;
    int w = 0, h = 0;
    std::string why;
    if (!is_png(png) || !png_size(png, &w, &h, &why)) {
        r.refuse(Note::not_png, {why.empty() ? "not a PNG" : why});
        return r;
    }
    // textures.c: width % ow, height % oh, and the same multiple on both sides
    if (ow <= 0 || oh <= 0 || w % ow != 0 || h % oh != 0 || w / ow != h / oh) {
        r.warn(Note::texture_stretched, {str(w), str(h), str(ow), str(oh)});
    } else {
        r.info(Note::texture_scale, {str(w), str(h), str(w / ow)});
    }
    return r;
}

CheckResult check_sheet(const fs::path& png, int sw, int sh) {
    CheckResult r;
    if (!is_png(png)) {
        r.refuse(Note::not_png, {"not a PNG"});
        return r;
    }
    int w = 0, h = 0;
    const char* error = nullptr;
    bool indexed = true;
    uint8_t* pixels = khdays_png_read_indexed(u8(png).c_str(), &w, &h, nullptr, &error);
    if (pixels == nullptr && error != nullptr && std::strcmp(error, "not indexed") == 0) {
        indexed = false;
        error = nullptr;
        pixels = khdays_png_read(u8(png).c_str(), &w, &h, &error);
    }
    if (pixels == nullptr) {
        r.refuse(Note::not_png, {error != nullptr ? error : "unreadable"});
        return r;
    }
    khdays_png_free(pixels);
    // tiles2d.c: scale = width / (columns * 8); 1..255, exact width, height at least scale x rows x 8
    const int scale = sw > 0 ? w / sw : 0;
    if (scale < 1 || scale > 255 || w != scale * sw || h < scale * sh) {
        r.refuse(Note::sheet_size, {str(w), str(h), str(sw), str(sh)});
        return r;
    }
    r.info(Note::sheet_scale, {str(scale)});
    r.info(indexed ? Note::sheet_indexed : Note::sheet_true_colour);
    return r;
}

CheckResult check_sound(const fs::path& file) {
    CheckResult r;
    KhdaysReplacement sound{};
    char why[256] = {};
    if (!khdays_replacement_load(&sound, u8(file).c_str(), nullptr, why, static_cast<int>(sizeof(why)))) {
        r.refuse(Note::sound_unreadable, {why[0] != '\0' ? why : "unreadable"});
        return r;
    }
    r.info(Note::sound_decoded, {str(sound.rate), str(sound.channels), str(static_cast<long long>(sound.frames))});
    if (sound.looped) {
        r.info(Note::sound_looped,
               {str(static_cast<long long>(sound.loop_start)), str(static_cast<long long>(sound.loop_end))});
    } else {
        r.info(Note::sound_plays_once);
    }
    khdays_replacement_free(&sound);
    return r;
}

std::string convert_to_png(const fs::path& from, const fs::path& to) {
    try {
        const auto image = khdays::assets::read_image(from);
        const char* error = khdays_png_write(u8(to).c_str(), image.rgba.data(), static_cast<int>(image.width),
                                             static_cast<int>(image.height));
        return error != nullptr ? std::string(error) : std::string();
    } catch (const std::exception& e) {
        return e.what();
    }
}

}  // namespace studio
