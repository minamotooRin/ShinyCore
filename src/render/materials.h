#pragma once
#include "shiny/material.h"
#include <raylib.h>
#include <memory>
class ScGpuMaterials final {
    struct Program;
    std::array<std::unique_ptr<Program>,64> programs_;
    std::array<ScMaterialId,64> attempted_ids_{};
    std::array<std::uint64_t,64> attempted_revisions_{};
public:
    ScGpuMaterials();
    ~ScGpuMaterials();
    ScGpuMaterials(ScGpuMaterials&&) noexcept;
    ScGpuMaterials& operator=(ScGpuMaterials&&) noexcept;
    void clear() noexcept;
    ScResult<void> sync(ScMaterials*,bool require_all);
    bool ready(ScMaterialId) const noexcept;
    bool begin(ScMaterialId,Texture2D* (*texture)(const char*),Texture2D scene={});
};
