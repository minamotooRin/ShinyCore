#pragma once
#include "shiny/state.h"
#include <array>
#include <cstdint>
#include <span>
#include <optional>
#include <vector>
struct ScWorld;
struct ScEntity;
using ScMaterialId=std::uint64_t;
enum class ScUniformType { scalar,vec2,vec3,vec4,integer,boolean,texture };
struct ScUniform {
    char name[64]{},texture[128]{}; // Resolved project-relative PNG path.
    ScUniformType type{};
    std::array<float,4> values{};
    int integer{};
};
struct ScMaterial {
    ScMaterialId id{};
    bool postprocess{};
    std::uint64_t revision{1},compiled_revision{},attempted_revision{};
    std::string shader,source,error;
    std::array<ScUniform,32> uniforms{};
    std::size_t uniform_count{};
};
struct ScPostChain {
    std::array<ScMaterialId,4> passes{};
    std::size_t count{};
    std::uint64_t revision{1},budget_bytes{64*1024*1024},required_bytes{},allocated_bytes{};
    std::size_t target_count{};
    bool presented{};
    std::string error;
};
// CPU-only, room-owned content. No Lua, window, GPU objects or callbacks.
class ScMaterials final {
    struct EntityBinding { std::uint64_t entity{}; ScMaterialId material{}; };
    struct ImageBinding { std::string path; ScMaterialId material{}; };
    std::array<ScMaterial,64> slots_{};
    std::vector<EntityBinding> entities_;
    std::array<ImageBinding,64> images_{};
    std::size_t image_count_{};
    std::array<std::uint16_t,64> generations_{};
    std::uint32_t epoch_;
public:
    ScPostChain post;
    ScMaterials(std::uint32_t epoch,std::size_t entity_capacity):entities_(entity_capacity),epoch_(epoch) { generations_.fill(1); }
    ScMaterials(const ScMaterials&)=delete;
    ScMaterials& operator=(const ScMaterials&)=delete;
    ScResult<ScMaterialId> create(const ScWorld&,const std::string& root,const ScValue&);
    ScResult<void> set(const ScWorld&,ScMaterialId,const ScValue&);
    ScResult<void> reload(ScMaterialId,const std::string& root);
    ScResult<void> configure_post(const ScWorld&,const ScValue&,std::uint64_t budget_bytes);
    bool destroy(const ScWorld&,ScMaterialId) noexcept;
    ScResult<void> bind_entity(const ScWorld&,std::uint64_t,std::optional<ScMaterialId>);
    ScResult<void> bind_image(const ScWorld&,std::string_view,ScMaterialId);
    ScMaterialId entity_material(const ScEntity&) const noexcept;
    ScMaterialId image_material(std::string_view path) const noexcept;
    std::size_t entity_bindings(const ScWorld&) const noexcept;
    std::size_t entity_capacity() const noexcept { return entities_.size(); }
    std::size_t image_bindings() const noexcept { return image_count_; }
    ScMaterial* find(ScMaterialId) noexcept;
    std::span<ScMaterial> entries() noexcept { return slots_; }
    std::size_t size() const noexcept;
};
// Throws a contextual content error; reads at most 64 KiB and rejects empty/NUL sources.
std::string sc_shader_source(const std::string& root,const std::string& relative);
