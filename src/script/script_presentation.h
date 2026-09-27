#pragma once
#include "shiny/state.h"
struct lua_State;
void sc_script_presentation_register(lua_State*);
void sc_script_presentation_describe();
ScValue sc_script_presentation_contracts();
