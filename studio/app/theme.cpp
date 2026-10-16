// The studio's colour palettes (theme.h): the five of the maintainer's MoM viewer
// (mom_viewer's web/style.css, "palette families drawn from the five Kingdom
// Hearts menu references"), each colour where the viewer puts it -- the page,
// the panels and floating windows, their title bars and the fields, the
// buttons, the borders, the focus and the active row's mark.
#include "theme.h"

#include "imgui.h"

namespace studio {

namespace {

struct Palette {
    const char* id;
    const char* name;
    // 0xRRGGBB, the viewer's --theme-* colours
    unsigned bg, panel, panel2, border, text, muted, button, accent, highlight, primary;
};

const Palette kPalettes[] = {
    {"kh1", "KH1", 0x080719, 0x111027, 0x1b1940, 0x42428b, 0xf0edff, 0xa9a4c7, 0x25234e, 0x8a8dff, 0xf3a446, 0xb27b38},
    {"kh2", "KH2", 0x03051d, 0x080c2d, 0x101b4e, 0x173bd0, 0xf0f4ff, 0x91a8dc, 0x111d53, 0x248aff, 0xff8c2e, 0x174eea},
    {"kh3", "KH3", 0x151d26, 0x202b36, 0x2c3b49, 0x536575, 0xedf2f4, 0xacb9c1, 0x344451, 0x70c4d5, 0xd9e4e8, 0x367e91},
    {"days", "Days", 0x09090b, 0x171517, 0x242123, 0x70202f, 0xf4eeee, 0xb6a9ad, 0x302026, 0xe3314b, 0xffdf45,
     0xbd1f37},
    {"mom", "MoM", 0x07132c, 0x0c1d43, 0x132d5c, 0x187dd2, 0xeff9ff, 0x9fc6e3, 0x12366a, 0x22c8f4, 0xf5d34a, 0x1359d8},
};
constexpr int kCount = static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));
constexpr int kDefault = kCount - 1;  // MoM, as the viewer

const Palette* current = &kPalettes[kDefault];

ImVec4 rgb(unsigned c, float a = 1.0f) {
    return ImVec4(static_cast<float>((c >> 16) & 255u) / 255.0f, static_cast<float>((c >> 8) & 255u) / 255.0f,
                  static_cast<float>(c & 255u) / 255.0f, a);
}

// `a` toward `b` by `t`
ImVec4 mix(unsigned a, unsigned b, float t) {
    const ImVec4 x = rgb(a), y = rgb(b);
    return ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, 1.0f);
}

void palette_colours(ImGuiStyle& s, const Palette& p) {
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = rgb(p.text);
    c[ImGuiCol_TextDisabled] = rgb(p.muted);
    // floating windows and the side panels are the viewer's panels; the page
    // behind them (the studio's main window) takes theme_page()
    c[ImGuiCol_WindowBg] = rgb(p.panel);
    c[ImGuiCol_ChildBg] = rgb(p.panel);
    c[ImGuiCol_PopupBg] = rgb(p.panel);
    c[ImGuiCol_Border] = rgb(p.border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    // the fields: panel 2 with the border
    c[ImGuiCol_FrameBg] = rgb(p.panel2);
    c[ImGuiCol_FrameBgHovered] = mix(p.panel2, p.border, 0.25f);
    c[ImGuiCol_FrameBgActive] = mix(p.panel2, p.border, 0.40f);
    c[ImGuiCol_TitleBg] = rgb(p.panel2);
    c[ImGuiCol_TitleBgActive] = rgb(p.panel2);
    c[ImGuiCol_TitleBgCollapsed] = rgb(p.panel);
    c[ImGuiCol_MenuBarBg] = rgb(p.panel2);
    c[ImGuiCol_ScrollbarBg] = rgb(p.bg, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = rgb(p.button);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(p.border);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(p.accent);
    c[ImGuiCol_CheckMark] = rgb(p.highlight);
    c[ImGuiCol_CheckboxSelectedBg] = rgb(p.panel2);
    c[ImGuiCol_SliderGrab] = rgb(p.accent);
    c[ImGuiCol_SliderGrabActive] = rgb(p.highlight);
    // the buttons: their own colour, panel 2 under the pointer, the primary pressed
    c[ImGuiCol_Button] = rgb(p.button);
    c[ImGuiCol_ButtonHovered] = rgb(p.panel2);
    c[ImGuiCol_ButtonActive] = rgb(p.primary);
    // rows and headers: panel 2, lighter under the pointer, the primary chosen
    c[ImGuiCol_Header] = rgb(p.panel2);
    c[ImGuiCol_HeaderHovered] = mix(p.panel2, p.border, 0.30f);
    c[ImGuiCol_HeaderActive] = rgb(p.primary);
    c[ImGuiCol_Separator] = rgb(p.border);
    c[ImGuiCol_SeparatorHovered] = rgb(p.accent);
    c[ImGuiCol_SeparatorActive] = rgb(p.highlight);
    c[ImGuiCol_ResizeGrip] = rgb(p.border, 0.5f);
    c[ImGuiCol_ResizeGripHovered] = rgb(p.accent);
    c[ImGuiCol_ResizeGripActive] = rgb(p.highlight);
    c[ImGuiCol_InputTextCursor] = rgb(p.text);
    c[ImGuiCol_Tab] = rgb(p.panel);
    c[ImGuiCol_TabHovered] = rgb(p.panel2);
    c[ImGuiCol_TabSelected] = rgb(p.panel2);
    c[ImGuiCol_TabSelectedOverline] = rgb(p.accent);
    c[ImGuiCol_TabDimmed] = rgb(p.panel);
    c[ImGuiCol_TabDimmedSelected] = rgb(p.panel2);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(p.border);
    c[ImGuiCol_PlotLines] = rgb(p.accent);
    c[ImGuiCol_PlotLinesHovered] = rgb(p.highlight);
    c[ImGuiCol_PlotHistogram] = rgb(p.accent);
    c[ImGuiCol_PlotHistogramHovered] = rgb(p.highlight);
    c[ImGuiCol_TableHeaderBg] = rgb(p.panel2);
    c[ImGuiCol_TableBorderStrong] = rgb(p.border);
    c[ImGuiCol_TableBorderLight] = rgb(p.border, 0.5f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rgb(p.text, 0.03f);
    c[ImGuiCol_TextLink] = rgb(p.accent);
    c[ImGuiCol_TextSelectedBg] = rgb(p.accent, 0.35f);
    c[ImGuiCol_TreeLines] = rgb(p.border);
    c[ImGuiCol_DragDropTarget] = rgb(p.highlight);
    c[ImGuiCol_NavCursor] = rgb(p.highlight);  // the focus, as the viewer's focused field
}

}  // namespace

int theme_count() {
    return kCount;
}

const char* theme_id(int index) {
    return kPalettes[index >= 0 && index < kCount ? index : kDefault].id;
}

const char* theme_label(int index, Language lang) {
    (void)lang;
    return kPalettes[index >= 0 && index < kCount ? index : kDefault].name;
}

int theme_index(const std::string& id) {
    for (int i = 0; i < kCount; ++i) {
        if (id == kPalettes[i].id)
            return i;
    }
    return kDefault;
}

ImVec4 theme_page() {
    return rgb(current->bg);
}

void apply_theme(const std::string& id) {
    ImGuiStyle& s = ImGui::GetStyle();
    const float k = s.FontScaleDpi > 0.0f ? s.FontScaleDpi : 1.0f;
    current = &kPalettes[theme_index(id)];
    ImGui::StyleColorsDark(&s);
    palette_colours(s, *current);
    // the viewer's corners (9 px floating windows, 6 px fields and buttons) and
    // its one-pixel borders around fields and buttons
    s.WindowRounding = 9.0f * k;
    s.ChildRounding = 8.0f * k;
    s.PopupRounding = 7.0f * k;
    s.FrameRounding = s.GrabRounding = s.TabRounding = 6.0f * k;
    s.ScrollbarRounding = 9.0f * k;
    s.FrameBorderSize = 1.0f;
}

}  // namespace studio
