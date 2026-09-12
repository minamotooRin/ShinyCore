#ifndef SHINY_NET_LUA_H
#define SHINY_NET_LUA_H

extern "C" {
#include <lua.h>
}

inline constexpr unsigned SC_NET_LUA_MAX_SESSIONS = 4;

/* Attach sc.net to the sc table at the top of the stack, preserving that stack.
 * The optional guard is called before operations with network side effects.
 * Native sessions close on explicit close(), collection, and VM teardown. */
void sc_net_lua_register(lua_State *L, lua_CFunction require_mutable_guard);
/* Comma-prefixed entries for the host's JSON functions array. */
void sc_net_lua_describe(void);

#endif
