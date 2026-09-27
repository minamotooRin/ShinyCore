#pragma once
#include "shiny/state.h"
struct lua_State;
void sc_script_material_register(lua_State*);
void sc_script_material_describe();
ScValue sc_script_material_contracts();
