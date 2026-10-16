// A text file of the game (a UI string table or a message database), each
// string shown beside the one the mod gives, edited in place and saved whole
// into the mod (core/texts/texts.h): what a modder translating or rewording the
// game works in. The characters the game's font cannot draw are said.
#include "../app.h"

#include "core/mods/mods.h"
#include "core/files/paths.h"
#include "core/texts/texts.h"

#include "khdays/assets/font.h"
#include "khdays/assets/message.h"
#include "khdays/vfs/filesystem.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>

namespace studio {

namespace {

namespace assets = khdays::assets;

// What the person has edited in a file, kept while they look at other items
// (nothing they typed is lost by choosing something else).
struct Edits {
    std::vector<std::vector<std::string>> lists;  // per list, per string: the text as shown (escapes)
};

struct TextView {
    int item = -1;
    std::string mod;
    std::string error;
    TextFile game, start;  // the game's, and what the mod holds (or the game's) when opened
    int list = 0;
    std::string filter;
};

TextView v;
std::map<std::string, Edits> pending;  // by the item's key

std::vector<std::uint8_t> read_bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The font the game draws these texts with (the European one; the Japanese one for ja).
const assets::Font* font_for(const Item& item) {
    static std::map<std::string, assets::Font> fonts;
    static std::map<std::string, bool> tried;
    const std::string name = item.language == "ja" ? "text/font_jp_10.nftr" : "text/font_eu_10.nftr";
    if (!tried[name]) {
        tried[name] = true;
        try {
            fonts[name] = assets::decode_nftr(khdays::vfs::data_root() / "nitrofs" / path_u8(name));
        } catch (const std::exception&) {
        }
    }
    const auto found = fonts.find(name);
    return found != fonts.end() ? &found->second : nullptr;
}

void open(App& app, const Item& item) {
    v = TextView{};
    v.item = app.selected;
    v.mod = app.current_mod() != nullptr ? app.current_mod()->name : std::string();
    const bool database = is_text_database(item.key);
    std::string why;
    if (!parse_text_file(read_bytes(item.original), database, &v.game, &why)) {
        v.error = why;
        return;
    }
    v.start = v.game;
    // the mod's own copy, when it has one: what is edited from
    if (const Mod* m = app.current_mod()) {
        const fs::path mine = m->dir / path_u8(replacement_path(item, "", false));
        std::error_code ec;
        TextFile held;
        if (fs::is_regular_file(mine, ec) && parse_text_file(read_bytes(mine), database, &held, &why) &&
            held.lists.size() == v.game.lists.size()) {
            v.start = held;
        }
    }
    Edits& e = pending[item.key];
    if (e.lists.empty()) {
        for (const auto& list : v.start.lists) {
            std::vector<std::string> shown;
            for (const auto& s : list)
                shown.push_back(assets::message_to_utf8(s));
            e.lists.push_back(std::move(shown));
        }
    }
}

std::string lower_text(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

void draw_text_view(App& app, const Item& item) {
    const std::string mod_name = app.current_mod() != nullptr ? app.current_mod()->name : std::string();
    if (v.item != app.selected || v.mod != mod_name)
        open(app, item);
    const auto lang = app.language;
    if (!v.error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", v.error.c_str());
        return;
    }
    Edits& e = pending[item.key];
    std::size_t texts = 0, changed = 0;
    for (std::size_t l = 0; l < e.lists.size(); ++l) {
        texts += e.lists[l].size();
        for (std::size_t k = 0; k < e.lists[l].size(); ++k)
            changed += e.lists[l][k] != assets::message_to_utf8(v.start.lists[l][k]) ? 1 : 0;
    }
    ImGui::TextUnformatted(
        fmt(lang, S::text_header, {item.key, std::to_string(texts), std::to_string(e.lists.size())}).c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr(lang, S::text_hint));
    ImGui::PopTextWrapPos();

    // saving: the whole file into the mod, or the mod's copy taken away
    const Mod* m = app.current_mod();
    if (m == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::choose_mod_first));
    } else {
        ImGui::BeginDisabled(changed == 0);
        if (ImGui::Button(fmt(lang, S::text_save, {std::to_string(changed)}).c_str())) {
            TextFile out = v.start;
            for (std::size_t l = 0; l < out.lists.size() && l < e.lists.size(); ++l) {
                for (std::size_t k = 0; k < out.lists[l].size() && k < e.lists[l].size(); ++k) {
                    if (e.lists[l][k] != assets::message_to_utf8(out.lists[l][k]))
                        out.lists[l][k] = assets::message_from_utf8(e.lists[l][k]);
                }
            }
            const fs::path dest = m->dir / path_u8(replacement_path(item, "", false));
            const std::vector<std::uint8_t> bytes = write_text_file(out);
            TextFile check;
            std::string why;
            std::error_code ec;
            fs::create_directories(dest.parent_path(), ec);
            {
                std::ofstream f(dest, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!f)
                    why = "cannot write " + u8(dest);
            }
            if (why.empty() &&
                (!parse_text_file(read_bytes(dest), out.database, &check, &why) || check.lists != out.lists)) {
                if (why.empty())
                    why = "it does not read back as written";
            }
            if (why.empty()) {
                app.tell(fmt(lang, S::text_saved,
                             {u8(dest.lexically_relative(app.settings.root)), std::to_string(changed)}) +
                             "  " + tr(lang, S::restart_note),
                         true);
                v.start = out;  // what the mod holds now
            } else {
                app.tell(fmt(lang, S::save_failed, {why}), false);
            }
        }
        ImGui::EndDisabled();
        std::error_code ec;
        const fs::path mine = m->dir / path_u8(replacement_path(item, "", false));
        if (fs::is_regular_file(mine, ec)) {
            same_line_if_room(button_width(tr(lang, S::text_give_back)));
            if (ImGui::Button(tr(lang, S::text_give_back))) {
                std::string why;
                if (remove_replacement(m->dir, item, &why)) {
                    pending.erase(item.key);
                    app.tell(fmt(lang, S::removed, {item.key, m->name}), true);
                    v.item = -1;  // opened again from the game's
                    return;
                }
                app.tell(fmt(lang, S::save_failed, {why}), false);
            }
        }
    }

    // a database's lists
    if (e.lists.size() > 1) {
        ImGui::SetNextItemWidth(16 * ImGui::GetFontSize());
        const std::string current =
            fmt(lang, S::text_list,
                {std::to_string(v.list), std::to_string(e.lists[static_cast<std::size_t>(v.list)].size())});
        if (ImGui::BeginCombo("##list", current.c_str())) {
            for (std::size_t l = 0; l < e.lists.size(); ++l) {
                if (ImGui::Selectable(
                        fmt(lang, S::text_list, {std::to_string(l), std::to_string(e.lists[l].size())}).c_str(),
                        static_cast<int>(l) == v.list)) {
                    v.list = static_cast<int>(l);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", tr(lang, S::text_filter), &v.filter);

    if (v.list < 0 || static_cast<std::size_t>(v.list) >= e.lists.size())
        v.list = 0;
    if (e.lists.empty())
        return;
    auto& shown = e.lists[static_cast<std::size_t>(v.list)];
    const auto& game = v.game.lists[static_cast<std::size_t>(v.list)];
    std::vector<int> rows;
    const std::string want = lower_text(v.filter);
    for (std::size_t k = 0; k < shown.size(); ++k) {
        if (want.empty() || lower_text(shown[k]).find(want) != std::string::npos ||
            lower_text(assets::message_to_utf8(game[k])).find(want) != std::string::npos) {
            rows.push_back(static_cast<int>(k));
        }
    }
    const assets::Font* font = font_for(item);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("##texts", 3, flags))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(tr(lang, S::text_games), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(tr(lang, S::text_yours), ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const std::size_t k = static_cast<std::size_t>(rows[static_cast<std::size_t>(r)]);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%zu", k);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(assets::message_to_utf8(game[k]).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(static_cast<int>(k));
            const bool edited = shown[k] != assets::message_to_utf8(v.start.lists[static_cast<std::size_t>(v.list)][k]);
            if (edited)
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.32f, 0.18f, 1));
            const int lines = 1 + static_cast<int>(std::count(shown[k].begin(), shown[k].end(), '\n'));
            ImGui::InputTextMultiline("##t", &shown[k],
                                      ImVec2(-1, static_cast<float>(lines) * ImGui::GetTextLineHeight() +
                                                     2 * ImGui::GetStyle().FramePadding.y));
            if (edited)
                ImGui::PopStyleColor();
            // the characters the game cannot draw
            if (font != nullptr) {
                const std::u16string missing = missing_glyphs(assets::message_from_utf8(shown[k]),
                                                              [&](char16_t c) { return font->glyph_of(c) >= 0; });
                if (!missing.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1), "%s",
                                       fmt(lang, S::text_no_glyph, {assets::message_to_utf8(missing)}).c_str());
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

}  // namespace studio
