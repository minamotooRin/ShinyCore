#pragma once
#include "script_api.h"
namespace sc_core_api {
extern const ScLuaContract clip, measure, map, objects, key, pad, connected, axis, action,
    tile, random, emit, tone, message, scene, rect, circle, text, tick, time, log;
ScValue types();
}
