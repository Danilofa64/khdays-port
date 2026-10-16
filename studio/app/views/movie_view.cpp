// A movie of the game seen and heard as each video it can be (the game's, the player's HD video, each
// mod's), and a video saved into the mod in its place: movies/<id>.mpg (in the game's movie) or
// movies/hd<id>.mp4 (with the movies in HD), one per movie, played through the port's own player.
#include "../app.h"

#include "core/mods/mods.h"
#include "core/files/paths.h"

#include "../../../port/host/movies/movie_hd.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace studio {

namespace {

// The video playing in the view (one at a time, as the port's player).
struct Player {
    int item = -1;
    fs::path file;
    bool open = false, paused = false, ended = false;
    KhdaysHdInfo info{};
    std::vector<std::uint8_t> rgba;  // the last picture, made smaller when large
    int width = 0, height = 0;
    std::uint64_t version = 0;
    Uint64 started = 0, paused_at = 0, paused_for = 0;  // milliseconds: the time shown
};

Player player;

std::string lower_ext(const fs::path& file) {
    std::string ext = u8(file.extension());
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// An MPEG stream starts with a pack (00 00 01 BA) or a sequence header (00 00 01 B3).
bool looks_mpeg(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char b[4] = {};
    in.read(reinterpret_cast<char*>(b), 4);
    return in && b[0] == 0 && b[1] == 0 && b[2] == 1 && (b[3] == 0xba || b[3] == 0xb3);
}

std::uint8_t clamp8(int v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

// The decoder's NV12 picture as RGBA (BT.601, limited range: the port's SDL
// texture's default), every `step`-th pixel when the picture is large.
void take_picture(const KhdaysHdPicture& p) {
    const int step = std::max(1, (p.width + 959) / 960);
    player.width = p.width / step;
    player.height = p.height / step;
    player.rgba.resize(static_cast<std::size_t>(player.width) * static_cast<std::size_t>(player.height) * 4u);
    for (int y = 0; y < player.height; ++y) {
        const int sy = y * step;
        const std::uint8_t* luma = p.luma + static_cast<std::ptrdiff_t>(sy) * p.pitch;
        const std::uint8_t* chroma = p.chroma + static_cast<std::ptrdiff_t>(sy / 2) * p.pitch;
        std::uint8_t* out = &player.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(player.width) * 4u];
        for (int x = 0; x < player.width; ++x) {
            const int sx = x * step;
            const int c = 298 * (luma[sx] - 16);
            const int u = chroma[(sx & ~1)] - 128, v = chroma[(sx & ~1) + 1] - 128;
            out[x * 4 + 0] = clamp8((c + 409 * v + 128) >> 8);
            out[x * 4 + 1] = clamp8((c - 100 * u - 208 * v + 128) >> 8);
            out[x * 4 + 2] = clamp8((c + 516 * u + 128) >> 8);
            out[x * 4 + 3] = 255;
        }
    }
    ++player.version;
}

void stop() {
    if (player.open)
        khdays_hd_close();
    player.open = false;
    player.paused = false;
}

void play(App& app, int item, const fs::path& file) {
    stop();
    player = Player{};
    player.item = item;
    player.file = file;
    char why[256] = "";
    if (!khdays_hd_open(u8(file).c_str(), &player.info, why, sizeof(why))) {
        app.tell(fmt(app.language, S::movie_cannot_play, {u8(file.filename()), why}), false);
        return;
    }
    khdays_hd_set_gain(1.0f);
    khdays_hd_start();
    player.open = true;
    player.started = SDL_GetTicks();
}

// What the player gives this frame: a newer picture, its end, a failure.
void poll(App& app) {
    if (!player.open)
        return;
    KhdaysHdPicture p;
    if (khdays_hd_picture_begin(&p)) {
        take_picture(p);
        khdays_hd_picture_done();
    }
    char why[256] = "";
    const int status = khdays_hd_status(why, sizeof(why));
    if (status == 1) {
        player.ended = true;
        stop();
    } else if (status == -1) {
        app.tell(fmt(app.language, S::movie_cannot_play, {u8(player.file.filename()), why}), false);
        stop();
    }
}

std::string clock_text(Uint64 ms) {
    const Uint64 s = ms / 1000;
    char text[32];
    std::snprintf(text, sizeof(text), "%llu:%02llu", static_cast<unsigned long long>(s / 60),
                  static_cast<unsigned long long>(s % 60));
    return text;
}

}  // namespace

void movie_view_leave() {
    stop();
    player.item = -1;
}

void movie_offer(App& app, const Item& item, const fs::path& file) {
    const Mod* m = app.current_mod();
    if (m == nullptr) {
        app.tell(tr(app.language, S::choose_mod_first), false);
        return;
    }
    const std::string ext = lower_ext(file);
    const bool hd = ext == ".mp4";
    if (!hd && ext != ".mpg" && ext != ".mpeg") {
        app.tell(fmt(app.language, S::movie_not_video, {u8(file.filename())}), false);
        return;
    }
    if (!hd && !looks_mpeg(file)) {
        app.tell(fmt(app.language, S::movie_not_mpeg, {u8(file.filename())}), false);
        return;
    }
    // the mod's file may be the one playing
    if (player.open && player.item == app.selected)
        stop();
    // one video per movie in a mod: the other kind goes (the .mpg would win over an .mp4)
    std::string why;
    if (!remove_replacement(m->dir, item, &why)) {
        app.tell(fmt(app.language, S::save_failed, {why}), false);
        return;
    }
    const fs::path dest = m->dir / path_u8(replacement_path(item, hd ? ".mp4" : ".mpg", false));
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::copy_file(file, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        app.tell(fmt(app.language, S::save_failed, {u8(dest) + ": " + ec.message()}), false);
        return;
    }
    app.tell(
        fmt(app.language, hd ? S::movie_saved_hd : S::movie_saved, {u8(dest.lexically_relative(app.settings.root))}) +
            "  " + tr(app.language, S::restart_note),
        true);
}

void draw_movie_view(App& app, const Item& item) {
    const auto lang = app.language;
    if (player.item != app.selected)
        movie_view_leave();
    std::error_code ec;
    const auto size = fs::file_size(item.original, ec);
    ImGui::TextUnformatted(fmt(lang, S::movie_header, {item.key, std::to_string((ec ? 0 : size) / 1024)}).c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr(lang, S::movie_hint));
    ImGui::PopTextWrapPos();

    // what plays now: the first mod's video, the player's own HD one, or the game's movie
    const auto held = holders(app.mods_root, item);
    const fs::path own = app.settings.root / "data" / "cutscenes" / "cinematics" / path_u8("hd" + item.key + ".mp4");
    const fs::path game = app.settings.root / "data" / "preview" / "video" / "mobiclip" / path_u8(item.key + ".mp4");
    const bool have_own = fs::is_regular_file(own, ec), have_game = fs::is_regular_file(game, ec);
    if (!held.empty()) {
        const bool hd = lower_ext(held.front().file) == ".mp4";
        ImGui::TextUnformatted(fmt(lang, hd ? S::movie_from_mod_hd : S::movie_from_mod, {held.front().mod}).c_str());
    } else if (have_own) {
        ImGui::TextUnformatted(
            fmt(lang, S::movie_from_player, {u8(own.lexically_relative(app.settings.root))}).c_str());
    } else {
        ImGui::TextUnformatted(tr(lang, S::movie_from_game));
    }

    // the videos it can be, each to watch here
    ImGui::SeparatorText(tr(lang, S::movie_watch));
    struct Source {
        std::string label;
        fs::path file;
    };
    std::vector<Source> sources;
    if (have_game)
        sources.push_back({tr(lang, S::movie_source_game), game});
    if (have_own)
        sources.push_back({fmt(lang, S::movie_source_player, {u8(own.lexically_relative(app.settings.root))}), own});
    for (const auto& h : held)
        sources.push_back({fmt(lang, S::movie_source_mod, {h.mod, u8(h.file.filename())}), h.file});
    if (!have_game) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s",
                            fmt(lang, S::movie_no_preview, {u8(game.lexically_relative(app.settings.root))}).c_str());
        ImGui::PopTextWrapPos();
    }
    for (std::size_t i = 0; i < sources.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool this_one =
            player.item == app.selected && player.file == sources[i].file && (player.open || player.ended);
        if (ImGui::SmallButton(this_one && player.open ? tr(lang, S::stop) : tr(lang, S::movie_play))) {
            if (this_one && player.open) {
                stop();
            } else {
                play(app, app.selected, sources[i].file);
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(sources[i].label.c_str());
        ImGui::PopID();
    }

    // the picture, below the controls
    poll(app);
    if (player.item == app.selected && (player.open || player.ended) && player.width > 0) {
        if (player.open) {
            if (ImGui::Button(player.paused ? tr(lang, S::movie_resume) : tr(lang, S::movie_pause))) {
                player.paused = !player.paused;
                khdays_hd_pause(player.paused ? 1 : 0);
                if (player.paused) {
                    player.paused_at = SDL_GetTicks();
                } else {
                    player.paused_for += SDL_GetTicks() - player.paused_at;
                }
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(tr(lang, S::movie_again)))
            play(app, app.selected, player.file);
        ImGui::SameLine();
        const Uint64 now = player.paused ? player.paused_at : SDL_GetTicks();
        ImGui::Text("%s  %s  %dx%d%s", clock_text(player.open ? now - player.started - player.paused_for : 0).c_str(),
                    u8(player.file.filename()).c_str(), player.info.width, player.info.height,
                    player.ended ? (std::string("  ") + tr(lang, S::movie_ended)).c_str() : "");
        const GpuImage& image = app.images->pixels("movie", player.version, player.rgba, player.width, player.height);
        if (image.texture != nullptr) {
            // as large as the panel allows: its width, half the window's height (a DS movie
            // of 256x160 enlarged like an HD one made smaller)
            const float aspect =
                static_cast<float>(player.info.height) / static_cast<float>(std::max(1, player.info.width));
            const float high = std::max(160.0f, ImGui::GetWindowHeight() * 0.5f);
            const float w = std::min(ImGui::GetContentRegionAvail().x, high / aspect);
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(image.texture))),
                         ImVec2(w, w * aspect));
        }
    }

    // a video into the mod
    ImGui::SeparatorText(tr(lang, S::movie_replace));
    const Mod* m = app.current_mod();
    if (m == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::choose_mod_first));
        return;
    }
    if (ImGui::Button(tr(lang, S::movie_choose))) {
        static const SDL_DialogFileFilter videos[] = {{"MP4, MPEG-1", "mp4;mpg;mpeg"}};
        SDL_ShowOpenFileDialog(
            [](void* user, const char* const* files, int) {
                if (files == nullptr || files[0] == nullptr)
                    return;
                App* a = static_cast<App*>(user);
                std::lock_guard lock(a->dialog_mutex);
                a->dialog_files.emplace_back(files[0]);
            },
            &app, app.window, videos, 1, nullptr, false);
    }
    bool mine = false;
    for (const auto& h : held)
        mine = mine || h.mod == m->name;
    if (mine) {
        same_line_if_room(button_width(tr(lang, S::text_give_back)));
        if (ImGui::Button(tr(lang, S::text_give_back))) {
            if (player.open && player.item == app.selected)
                stop();
            std::string why;
            if (remove_replacement(m->dir, item, &why)) {
                app.tell(fmt(lang, S::removed, {item.key, m->name}), true);
            } else {
                app.tell(fmt(lang, S::save_failed, {why}), false);
            }
        }
    }
}

}  // namespace studio
