#ifndef SHINY_PROFILE_CLOCK_H
#define SHINY_PROFILE_CLOCK_H

#include <chrono>

using ScProfileClock=std::chrono::steady_clock;
inline double sc_profile_ms(ScProfileClock::time_point start) {
    return std::chrono::duration<double,std::milli>(ScProfileClock::now()-start).count();
}
// No clock read when disabled; never install a timing hook in the Lua VM.
class ScProfileScope final {
    double* output_;
    ScProfileClock::time_point start_;
public:
    explicit ScProfileScope(double* output):output_(output),start_(output?ScProfileClock::now():ScProfileClock::time_point{}) {}
    ~ScProfileScope() { if(output_) *output_+=sc_profile_ms(start_); }
    ScProfileScope(const ScProfileScope&)=delete;
    ScProfileScope& operator=(const ScProfileScope&)=delete;
};

#endif
