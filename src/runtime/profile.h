#pragma once
#include "shiny/profile.h"
#include "../profile_clock.h"
#include <iosfwd>

class ScScript;
struct ScFrameProfile {
    ScProfileClock::time_point start{};
    ScStepProfile step;
    ScRenderProfile render;
    double script_update_ms{}, script_draw_ms{}, simulation_ms{}, audio_ms{}, room_ms{}, diagnostic_ms{}, pace_ms{};
    int steps{};
};

// Diagnostic writer, outside the game simulation and Lua boundary.
class ScProfileWriter final {
    std::ostream& output_;
    ScProfileClock::time_point start_=ScProfileClock::now();
    std::uint64_t frames_{}, gpu_issued_{}, gpu_received_{};
    bool gpu_supported_;
public:
    ScProfileWriter(std::ostream&,bool graphics,bool gpu_supported,bool trace_or_record,const char* renderer);
    void frame(ScFrameProfile&,const ScScript&,std::uint64_t ticks);
    void finish();
};
