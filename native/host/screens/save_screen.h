/* The port's save list on the screen (save_screen.c): what runtime/menus/savelist.h
 * says the list is, drawn over the whole window in a mod's pieces
 * (layouts/save/<lang>/save.txt, native/tools/save_screen.py) -- Dream Drop
 * Distance HD's save screen in Days's tones, with Days's faces and words.
 * The window's thread only. */
#ifndef KHDAYS_SAVE_SCREEN_H
#define KHDAYS_SAVE_SCREEN_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the list over whatever was rendered when it shows; nothing otherwise.
 * Returns whether it drew. */
int khdays_save_screen_draw(SDL_Renderer *renderer);

/* The renderer is about to go: its textures are freed (made again on the
 * next draw). */
void khdays_save_screen_renderer_gone(void);

#ifdef __cplusplus
}
#endif

#endif
