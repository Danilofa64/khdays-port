/* The players' names on the field's HUD in multiplayer: a port addition.
 * The DS shows only their J labels. */
#ifndef KHDAYS_UI_PLAYER_NAMES_H
#define KHDAYS_UI_PLAYER_NAMES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the HUD's emit at every VBlank. Keeps the names the game shows
 * while a group forms; and while `party_there` (hud.c's "hud.portrait"
 * piece: the field's party on the upper screen) in a session of two or more,
 * gives the element "hud.names": each player's name beside its J label, in
 * white. */
void khdays_ui_player_names_emit(int party_there);

#ifdef __cplusplus
}
#endif

#endif
