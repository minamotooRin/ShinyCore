#pragma once
#include "materials.h"
struct ScColorTarget;
// Two color-only targets at most; UI is composed after this bounded chain.
class ScPostProcess final {
    std::array<std::unique_ptr<ScColorTarget>,2> targets_;
    int width_{},height_{};
    std::uint64_t failed_revision_{};
    std::string allocation_error_;
    void report(ScPostChain&) const noexcept;
public:
    ScPostProcess();
    ~ScPostProcess();
    ScPostProcess(ScPostProcess&&) noexcept;
    ScPostProcess& operator=(ScPostProcess&&) noexcept;
    void clear() noexcept;
    ScResult<void> prepare(ScPostChain&,int width,int height);
    Texture2D apply(Texture2D scene,ScMaterials*,ScGpuMaterials&,Texture2D* (*texture)(const char*));
};
