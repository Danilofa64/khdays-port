/* Each room's signal and lock on the game's own list of rooms (room_icons.cpp), while the port's
 * list is drawn by the game's screens (ui/rooms2d.c). */
#ifndef KHDAYS_UI_ROOM_ICONS_H
#define KHDAYS_UI_ROOM_ICONS_H

#ifdef __cplusplus
extern "C" {
#endif

/* At the HUD part's VBlank, as the badges and the crowns. */
void khdays_ui_room_icons_emit(void);

#ifdef __cplusplus
}
#endif

#endif
