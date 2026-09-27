#ifndef SHINY_GPU_TIMER_H
#define SHINY_GPU_TIMER_H
#include "shiny/profile.h"
const char* sc_gpu_renderer();

// Owned by the backend, released before its GL context. Never waits for a result.
class ScGpuTimer final {
    std::array<unsigned,8> queries_{};
    std::array<std::uint64_t,8> frames_{};
    std::size_t read_{}, count_{};
    bool active_{};
public:
    ScGpuTimer()=default;
    ScGpuTimer(const ScGpuTimer&)=delete;
    ScGpuTimer& operator=(const ScGpuTimer&)=delete;
    ~ScGpuTimer() { close(); }
    bool open();
    void close() noexcept;
    void begin(ScRenderProfile&);
    void end();
};
#endif
