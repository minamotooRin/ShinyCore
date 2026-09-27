#pragma once
#include "shiny/core.h"
#include "shiny/state.h"
struct ScNavigationRegion;
ScResult<ScValue> sc_project_map(ScWorld*,const std::string& root,const std::string& path);
ScResult<void> sc_project_resources(ScWorld*,const ScValue&,const std::string& root);
// Preflight both navigation grids before publishing geometry or either mask.
// local_update requires unchanged grid dimensions/origins and valid previous terrain masks.
ScResult<void> sc_project_tiles(ScWorld*,ScNavigationRegion* region=nullptr,bool local_update=false);
const ScTileGraphic* sc_tile_graphic(const ScWorld*,std::uint32_t gid);
