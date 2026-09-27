#pragma once
struct lua_State;
struct ScValue;
void sc_script_application_register(lua_State*);
void sc_script_application_describe();
ScValue sc_script_application_contracts();
