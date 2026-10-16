/* The port's list of rooms on the screen (room_screen.c): what runtime/net/roomlist.h says the list
 * is, drawn over the whole window in the HD theater's pieces (layouts/rooms/<lang>/rooms.txt,
 * tools/hd/theater_mission.py), with the game's words for its own parts. The window's thread
 * only. */
#ifndef KHDAYS_ROOM_SCREEN_H
#define KHDAYS_ROOM_SCREEN_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the list over whatever was rendered when it shows; nothing otherwise. Returns whether it
 * drew. */
int khdays_room_screen_draw(SDL_Renderer *renderer);

/* An event of the window's: taken (returns 1) while a field of the list takes what is typed --
 * the keyboard's text, Backspace, Enter and Escape, and a gamepad's buttons (its directions as
 * the settings menu's text entry has them). */
int khdays_room_screen_event(const SDL_Event *event);

/* The renderer is about to go: its textures are freed (made again on the next draw). */
void khdays_room_screen_renderer_gone(void);

#ifdef __cplusplus
}
#endif

#endif
