/* The port's list of rooms on the game's own screens (rooms2d.c): without the HD screens' pieces the
 * game's "Select a group to join" and its members' screen draw the port's list
 * (runtime/net/roomlist.h, its view in the game look) -- prepare.py's hooks in
 * Ov006/Ov008_UpdateMissionMenuOptionScreen and ..._SelectionScreen give the game's own text calls
 * these strings and its hand this place. Each gives the game's own back when the view is not the
 * game look's on that screen. A string is written, UTF-16 ended by 0, into `out` (`units` long: a
 * buffer of the hooked function, in the game's memory) and `out` returned. The game's thread. */
#ifndef KHDAYS_UI_ROOMS2D_H
#define KHDAYS_UI_ROOMS2D_H

#ifdef __cplusplus
extern "C" {
#endif

/* The list: plate 0-3's label (`game` the game's, `empty` its "------"), its colour (the game's 1
 * white, 3 grey), the text on its right (a room's players; NULL none), the hand's place (0 the
 * button, 1-4 a plate) and the screen's words by the game's number (0x3d the note, 0x3e the
 * button's). */
void *khdays_rooms2d_label(int plate, void *game, void *empty, void *out, int units);
int khdays_rooms2d_colour(int plate, int game);
void *khdays_rooms2d_detail(int plate, void *out, int units);
int khdays_rooms2d_hand(int game);
void *khdays_rooms2d_text(int id, void *game, void *out, int units);

/* The members' screen: the room's name (NULL when it is not known here) and the text on the right
 * of the `row`th plate the game filled (a member's ping; NULL none). */
void *khdays_rooms2d_title(void *out, int units);
void *khdays_rooms2d_member_detail(int row, void *out, int units);

#ifdef __cplusplus
}
#endif

#endif
