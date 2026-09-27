#pragma once
struct lua_State;
void sc_script_systems_register(lua_State*);
void sc_script_systems_describe();
struct ScValue;
ScValue sc_script_stream_contracts();

// Decode into the script-owned batch and push a preallocated handle return table.
void sc_script_prepare_spawn_batch(lua_State*,int index);
void sc_script_push_spawn_ids(lua_State*);
