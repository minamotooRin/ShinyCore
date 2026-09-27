#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <span>

struct ScWorld;
struct ScPointLight {
    float x{},y{},radius{128},intensity{1},softness{},height{32};
    int samples{1};
    std::uint32_t color{0xffffffffu};
    std::uint64_t ignore{};
    bool shadows{true};
};
struct ScNormalBinding { std::string image,normal; };
struct ScNormalMaps {
    std::array<ScNormalBinding,64> bindings;
    std::size_t count{};
    const char* find(std::string_view image) const noexcept;
};
enum class ScOccluderMode : std::uint8_t { body,bounds,shape,none };
struct ScOccluderOverride { std::uint64_t entity{}; ScOccluderMode mode{}; };

// Room-owned presentation settings/diagnostics; never part of gameplay state.
struct ScLighting {
    static constexpr std::size_t occluder_capacity=8192, light_capacity=32, shadow_capacity=16;
    float softness{}; // Area-light source radius in authored pixels.
    int samples{1};
    std::array<ScPointLight,light_capacity> points{};
    std::size_t point_count{},occluders{},required_occluders{},lights{},shadow_lights{};
    bool presented{};
    std::string error;
    std::unique_ptr<ScNormalMaps> normal_maps;
    std::vector<ScOccluderOverride> occluder_modes; // Sized once from the room entity budget.
    std::uint64_t normal_target_bytes{};
    static constexpr std::uint64_t normal_budget_bytes=64*1024*1024;
};
struct ScLightSample { float x{},y{},weight{}; };
ScLightSample sc_light_sample(float softness,int samples,int index);
struct ScLightFrame {
    std::array<ScPointLight,ScLighting::light_capacity> lights{};
    std::size_t count{},shadow_count{};
};
// Pure presentation preflight, shared by headless drawing and native submission.
// Failure clears output; no partial light frame is consumed.
std::expected<void,const char*> sc_collect_lights(const ScWorld&,const ScLighting&,ScLightFrame&,float alpha=1);
std::expected<void,std::string> sc_normal_bind(const ScWorld&,ScLighting&,std::string_view image,std::string_view normal);
// Column-major tangent-to-world XY basis, matching the renderer's UV operations.
std::array<float,4> sc_normal_basis(bool flip_x,bool flip_y,bool diagonal,float angle);
ScOccluderMode sc_occluder_mode(std::span<const ScOccluderOverride>,std::uint64_t entity) noexcept;
