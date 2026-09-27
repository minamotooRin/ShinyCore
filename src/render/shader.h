#pragma once
#include <raylib.h>
#include <rlgl.h>
#include <array>

// Shared fixed vertex stage; compilation never substitutes raylib's default shader.
class ScGpuShader final {
    unsigned int id_{};
    std::array<int,RL_MAX_SHADER_LOCATIONS> locations_{};
public:
    ScGpuShader()=default;
    ~ScGpuShader();
    ScGpuShader(const ScGpuShader&)=delete;
    ScGpuShader& operator=(const ScGpuShader&)=delete;
    ScGpuShader(ScGpuShader&&) noexcept;
    ScGpuShader& operator=(ScGpuShader&&) noexcept;
    void compile(const char* fragment);
    Shader get() noexcept { return {id_,locations_.data()}; }
    int uniform(const char* name) const;
};
