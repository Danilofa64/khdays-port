/* The pause over an HD movie (movie_pause.cpp): the DS's own scene pause, its panel composed from the
 * game's pieces (pause/pausebg_<lang>.pbg, etcicon.NSCR, pauseicon.NSCR) as PauseMenu_Open lays it out
 * for a scene -- its title, Continue and Skip. The game has no pause over its movies (START skips
 * them); the port shows this one over the HD video it plays instead (runtime/movies/movie_hd.h). */
#ifndef KHDAYS_HOST_MOVIE_PAUSE_H
#define KHDAYS_HOST_MOVIE_PAUSE_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The window's thread: the panel in the game's language `language` (1 English to 5 Spanish), the entry
 * `selected` lit (0 Continue, 1 Skip) in its blink's look `lit` (1 or 2), as a 256 x 192 texture of
 * `renderer` (the DS's screen, clear around the panel); NULL when the game's pieces cannot be read
 * (said once). */
SDL_Texture *khdays_movie_pause_panel(SDL_Renderer *renderer, int language, int selected, int lit);

/* The renderer is about to go (or to lose its textures): the panel's textures freed. */
void khdays_movie_pause_renderer_gone(void);

#ifdef __cplusplus
}
#endif

#endif
