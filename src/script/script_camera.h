#pragma once
#include "shiny/state.h"
struct lua_State;
void sc_script_camera_register(lua_State*);
void sc_script_camera_describe();
ScValue sc_script_camera_contracts();
