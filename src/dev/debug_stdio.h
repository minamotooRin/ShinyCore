#pragma once
#include "shiny/state.h"
#include <cstdint>
#include <string>
#include <map>
#include <set>
#include <functional>
class ScScript;
struct lua_State;
struct lua_Debug;
// Main-thread protocol owner. No channel or buffers exist unless explicitly enabled.
class ScDebugStdio final {
public:
    explicit ScDebugStdio(bool break_on_entry=false);
    std::function<void(ScScript&)> pump; // Host device/network polling, never reenter this VM.
    std::function<ScValue(const ScValue&)> panel; // Graphical host only; no Lua execution.
    void attach(ScScript&,std::uint64_t frame);
    void service(ScScript&,std::uint64_t frame);
    bool running() const noexcept { return !paused_||steps_!=0; }
    bool exiting() const noexcept { return exiting_; }
    void completed(std::uint64_t frame);
    void finish(std::uint64_t frame);
    bool local_resume() noexcept { if(connected_) return false; paused_=false; return true; }
private:
    static bool hook(void*,ScScript&,lua_State*,lua_Debug*) noexcept;
    bool on_hook(ScScript&,lua_State*,lua_Debug*);
    void request(std::string_view,ScScript&,std::uint64_t);
    void event(const char*,std::uint64_t);
    bool connected_{true},paused_{true},discard_{};
    std::uint32_t steps_{};
    std::string input_;
    std::map<std::string,std::set<int>,std::less<>> breakpoints_;
    enum class LineStep { none,into,over,out } line_step_{};
    lua_State* stopped_vm_{}; // Borrowed only while suspended inside the Lua hook.
    std::uint64_t frame_{};
    int step_depth_{};
    bool polling_hook_{};
    bool break_on_entry_{},exiting_{};

};
