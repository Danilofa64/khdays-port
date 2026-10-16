// The settings menu in the HD collection's own menu's pieces (settings_skin.cpp):
// what native/tools/theater_settings.py makes from the player's copy into a mod
// (layouts/settings/<lang>/settings.txt, its atlas and its font), drawn by
// settings_menu.cpp where it draws its own rectangles when the mod is there.
//
// Places are the pieces' picture's (1920 x 1080), mapped onto the window: its
// width onto the window's width, its height onto the window's height.
#ifndef KHDAYS_SETTINGS_SKIN_H
#define KHDAYS_SETTINGS_SKIN_H

#include "imgui.h"

#include <SDL3/SDL.h>

// Loads the pieces for `language` (KHDAYS_LANGUAGE_*) if they are not those
// loaded (with the renderer's textures). Returns whether they are there.
bool khdays_skin_use(SDL_Renderer *renderer, int language);

// The renderer goes: its textures are freed (loaded again with the next).
void khdays_skin_renderer_gone();

// The menu's font in ImGui's atlas (once, before a frame begins): the pieces'
// font, a TrueType font's glyphs (`fallback_ttf`, not copied) where it has
// none. NULL without the pieces' font.
ImFont *khdays_skin_font(ImFontAtlas *atlas, const void *fallback_ttf, int fallback_size);

// A point of the picture in the window of `size`.
ImVec2 khdays_skin_point(ImVec2 size, float x, float y);

// The screen behind the menu, over the whole window.
void khdays_skin_screen(ImDrawList *dl, ImVec2 size);

// A piece where it lies in the picture, in the window of `size` (the faded
// picture of the section shown: "emblem_screen" and the others).
bool khdays_skin_place(ImDrawList *dl, const char *piece, ImVec2 size);

// A piece in a rectangle: `piece` and `state` as the file names them ("row",
// "selected"); a piece of a left, a middle and a right part has its ends
// scaled with the rectangle's height and its middle stretched between them
// (its other states placed as they lie against its shown one); a stacked one
// ("line") likewise upright; one of a whole part is stretched over it.
// Returns whether the piece is there.
bool khdays_skin_piece(ImDrawList *dl, const char *piece, const char *state, ImVec2 min, ImVec2 max);

// The question's window over a rectangle, and its title where it lies.
bool khdays_skin_question(ImDrawList *dl, ImVec2 min, ImVec2 max);

// A number's slider across a rectangle (its track's), `t` 0 to 1 along it.
bool khdays_skin_slider(ImDrawList *dl, const char *state, ImVec2 min, ImVec2 max, float t);

// A piece's shown size in the picture (its parts' extent), or false.
bool khdays_skin_size(const char *piece, float *w, float *h);

// Where a piece's shown parts lie in the picture, or false.
bool khdays_skin_bounds(const char *piece, float *x0, float *y0, float *x1, float *y1);

#endif
