#pragma once
#include "shiny/script.h"
void sc_script_body_patch(lua_State*,int,ScEntity*);
void sc_script_body_push(lua_State*,const ScEntity*);
void sc_script_physics_register(lua_State*);
void sc_script_physics_describe();
ScValue sc_script_physics_contracts();
