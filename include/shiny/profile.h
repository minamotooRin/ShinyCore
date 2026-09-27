#ifndef SHINY_PROFILE_H
#define SHINY_PROFILE_H

#include <array>
#include <cstddef>
#include <cstdint>

// Optional accumulated durations in milliseconds. Callers own and reset samples.
struct ScStepProfile {
    double physics_ms{}, entities_ms{}, projectiles_ms{}, particles_ms{};
};
struct ScGpuSample { std::uint64_t frame{}; double ms{}; };
struct ScRenderProfile {
    std::uint64_t frame{};
    double submit_ms{}, present_ms{}, poll_ms{}, capture_ms{};
    std::array<ScGpuSample,8> gpu{};
    std::size_t gpu_count{};
    bool gpu_issued{};
};

#endif
