#pragma once
#include "shiny/state.h"
struct lua_State;
void sc_script_save_register(lua_State*);
void sc_script_save_describe();
ScValue sc_script_save_contracts();
