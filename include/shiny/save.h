#pragma once
#include "shiny/state.h"

inline constexpr int SC_SAVE_FORMAT=2;
inline constexpr std::size_t SC_SAVE_BYTES=SC_STATE_BYTES+8192;
ScResult<void> sc_save_validate(const ScValue& record,std::string_view project,double data_version);
ScResult<ScValue> sc_save_read(const std::string& path,std::string_view project,double data_version);
ScResult<void> sc_save_write(const std::string& path,const ScValue& record,std::string_view project,double data_version);
