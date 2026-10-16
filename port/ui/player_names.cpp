// The players' names on the field's HUD in multiplayer (player_names.h). The game knows every name
// only while the group forms, in the selection message of the Mission Mode context (ov006's at
// 0x020565e4, ov008's at 0x02090f24; +0x42c: the members present at +8, their names at +0xa, 11 UTF-16
// characters each by player number); the port keeps them for the mission. In the field each name goes
// left of its player's J label (the own label at 199,175; the allies' rows at y 120, 96 and 72 from x
// 216), in the game's 8-dot font, white with a black edge, in the party's layer.
#include "player_names.h"

extern "C" {
#include "elements.h"
#include "parts.h"
#include "../runtime/core/window.h"
}

#include "khdays/assets/font.h"
#include "khdays/vfs/filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

// the port's and the game's
extern "C" int khdays_overlay_loaded(int id);
extern "C" int Session_IsActive(void);
extern "C" int Session_GetSetup(void);
extern "C" unsigned int Session_GetLocalPlayerIndex(void);

namespace {

constexpr int kPlayers = 4;
constexpr int kNameChars = 11;          // with the terminator
constexpr std::uint32_t kSelect = 0x020565e4u;  // ov006's mission globals: the context first
constexpr std::uint32_t kLobby = 0x02090f24u;   // ov008's
constexpr std::uint32_t kMessage = 0x42c;       // the selection message in the context
constexpr int kOwnRight = 197, kOwnTop = 175;   // the own name's right edge and top (DS dots): its label's top
constexpr int kAllyRight = 214, kAllyTop = 120, kAllyStep = 24;  // the allies' (their labels at 216,120 measured)
constexpr int kLabelHeight = 7;  // a label's plate: the names centred on it
constexpr int kSizeQuarters = 3; // the names at three quarters of the font's own size

std::u16string names[kPlayers];

bool in_game_ram(std::uint32_t p) {
    return khdays_game_ram(p) != 0;  // main RAM or the game's heaps
}

template <typename T>
T at(std::uint32_t address) {
    return *static_cast<const volatile T*>(KHDAYS_W(address));
}

// The names of the players present, while ov006 or ov008 has its context.
void keep_names() {
    std::uint32_t context = 0;
    if (khdays_overlay_loaded(6)) {
        context = at<std::uint32_t>(kSelect);
    } else if (khdays_overlay_loaded(8)) {
        context = at<std::uint32_t>(kLobby);
    }
    if (!in_game_ram(context)) {
        return;
    }
    const std::uint16_t present = at<std::uint16_t>(context + kMessage + 8);
    for (int i = 0; i < kPlayers; ++i) {
        if (!(present & (1u << i))) {
            continue;
        }
        std::u16string name;
        for (int c = 0; c < kNameChars - 1; ++c) {
            const char16_t ch = at<std::uint16_t>(context + kMessage + 0xa + static_cast<std::uint32_t>(i * kNameChars + c) * 2);
            if (ch == 0) {
                break;
            }
            name.push_back(ch);
        }
        if (!name.empty()) {
            names[i] = name;
        }
    }
}

std::optional<khdays::assets::Font> font;
bool font_tried;

const khdays::assets::Font* the_font() {
    if (!font_tried) {
        font_tried = true;
        const auto path = khdays::vfs::resolve("text/font_eu_08.nftr");
        try {
            if (path) {
                font = khdays::assets::decode_nftr(*path);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ui: the players' names' font could not be read (%s)\n", e.what());
        }
        if (!font) {
            std::fprintf(stderr, "ui: the players' names are not shown: no text/font_eu_08.nftr\n");
        }
    }
    return font ? &*font : nullptr;
}

// A name drawn: its image and what it was drawn from.
struct Drawn {
    std::u16string name;
    int scale = 0;
    int image = -1;
    int w = 0, h = 0;  // the font's dots
    int dot = 0;       // texels per font dot
};
Drawn drawn[kPlayers];

// The name in white over a black edge one dot around it, its texels at the
// internal resolution, the glyphs' coverage taken as shown from half up: a
// font dot is three quarters of a DS dot's texels, whole texels so every dot
// is as wide as the next.
bool draw(Drawn& d, const std::u16string& name, int scale) {
    if (d.image >= 0 && d.name == name && d.scale == scale) {
        return true;
    }
    const auto* f = the_font();
    if (f == nullptr) {
        return false;
    }
    const auto text = khdays::assets::render_text(*f, name);
    const int w = text.width + 2, h = text.height + 2;
    std::vector<std::uint32_t> dots(static_cast<std::size_t>(w * h), 0u);
    auto inked = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < text.width && y < text.height &&
               text.rgba[(static_cast<std::size_t>(y) * text.width + x) * 4 + 3] >= 128;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy) {
                for (int dx = -1; dx <= 1 && !edge; ++dx) {
                    edge = inked(x - 1 + dx, y - 1 + dy);
                }
            }
            if (inked(x - 1, y - 1)) {
                dots[static_cast<std::size_t>(y * w + x)] = 0xffffffffu;
            } else if (edge) {
                dots[static_cast<std::size_t>(y * w + x)] = 0xff000000u;
            }
        }
    }
    const int dot = (scale * kSizeQuarters + 2) / 4 > 0 ? (scale * kSizeQuarters + 2) / 4 : 1;
    khdays_ui_image_put(d.image);
    d.image = khdays_ui_image_get(w * dot, h * dot);
    if (d.image < 0) {
        return false;
    }
    std::uint32_t* texels = khdays_ui_image_begin(d.image);
    if (texels == nullptr) {
        return false;
    }
    for (int y = 0; y < h * dot; ++y) {
        for (int x = 0; x < w * dot; ++x) {
            texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w * dot) + static_cast<std::size_t>(x)] =
                dots[static_cast<std::size_t>((y / dot) * w + x / dot)];
        }
    }
    khdays_ui_image_end(d.image);
    d.name = name;
    d.scale = scale;
    d.w = w;
    d.h = h;
    d.dot = dot;
    return true;
}

void emit_name(int player, int right, int top) {
    const int scale = khdays_ui_scale();
    Drawn& d = drawn[player];
    if (names[player].empty() || !draw(d, names[player], scale)) {
        return;
    }
    KhdaysUiElement e;
    e.id = "hud.names";
    e.screen = 0;
    e.slot = KES_DIRECT | 3u; /* in the party's layer (BG3): the upper screen shows no sprites in the field */
    e.w = static_cast<float>(d.w * d.dot) / static_cast<float>(scale);
    e.h = static_cast<float>(d.h * d.dot) / static_cast<float>(scale);
    e.x = static_cast<float>(right) - e.w;
    e.y = static_cast<float>(top) + kLabelHeight / 2.0f - e.h / 2.0f;
    e.image = d.image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}

}  // namespace

extern "C" void khdays_ui_player_names_emit(int party_there) {
    keep_names();
    if (!party_there || khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_ON || !Session_IsActive()) {
        return;
    }
    const int players = at<std::int32_t>(static_cast<std::uint32_t>(Session_GetSetup()) + 4);
    const int own = static_cast<int>(Session_GetLocalPlayerIndex());
    if (players < 2 || players > kPlayers || own < 0 || own >= players) {
        return;
    }
    emit_name(own, kOwnRight, kOwnTop);
    for (int p = 0, row = 0; p < players; ++p) {
        if (p != own) {
            emit_name(p, kAllyRight, kAllyTop - kAllyStep * row++);
        }
    }
}
