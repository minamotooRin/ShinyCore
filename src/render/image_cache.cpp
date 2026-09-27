#include "image_cache.h"
#include <algorithm>
#include <stdexcept>

ScGpuImages::ScGpuImages(ScImageCache& cache):cache_(cache) { entries_.resize(cache.statistics().capacity); }
ScResult<bool> ScGpuImages::prepare(std::span<const ScImageId> ids,std::size_t uploads) try {
    if(uploads<1||uploads>128||ids.size()>128) return std::unexpected("image preparation requires 1..128 uploads and at most 128 references");
    collect();
    // Finish the ordered CPU batch before any of its new textures are uploaded.
    for(auto id:ids) { auto ready=cache_.poll(id); if(!ready||!*ready) return ready; }
    std::size_t uploaded{};
    for(auto id:ids) {
        if(texture(id)) continue;
        if(uploaded==uploads) return false;
        auto slot=std::find_if(entries_.begin(),entries_.end(),[](const auto& entry){return !entry.id;});
        if(slot==entries_.end()) return std::unexpected("GPU image cache capacity exhausted");
        const auto& pixels=*cache_.pixels(id);
        // Borrow only for this synchronous main-thread upload. The cache owns pixels.
        Image view{const_cast<unsigned char*>(pixels.rgba.data()),pixels.width,pixels.height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        TextureOwner image{LoadTextureFromImage(view)};
        if(!image) return std::unexpected("GPU image upload failed for ID "+std::to_string(id));
        SetTextureFilter(image.get(),TEXTURE_FILTER_POINT);
        slot->texture=std::move(image); slot->id=id; ++uploaded;
    }
    return true;
} catch(const std::exception& error) { return std::unexpected(error.what()); }
Texture2D* ScGpuImages::texture(ScImageId id) noexcept {
    if(!cache_.pinned(id)) return nullptr;
    for(auto& entry:entries_) if(entry.id==id) return entry.texture.ptr();
    return nullptr;
}
void ScGpuImages::collect() noexcept {
    for(auto& entry:entries_) if(entry.id&&!cache_.contains(entry.id)) { entry.texture.reset(); entry.id=0; }
}
void ScGpuImages::clear() noexcept { for(auto& entry:entries_) { entry.texture.reset(); entry.id=0; } }
std::size_t ScGpuImages::count() const noexcept {
    return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),[](const auto& entry){return entry.id!=0;}));
}
std::size_t ScGpuImages::bytes() const noexcept {
    std::size_t result{};
    for(const auto& entry:entries_) if(entry.id) result+=static_cast<std::size_t>(entry.texture.get().width)*static_cast<std::size_t>(entry.texture.get().height)*4;
    return result;
}
