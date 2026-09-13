#pragma once
#include "shiny/input.h"
#include "shiny/state.h"

struct ScDeviceReplayEvent { std::uint64_t frame{}; ScDeviceInput input{}; };
ScResult<ScDeviceReplayEvent> sc_device_replay_event(const ScValue& value);
ScValue sc_input_snapshot(const ScDeviceInput& input);
