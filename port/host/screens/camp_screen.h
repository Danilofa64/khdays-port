/* Mission Mode's camp menu on one screen (camp_screen.c): drawn over the one-screen picture, which
 * shows the screen's backdrop and the game's 3D model moved into the screen's window for it
 * (onescreen.c), while the game is on its menu page (runtime/menus/camp.h). */
#ifndef KHDAYS_HOST_CAMP_SCREEN_H
#define KHDAYS_HOST_CAMP_SCREEN_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* What the one-screen picture needs for the menu: the DS's frame of the model (its upper screen's
 * dots: every polygon in it is the model's) and the part of it the window shows, the screen's
 * window for it (1920 x 1080), the backdrop (RGBA, red first; kept while the port runs); `model`
 * 0 on the Preferences page: its own backdrop, nothing of the game's 3D. */
typedef struct KhdaysCampModel {
    float source[4], view[4], window[4];
    const uint8_t *backdrop;
    int backdrop_w, backdrop_h;
    int model;
} KhdaysCampModel;

/* The window's thread: whether the screen shows now, and its model's window; and whether the
 * one-screen picture took it (the screen draws only over that picture). */
int khdays_camp_screen_model(KhdaysCampModel *out);
void khdays_camp_screen_applied(int yes);
/* The window's thread: draws the screen over the window when the game is on its menu page; 0 when
 * it does not show. */
int khdays_camp_screen_draw(SDL_Renderer *renderer);
/* The renderer is gone: its textures with it. */
void khdays_camp_screen_renderer_gone(void);

#endif
