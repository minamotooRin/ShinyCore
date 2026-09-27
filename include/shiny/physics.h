#pragma once
#include "shiny/core.h"
#include <span>
#include <expected>
#include <optional>
bool sc_physics_polygon_valid(const float* vertices,int count);
bool sc_physics_body_valid(const ScEntity&);
void sc_physics_sync(ScWorld*);
void sc_physics_step(ScWorld*);
struct ScTerrainStats { std::size_t chunks{}; std::uint64_t replacements{}; };
// Read-only diagnostics; does not synchronize or allocate physics objects.
ScTerrainStats sc_physics_terrain_stats(const ScWorld&) noexcept;
// Explicit streamed-terrain replacement; removes finite room borders on success.
// Optional entering drafts are preflighted together and receive handles on success.
// Failure retains geometry, bodies, entity slots, identities and draft IDs.
std::expected<void,std::string> sc_physics_replace_terrain(ScWorld&,std::span<const ScTerrainShape>,std::span<ScEntity> entering={});
struct ScRay { ScEntityId id{}; float x{},y{},nx{},ny{},fraction{}; bool hit{}; };
ScRay sc_physics_ray(ScWorld*,float x,float y,float dx,float dy);
// World-space convex points plus radius: circle (1), capsule (2), polygon (3..8).
struct ScQueryShape { std::array<float,16> points{}; int count{}; float radius{}; };
// Borrowed until the next overlap query; terrain is 0, entities are unique and sorted.
std::span<const ScEntityId> sc_physics_overlap(ScWorld*,const ScQueryShape&);
ScRay sc_physics_sweep(ScWorld*,const ScQueryShape&,float dx,float dy);
using ScJointId = std::uint64_t; // 20 room bits, 24 generation bits, 8 slot bits; JSON-exact.
ScJointId sc_physics_joint(ScWorld*,int kind,ScEntityId a,ScEntityId b,float x,float y,float length,
                             std::optional<std::array<float,2>> anchor_b=std::nullopt);
bool sc_physics_joint_destroy(ScWorld*,ScJointId id);
struct ScJointControl {
    bool limit{},motor{};
    float lower{},upper{},speed{},max_effort{};
    bool operator==(const ScJointControl&) const = default;
};
std::expected<ScJointControl,const char*> sc_physics_joint_control(ScWorld*,ScJointId id);
std::expected<void,const char*> sc_physics_joint_control(ScWorld*,ScJointId id,const ScJointControl&);
