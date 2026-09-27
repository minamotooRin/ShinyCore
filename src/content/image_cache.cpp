#include "shiny/image_cache.h"
#include <algorithm>
#include <climits>
#include <stdexcept>

ScImageCache::ScImageCache(ScContentLoader& loader,std::size_t budget,std::size_t capacity)
    :loader_(loader),budget_(budget) {
    if(budget<65536||budget>ScContentLoader::image_budget||capacity<1||capacity>128)
        throw std::invalid_argument("image cache requires 64 KiB..128 MiB and 1..128 entries");
    entries_.resize(capacity);
}
ScImageCache::~ScImageCache() { for(auto& entry:entries_) if(entry.ticket) loader_.cancel(entry.ticket); }
ScImageCache::Entry* ScImageCache::find(ScImageId id) noexcept {
    if(id) for(auto& entry:entries_) if(entry.id==id) return &entry;
    return nullptr;
}
const ScImageCache::Entry* ScImageCache::find(ScImageId id) const noexcept {
    if(id) for(const auto& entry:entries_) if(entry.id==id) return &entry;
    return nullptr;
}
void ScImageCache::evict(Entry& entry) noexcept {
    if(entry.ticket) loader_.cancel(entry.ticket);
    resident_-=entry.charge; entry=Entry{};
}
ScResult<ScImageId> ScImageCache::acquire(ScImageRequest request) try {
    if(request.path.empty()||request.path.find('\0')!=request.path.npos||request.width<1||request.width>8192||request.height<1||request.height>8192)
        return std::unexpected("image requires a nonempty path and dimensions 1..8192");
    for(auto& entry:entries_) if(entry.id&&entry.shared&&entry.request.path==request.path) {
        if(entry.request.width!=request.width||entry.request.height!=request.height)
            return std::unexpected("cached image dimensions disagree with declaration: "+request.path);
        return retain(entry.id);
    }
    return reserve(std::move(request),true);
} catch(const std::exception& error) { return std::unexpected(error.what()); }
ScResult<ScImageId> ScImageCache::retain(ScImageId id) {
    auto* entry=find(id);
    if(!entry) return std::unexpected("invalid image reference");
    if(entry->references==UINT_MAX) return std::unexpected("image reference capacity exhausted");
    ++entry->references; entry->stamp=++clock_; return id;
}
ScResult<ScImageId> ScImageCache::refresh(ScImageId id) try {
    const auto* entry=find(id);
    if(!entry||!entry->references||entry->pending||!entry->result)
        return std::unexpected("reload requires a retained ready image");
    return reserve(entry->request,false);
} catch(const std::exception& error) { return std::unexpected(error.what()); }
ScResult<ScImageId> ScImageCache::reserve(ScImageRequest request,bool shared) {
    const auto charge=static_cast<std::size_t>(request.width)*static_cast<std::size_t>(request.height)*4;
    if(charge>budget_) return std::unexpected("image exceeds cache byte budget: "+request.path);
    if(next_==((std::uint64_t{1}<<52)-1)) return std::unexpected("image ID capacity exhausted");
    std::size_t available=budget_-resident_,slots{};
    for(const auto& entry:entries_) if(!entry.references) { available+=entry.charge; ++slots; }
    if(available<charge||!slots) return std::unexpected("image cache exhausted by pinned images");
    // Reserve logically now; poll queues IO at the host boundary. A cancelled
    // in-flight decode may still occupy the loader, independently of disk speed.
    Entry* slot=nullptr;
    while(!slot||resident_+charge>budget_) {
        for(auto& entry:entries_) if(!entry.id) { slot=&entry; break; }
        if(slot&&resident_+charge<=budget_) break;
        Entry* victim=nullptr;
        for(auto& entry:entries_) if(entry.id&&!entry.references&&(!victim||entry.stamp<victim->stamp)) victim=&entry;
        evict(*victim);
    }
    // No allocating operation follows eviction.
    slot->request=std::move(request); slot->id=++next_; slot->charge=charge;
    slot->pending=true; slot->shared=shared; slot->references=1; slot->stamp=++clock_; resident_+=charge;
    return slot->id;
}
ScResult<void> ScImageCache::publish(std::span<const ScImageId> ids) {
    for(auto id:ids) {
        const auto* entry=find(id);
        if(!entry||!entry->references||entry->pending||!entry->result)
            return std::unexpected("image publication requires retained ready revisions");
        for(auto other:ids) if(other!=id) {
            const auto* candidate=find(other);
            if(candidate&&candidate->request.path==entry->request.path)
                return std::unexpected("cannot publish different revisions of the same image");
        }
    }
    for(auto id:ids) {
        auto* entry=find(id);
        for(auto& old:entries_) if(old.id&&old.id!=id&&old.request.path==entry->request.path) {
            old.shared=false;
            if(!old.references) evict(old);
        }
        entry->shared=true;
    }
    return {};
}
ScResult<void> ScImageCache::release(ScImageId id) {
    auto* entry=find(id);
    if(!entry||!entry->references) return std::unexpected("invalid or unbalanced image release");
    if(!--entry->references) {
        entry->stamp=++clock_;
        if(entry->pending||!entry->shared) evict(*entry); // Discard cancelled work and unpublished/retired revisions.
    }
    return {};
}
ScResult<bool> ScImageCache::poll(ScImageId id) try {
    auto* entry=find(id);
    if(!entry||!entry->references) return std::unexpected("image must be retained before preparation");
    if(entry->pending) {
        if(!entry->ticket) entry->ticket=loader_.try_submit_image(entry->request);
        if(!entry->ticket) return false;
        auto result=loader_.take_image(entry->ticket);
        if(!result) return false;
        entry->result=std::move(*result); entry->ticket=0; entry->pending=false;
    }
    if(!entry->result) return std::unexpected(entry->result.error());
    return true;
} catch(const std::exception& error) {
    if(auto* entry=find(id)) {
        if(entry->ticket) loader_.cancel(entry->ticket);
        entry->ticket=0; entry->pending=false; entry->result=std::unexpected(error.what());
    }
    return std::unexpected(error.what());
}
ScResult<void> ScImageCache::retry(ScImageId id) try {
    auto* entry=find(id);
    if(!entry||!entry->references||entry->pending||entry->result)
        return std::unexpected("only a retained failed image can be retried");
    entry->pending=true; return {};
} catch(const std::exception& error) { return std::unexpected(error.what()); }
const ScImagePixels* ScImageCache::pixels(ScImageId id) const noexcept {
    const auto* entry=find(id);
    return entry&&!entry->pending&&entry->result?&*entry->result:nullptr;
}
bool ScImageCache::contains(ScImageId id) const noexcept { return find(id)!=nullptr; }
bool ScImageCache::pinned(ScImageId id) const noexcept { const auto* entry=find(id); return entry&&entry->references; }
ScImageCacheStats ScImageCache::statistics() const noexcept {
    ScImageCacheStats result{budget_,resident_,entries_.size()};
    for(const auto& entry:entries_) if(entry.id) {
        ++result.resident;
        if(entry.references) ++result.pinned;
        if(entry.pending) ++result.pending;
    }
    return result;
}
