/* The port's list of rooms on the game's own screens (rooms2d.c), when the HD screens' pieces are
 * absent: prepare.py's hooks in Ov006/Ov008's room and members screens take these strings and the
 * hand's place, or the game's own when the view is not the game look's. A string is written, UTF-16
 * ended by 0, into `out` (`units` long, in the game's memory) and `out` returned. The game's thread. */
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
