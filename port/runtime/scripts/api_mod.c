/* The mod table: the mod's own names and its subscriptions to the game's events. */
#include "api.h"

#include "events.h"
#include "game_link.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

static int frame(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)khdays_link_frame());
    return 1;
}

void khdays_api_mod_open(KhdaysMod *m)
{
    static const luaL_Reg functions[] = {
        {"on", khdays_script_events_on}, {"off", khdays_script_events_off}, {"frame", frame}, {NULL, NULL}};
    lua_State *L = m->L;
    luaL_newlib(L, functions);
    lua_pushstring(L, m->manifest.id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, m->manifest.name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, m->manifest.version);
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "mod");
}
