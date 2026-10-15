// What the port accepts for each kind of replacement,
// checked with the port's own readers where they
// decide: png_io.c for PNG, replace.c for sounds. A file the tool accepts is
// a file the port uses.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

enum class Verdict { accepted, warned, refused };

// What a check found; the interface words each in its language.
enum class Note {
    not_image,             // {0}: the reader's reason
    not_png,               // {0}: the reader's reason
    converted_to_png,      // {0}: the source format's extension
    texture_stretched,     // {0}x{1} is not a whole multiple of the original's {2}x{3}
    texture_scale,         // {0}x{1}: {2} times the original
    sheet_size,            // {0}x{1}: wants {2}x{3} times a whole number from 1 to 255, height at least that
    sheet_scale,           // {0} times the sheet
    sheet_indexed,         // drawn with the game's palettes
    sheet_true_colour,     // drawn with its own colours
    sound_unreadable,      // {0}: the port's reason
    sound_decoded,         // {0} Hz, {1} channels, {2} frames
    sound_looped,          // loop from {0} to {1}
    sound_plays_once,
};

struct Finding {
    Note note;
    std::vector<std::string> args;
};

struct CheckResult {
    Verdict verdict = Verdict::accepted;
    std::vector<Finding> findings;

    void info(Note n, std::vector<std::string> a = {}) { findings.push_back({n, std::move(a)}); }
    void warn(Note n, std::vector<std::string> a = {}) {
        if (verdict == Verdict::accepted) verdict = Verdict::warned;
        findings.push_back({n, std::move(a)});
    }
    void refuse(Note n, std::vector<std::string> a = {}) {
        verdict = Verdict::refused;
        findings.push_back({n, std::move(a)});
    }
};

// A PNG's size as the port reads it; false when it cannot be read.
bool png_size(const std::filesystem::path& png, int* width, int* height, std::string* why);

// A 3D texture replacing one of `original_w` x `original_h` (the port:
// native/runtime/mods/textures.c lines 235-245 -- any PNG; not the same whole
// multiple on both sides: stretched, with a warning).
CheckResult check_texture(const std::filesystem::path& png, int original_w, int original_h);

// A 2D tile sheet replacing one of `sheet_w` x `sheet_h` (the port:
// native/runtime/mods/tiles2d.c lines 89-107 -- an indexed PNG is the game's
// palette indices, any other is true colour; width = s x the sheet's for a
// whole s in 1..255 and height at least s x its height, else not drawn).
CheckResult check_sheet(const std::filesystem::path& png, int sheet_w, int sheet_h);

// A sound (the port: native/sound/replace.c khdays_replacement_load).
CheckResult check_sound(const std::filesystem::path& file);

// Any image the port's model textures may be (src/assets/image.cpp: PNG,
// JPEG, TGA, BMP) written as PNG; empty, or the reason.
std::string convert_to_png(const std::filesystem::path& from, const std::filesystem::path& to);

// Whether a file is a PNG by its first bytes.
bool is_png(const std::filesystem::path& file);

}  // namespace studio
