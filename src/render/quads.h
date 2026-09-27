#pragma once
#include <raylib.h>
#include <cstdint>
class ScGpuMaterials;

// Ordered textured quads shared by tiles, projectiles and particles. No allocation.
class ScQuadBatch final {
    ScGpuMaterials* materials_;
    Texture2D* (*texture_)(const char*);
    std::uint64_t material_{};
    unsigned int image_{};
    bool active_{},additive_{};
    void finish() noexcept;
public:
    ScQuadBatch(ScGpuMaterials* materials,Texture2D* (*texture)(const char*)):materials_(materials),texture_(texture) {}
    ~ScQuadBatch() { finish(); }
    ScQuadBatch(const ScQuadBatch&)=delete;
    ScQuadBatch& operator=(const ScQuadBatch&)=delete;
    void draw(unsigned int image,Rectangle uv,Rectangle destination,Color tint,std::uint64_t material=0,
              bool additive=false,bool flip_x=false,bool flip_y=false,bool diagonal=false);
};
