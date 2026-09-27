#pragma once
#include "shiny/image_cache.h"
#include <span>

struct ScNamedImage { std::string name; ScImageRequest source; };
class ScRoomImages final {
public:
    using Upload=std::function<ScResult<bool>(std::span<const ScImageId>)>;
    ScRoomImages(ScImageCache&,std::uint32_t epoch);
    ~ScRoomImages();
    ScRoomImages(const ScRoomImages&)=delete;
    ScRoomImages& operator=(const ScRoomImages&)=delete;
    ScResult<std::uint64_t> prepare(std::vector<ScNamedImage>);
    ScResult<std::uint64_t> reload();
    ScResult<bool> advance(const Upload& upload={});
    ScResult<void> commit(std::uint64_t);
    ScResult<void> cancel(std::uint64_t);
    ScResult<void> retry(std::uint64_t);
    ScResult<ScValue> status(std::uint64_t) const;
    bool busy() const noexcept { return request_!=0; }
    bool failed() const noexcept { return phase_==Phase::failed; }
    ScImageId image(std::string_view name) const noexcept;
    ScImageId path(std::string_view path) const noexcept;
    ScImageCache& cache() const noexcept { return cache_; }
private:
    enum class Phase { idle,pending,ready,failed };
    struct Binding { ScNamedImage image; ScImageId id{}; };
    void release(std::vector<Binding>&) noexcept;
    ScResult<std::uint64_t> stage(std::vector<ScNamedImage>,bool refresh);
    ScImageCache& cache_;
    std::uint32_t epoch_{},sequence_{};
    std::uint64_t request_{};
    ScImageId failed_image_{};
    Phase phase_{};
    std::string error_;
    std::vector<Binding> active_,staged_;
    std::vector<ScImageId> ids_;
};
