#pragma once
#include "shiny/state.h"
#include <cstddef>
class ScScript;
struct lua_State;
inline constexpr std::size_t SC_DEBUG_PATH_LIMIT=16,SC_DEBUG_SCAN_LIMIT=65536;
// Borrow the paused VM only for this request; never execute Lua or retain references.
ScValue sc_debug_stack(ScScript&,lua_State*,const ScValue& request,bool locals);
std::string sc_debug_source_path(const ScScript&,const char* source);
