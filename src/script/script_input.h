#pragma once
#include "shiny/state.h"
struct lua_State;
void sc_script_input_register(lua_State*);
void sc_script_input_describe();
ScValue sc_script_input_contracts();
