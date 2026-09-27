#pragma once
#include "shiny/state.h"
struct lua_State;
void sc_script_lighting_register(lua_State*);
void sc_script_lighting_describe();
ScValue sc_script_lighting_contracts();
