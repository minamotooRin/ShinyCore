#pragma once
#include <raylib.h>

// Color-only, point/clamp RGBA8 target, shared by normals and postprocessing.
struct ScColorTarget final {
    RenderTexture2D value{};
    ScColorTarget()=default;
    ~ScColorTarget();
    ScColorTarget(const ScColorTarget&)=delete;
    ScColorTarget& operator=(const ScColorTarget&)=delete;
    void open(int width,int height); // Called once on a fresh owner; failure cleans up through RAII.
};
