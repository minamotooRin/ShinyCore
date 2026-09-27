#pragma once
struct lua_State;
struct ScValue;
void sc_script_projectiles_register(lua_State*);
void sc_script_projectiles_describe();
ScValue sc_script_projectiles_contracts();
