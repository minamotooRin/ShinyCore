#pragma once
#include "shiny/content_loader.h"
#include <span>

using ScImageId=std::uint64_t;
struct ScImageCacheStats {
    std::size_t budget_bytes{},resident_bytes{},capacity{},resident{},pinned{},pending{};
};

// Application-owned CPU residency, used only by the host thread. Requests share
// one loader, while active/candidate rooms retain independent references.
class ScImageCache final {
public:
    explicit ScImageCache(ScContentLoader&,std::size_t budget=128u*1024u*1024u,std::size_t capacity=128);
    ~ScImageCache();
    ScImageCache(const ScImageCache&)=delete;
    ScImageCache& operator=(const ScImageCache&)=delete;
    ScResult<ScImageId> acquire(ScImageRequest);
    ScResult<ScImageId> retain(ScImageId);
    ScResult<ScImageId> refresh(ScImageId); // Private new revision; old references remain valid.
    ScResult<void> publish(std::span<const ScImageId>); // Prefer these ready revisions on later acquire.
    ScResult<void> release(ScImageId);
    ScResult<bool> poll(ScImageId); // Observe only from host preparation, never gameplay timing.
    ScResult<void> retry(ScImageId); // Failed, pinned images only; same ID and declared dimensions.
    const ScImagePixels* pixels(ScImageId) const noexcept; // Borrowed until eviction/cache destruction.
    bool contains(ScImageId) const noexcept;
    bool pinned(ScImageId) const noexcept;
    ScImageCacheStats statistics() const noexcept;
private:
    struct Entry {
        ScImageId id{};
        ScImageRequest request;
        std::size_t charge{};
        unsigned references{};
        bool pending{},shared{};
        std::uint64_t stamp{},ticket{};
        ScResult<ScImagePixels> result=std::unexpected(std::string{});
    };
    Entry* find(ScImageId) noexcept;
    const Entry* find(ScImageId) const noexcept;
    void evict(Entry&) noexcept;
    ScResult<ScImageId> reserve(ScImageRequest,bool shared);
    ScContentLoader& loader_;
    std::vector<Entry> entries_;
    std::size_t budget_{},resident_{};
    std::uint64_t next_{},clock_{};
};
