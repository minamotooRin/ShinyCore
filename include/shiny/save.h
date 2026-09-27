#pragma once
#include "shiny/state.h"
#include <optional>

inline constexpr int SC_SAVE_FORMAT=3;
inline constexpr std::size_t SC_SAVE_BYTES=4*1024*1024;
inline constexpr std::size_t SC_SAVE_CHUNKS=16384;
inline constexpr std::size_t SC_SAVE_WORLD_BYTES=1024ull*1024*1024;
ScResult<void> sc_save_validate(const ScValue& record,std::string_view project,double data_version);
ScResult<void> sc_save_validate_changes(const ScValue::Object& changes);
ScResult<ScValue> sc_save_read(const std::string& path,std::string_view project,double data_version);
// A read selects one complete snapshot, including all referenced chunks. Individual
// reads then use that snapshot; they never silently mix it with the backup.
ScResult<std::optional<ScValue>> sc_save_read_chunk(const std::string& path,const ScValue& snapshot,std::string_view key);
// Changes map chunk keys to state objects; null removes a key. Existing chunks are
// retained. Only the index replacement commits; unreferenced records are collected.
ScResult<void> sc_save_write(const std::string& path,const ScValue& record,std::string_view project,double data_version,
                             const ScValue::Object& changes={});
ScResult<void> sc_save_delete(const std::string& path);
