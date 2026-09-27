#pragma once
struct lua_State;
struct ScValue;
void sc_script_particles_register(lua_State*);
void sc_script_particles_describe();
ScValue sc_script_particles_contracts();
