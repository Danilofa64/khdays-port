/* Mission Mode's character select on one screen (runtime/menus/charselect.h), in the pieces of the HD
 * collection's 358/2 Days theater (a mod's layouts/select/<lang>/select.txt: tools/theater_mission.py). */
#ifndef KHDAYS_HOST_SELECT_SCREEN_H
#define KHDAYS_HOST_SELECT_SCREEN_H

#include <SDL3/SDL.h>

/* The window's thread, after the game's picture: draws the screen over it while the game's runs
 * (and the mod gives it); whether it drew. */
int khdays_select_screen_draw(SDL_Renderer *renderer);
/* The renderer is gone: its textures with it. */
void khdays_select_screen_renderer_gone(void);

#endif
