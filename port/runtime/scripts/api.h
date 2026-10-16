/* The tables a mod's scripts see: mod (api_mod.c), game, player, mission and scene (api_state.c). */
#ifndef KHDAYS_SCRIPTS_API_H
#define KHDAYS_SCRIPTS_API_H

#include "mod.h"

void khdays_api_mod_open(KhdaysMod *mod);
void khdays_api_state_open(KhdaysMod *mod);

/* For a name that changes the game: 1 when this mod may now, else pushes nil and the reason and
 * returns 0 (a visual mod, a guest in a multiplayer session, no game yet). */
int khdays_api_may_change(struct lua_State *L, int needs_state);

#endif
