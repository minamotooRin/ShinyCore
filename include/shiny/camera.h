#pragma once
#include <cstdint>

struct ScWorld;
struct ScCameraPoint { float x{},y{}; };
struct ScCameraRect { float x{},y{},w{},h{}; };
enum class ScCameraBounds { map, none, custom };
struct ScCamera {
    float zoom{1},rotation{},smoothing{.16f};
    bool pixel_snap{true};
    ScCameraBounds bounds{ScCameraBounds::map};
    ScCameraRect rectangle{};
    float shake_amplitude{};
    bool shake_pending{};
    std::uint32_t shake_seed{1},shake_frame{},shake_frames{};
};
struct ScCameraView {
    ScCameraPoint center{},offset{};
    float zoom{1},rotation{};
    ScCameraRect visible{}; // Conservative world AABB, including shake.
};
ScCameraView sc_camera_view(const ScWorld&,bool snap=true) noexcept;
ScCameraView sc_camera_mix(const ScCameraView&,const ScCameraView&,float alpha,bool snap) noexcept;
ScCameraPoint sc_camera_to_screen(const ScCameraView&,ScCameraPoint) noexcept;
ScCameraPoint sc_camera_to_world(const ScCameraView&,ScCameraPoint) noexcept;
void sc_camera_step(ScWorld&,bool follow=true) noexcept;
