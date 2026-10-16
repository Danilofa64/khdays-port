// What the port accepts per kind: PNGs and WAVs made
// here, checked by the port's own readers through core/check.
#include "test.h"

#include "core/mods/check.h"
#include "core/files/paths.h"

extern "C" {
#include "render/png_io.h"
#include "replace.h"
}

#include <cstdint>
#include <vector>

using namespace studio;

namespace {

fs::path rgba_png(const fs::path& dir, const char* name, int w, int h) {
    const std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 200);
    const fs::path file = dir / name;
    CHECK(khdays_png_write(u8(file).c_str(), pixels.data(), w, h) == nullptr);
    return file;
}

fs::path indexed_png(const fs::path& dir, const char* name, int w, int h) {
    const std::vector<std::uint8_t> indices(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 1);
    const std::uint8_t palette[6] = {0, 0, 0, 255, 0, 0};
    const fs::path file = dir / name;
    CHECK(khdays_png_write_indexed(u8(file).c_str(), indices.data(), w, h, palette, 2, 1) == nullptr);
    return file;
}

bool has(const CheckResult& r, Note n) {
    for (const auto& f : r.findings) {
        if (f.note == n)
            return true;
    }
    return false;
}

}  // namespace

TEST(textures_as_the_port_takes_them) {
    const fs::path dir = studio_test::scratch("textures");
    CHECK(check_texture(rgba_png(dir, "same.png", 16, 8), 16, 8).verdict == Verdict::accepted);
    const CheckResult twice = check_texture(rgba_png(dir, "twice.png", 32, 16), 16, 8);
    CHECK(twice.verdict == Verdict::accepted && has(twice, Note::texture_scale));
    CHECK(check_texture(rgba_png(dir, "wide.png", 32, 8), 16, 8).verdict == Verdict::warned);
    CHECK(check_texture(rgba_png(dir, "odd.png", 17, 8), 16, 8).verdict == Verdict::warned);
    studio_test::write(dir / "fake.png", "not a picture");
    CHECK(check_texture(dir / "fake.png", 16, 8).verdict == Verdict::refused);
}

TEST(sheets_as_the_port_takes_them) {
    const fs::path dir = studio_test::scratch("sheets");
    const CheckResult same = check_sheet(indexed_png(dir, "same.png", 16, 8), 16, 8);
    CHECK(same.verdict == Verdict::accepted && has(same, Note::sheet_indexed));
    const CheckResult hd = check_sheet(rgba_png(dir, "hd.png", 32, 16), 16, 8);
    CHECK(hd.verdict == Verdict::accepted && has(hd, Note::sheet_true_colour));
    CHECK(check_sheet(rgba_png(dir, "taller.png", 48, 30), 16, 8).verdict == Verdict::accepted);  // height may be more
    CHECK(check_sheet(rgba_png(dir, "short.png", 32, 15), 16, 8).verdict == Verdict::refused);
    CHECK(check_sheet(rgba_png(dir, "width.png", 33, 16), 16, 8).verdict == Verdict::refused);
    CHECK(check_sheet(rgba_png(dir, "small.png", 8, 8), 16, 8).verdict == Verdict::refused);
}

TEST(sounds_as_the_port_takes_them) {
    const fs::path dir = studio_test::scratch("sounds");
    std::vector<std::int16_t> samples(2 * 4800);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<std::int16_t>((i * 37) % 2000 - 1000);
    const fs::path looped = dir / "looped.wav";
    CHECK(khdays_replacement_write_wav(u8(looped).c_str(), 48000, 2, samples.data(), 4800, 1, 100, 4000) != 0);
    const CheckResult r = check_sound(looped);
    CHECK(r.verdict == Verdict::accepted && has(r, Note::sound_looped));
    bool loop_args = false;
    for (const auto& f : r.findings) {
        if (f.note == Note::sound_looped)
            loop_args = f.args.size() == 2 && f.args[0] == "100" && f.args[1] == "4000";
    }
    CHECK(loop_args);

    const fs::path once = dir / "once.wav";
    CHECK(khdays_replacement_write_wav(u8(once).c_str(), 32000, 1, samples.data(), 4800, 0, 0, 0) != 0);
    CHECK(has(check_sound(once), Note::sound_plays_once));

    studio_test::write(dir / "text.ogg", "not a sound");
    CHECK(check_sound(dir / "text.ogg").verdict == Verdict::refused);
    studio_test::write(dir / "short.wav", "RIFF");
    CHECK(check_sound(dir / "short.wav").verdict == Verdict::refused);
}

TEST(other_images_become_png) {
    const fs::path dir = studio_test::scratch("convert");
    // a 2x1 24-bit BMP: red, blue
    std::string bmp = "BM";
    auto le32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            bmp += static_cast<char>((v >> (8 * i)) & 0xff);
    };
    auto le16 = [&](std::uint16_t v) {
        bmp += static_cast<char>(v & 0xff);
        bmp += static_cast<char>(v >> 8);
    };
    le32(14 + 40 + 8);
    le32(0);
    le32(14 + 40);
    le32(40);
    le32(2);
    le32(1);
    le16(1);
    le16(24);
    le32(0);
    le32(8);
    le32(2835);
    le32(2835);
    le32(0);
    le32(0);
    bmp += std::string("\x00\x00\xff\xff\x00\x00\x00\x00", 8);  // BGR red, BGR blue, row padding
    studio_test::write(dir / "two.bmp", bmp);
    CHECK(!is_png(dir / "two.bmp"));
    CHECK(convert_to_png(dir / "two.bmp", dir / "two.png").empty());
    CHECK(is_png(dir / "two.png"));
    int w = 0, h = 0;
    std::string why;
    CHECK(png_size(dir / "two.png", &w, &h, &why) && w == 2 && h == 1);
    CHECK(!convert_to_png(dir / "missing.bmp", dir / "x.png").empty());
}
