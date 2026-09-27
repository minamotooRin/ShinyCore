#pragma once
#include "shiny/core.h"
#include <expected>

inline constexpr std::size_t SC_ATTACHMENT_DEPTH = 32;
// Offsets locate the child bounds relative to the parent's unrotated top-left.
ScPose sc_attachment_transform(ScPose parent,float parent_w,float parent_h,
                               const ScEntity& child,ScPose local) noexcept;
std::expected<void,const char*> sc_attach(ScWorld&,ScEntityId child,ScEntityId parent,ScPose local) noexcept;
std::expected<void,const char*> sc_detach(ScWorld&,ScEntityId child) noexcept;
// No allocation; fixed poses clamp to the same +/-1e6 bounds as native movement.
void sc_attachments_sync(ScWorld&) noexcept;
bool sc_attachment_patch_valid(const ScEntity& before,const ScEntity& after) noexcept;
// Draft geometry must already satisfy spawn preflight. Zero means root; other
// entries are one-based indices in this same batch, including forward references.
std::expected<void,const char*> sc_attachment_batch_preflight(std::span<const ScEntity> drafts,
                                                             std::span<const std::size_t> parents) noexcept;
