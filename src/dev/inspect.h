#pragma once
#include "shiny/state.h"
#include <string_view>
struct lua_State;
class ScScript;
// Optional script-owned weak UI registry. Inspection never executes Lua code.
void sc_debug_ui_init(lua_State*);
int sc_debug_ui_register(lua_State*);
ScValue sc_debug_ui_inspect(ScScript&,const ScValue& request);
ScValue sc_debug_inspect(ScScript&,std::string_view section,const ScValue& request);
