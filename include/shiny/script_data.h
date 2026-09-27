#pragma once
#include "shiny/script.h"
// Internal C/Lua bridge. Readers do not raise Lua errors. Pushers own no C++ data.
ScResult<ScValue> sc_lua_read(lua_State* L,int index);
void sc_lua_push(lua_State* L,const ScValue& value);
void sc_script_data_register(lua_State* L);
void sc_script_project_load(lua_State* L);
void sc_script_data_describe();
ScValue sc_script_data_contracts();
