#pragma once
#include "shiny/camera.h"
#include <cstddef>
#include <cstdint>
#include <vector>

struct ScWorld;
struct ScEntity;
struct ScPose { float x{},y{},angle{}; };
struct ScDisplayPoint { float x{},y{}; bool valid{}; };
struct ScPreviousEntity { std::uint64_t id{}; ScPose pose{}; std::uint64_t parent{}; ScPose local{}; };
// Room-owned previous fixed state, never a substitute for simulation or save data.
struct ScPresentation {
    explicit ScPresentation(std::size_t capacity):entities(capacity) {}
    std::vector<ScPreviousEntity> entities;
    ScCameraView camera{};
    float camera_x{},camera_y{};
    std::uint64_t tick{};
    bool enabled{true},captured{},camera_valid{};
};
// May allocate; call only while loading. Disabling retains reserved storage.
void sc_presentation_configure(ScWorld&,bool enabled);
// Once per tick, before authored changes. sc_step provides a fallback for native callers.
void sc_presentation_capture(ScWorld&) noexcept;
void sc_presentation_snap(ScWorld&,std::uint64_t entity) noexcept;
void sc_presentation_snap_camera(ScWorld&) noexcept;
bool sc_presentation_ready(const ScWorld&) noexcept;
ScPose sc_display_pose(const ScWorld&,const ScEntity&,float alpha) noexcept;
ScCameraView sc_display_camera(const ScWorld&,float alpha) noexcept;
ScCameraPoint sc_display_camera_anchor(const ScWorld&,float alpha) noexcept;
ScCameraPoint sc_display_point(const ScWorld&,const ScDisplayPoint&,float x,float y,float alpha) noexcept;
