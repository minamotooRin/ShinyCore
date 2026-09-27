#pragma once
#include "shiny/state.h"
struct lua_State;
void sc_script_identity_register(lua_State*);
void sc_script_identity_describe();
ScValue sc_script_identity_contracts();
