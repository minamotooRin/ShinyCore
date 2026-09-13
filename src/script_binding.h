#pragma once
#include <lua.hpp>
#include <cstdio>
#include <exception>

// The wrapper has no owning locals across a Lua error. It also retains the
// original callback's upvalue layout, unlike a wrapper that invokes lua_call.
template<auto Function> int sc_lua_guard(lua_State* L) {
    char message[2048]{};
    try { return Function(L); }
    catch(const std::exception& error) { std::snprintf(message,sizeof message,"native operation: %s",error.what()); }
    catch(...) { std::snprintf(message,sizeof message,"unexpected native operation failure"); }
    return luaL_error(L,"%s",message);
}
