#pragma once
#include "shiny/lighting.h"
#include "shiny/state.h"
#include <raylib.h>
#include <memory>

class ScGpuNormals final {
    struct Data;
    std::unique_ptr<Data> data_;
    bool drawing_{};
    float rotation_{};
public:
    ScGpuNormals();
    ~ScGpuNormals();
    ScGpuNormals(ScGpuNormals&&) noexcept;
    ScGpuNormals& operator=(ScGpuNormals&&) noexcept;
    void clear() noexcept;
    ScResult<void> prepare(ScLighting&,int width,int height);
    bool begin(float rotation);
    bool drawing() const noexcept { return drawing_; }
    void surface(const char* image=nullptr,Texture2D* (*texture)(const char*)=nullptr,bool flip_x=false,bool flip_y=false,bool diagonal=false,float angle=0);
    void end();
    unsigned int light(float x,float y,float height); // Primary texture persists across native batch flushes.
    void end_light();
};
