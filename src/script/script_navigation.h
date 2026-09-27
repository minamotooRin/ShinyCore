#pragma once
#include "shiny/navigation.h"
#include "shiny/state.h"
struct lua_State;

void sc_script_navigation_register(lua_State*);
void sc_script_navigation_describe();
ScValue sc_script_navigation_contracts();
// Shared by explicit navigation regions and atomic streamed terrain commits.
std::unique_ptr<ScNavigationRegion> sc_prepare_navigation_region(double x,double y,const ScValue& rows,
    double cell_size,std::span<const ScTerrainShape> shapes);
