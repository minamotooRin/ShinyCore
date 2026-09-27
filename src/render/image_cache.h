#pragma once
#include "shiny/image_cache.h"
#include "../platform/raylib_owner.h"
#include <raylib.h>
#include <span>

// Host-thread GPU residency follows CPU cache IDs. Clear before closing context.
class ScGpuImages final {
public:
    explicit ScGpuImages(ScImageCache&);
    ScGpuImages(const ScGpuImages&)=delete;
    ScGpuImages& operator=(const ScGpuImages&)=delete;
    ScResult<bool> prepare(std::span<const ScImageId>,std::size_t uploads=1);
    Texture2D* texture(ScImageId) noexcept;
    void collect() noexcept;
    void clear() noexcept;
    std::size_t count() const noexcept;
    std::size_t bytes() const noexcept;
private:
    using TextureOwner=Owned<Texture2D,IsTextureValid,UnloadTexture>;
    struct Entry { ScImageId id{}; TextureOwner texture; };
    ScImageCache& cache_; // Must outlive this adapter.
    std::vector<Entry> entries_;
};
