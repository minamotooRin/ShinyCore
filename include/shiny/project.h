#pragma once
#include "shiny/core.h"
#include "shiny/state.h"
ScResult<ScValue> sc_project_map(ScWorld*,const std::string& root,const std::string& path);
ScResult<void> sc_project_resources(ScWorld*,const ScValue&,const std::string& root);
ScResult<void> sc_project_tiles(ScWorld*);
const ScTileGraphic* sc_tile_graphic(const ScWorld*,std::uint32_t gid);
