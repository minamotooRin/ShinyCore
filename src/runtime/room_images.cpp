#include "shiny/room_images.h"
#include <climits>
#include <set>
#include <stdexcept>

ScRoomImages::ScRoomImages(ScImageCache& cache,std::uint32_t epoch):cache_(cache),epoch_(epoch) {
    if(!epoch||epoch>0xfffff) throw std::invalid_argument("invalid image room epoch");
    active_.reserve(128); staged_.reserve(128); ids_.reserve(128);
}
ScRoomImages::~ScRoomImages() { release(staged_); release(active_); }
void ScRoomImages::release(std::vector<Binding>& bindings) noexcept {
    for(const auto& binding:bindings) if(binding.id) (void)cache_.release(binding.id);
    bindings.clear();
}
ScResult<std::uint64_t> ScRoomImages::prepare(std::vector<ScNamedImage> images) { return stage(std::move(images),false); }
ScResult<std::uint64_t> ScRoomImages::reload() try {
    if(active_.empty()) return std::unexpected("no committed streamed images to reload");
    std::vector<ScNamedImage> images; images.reserve(active_.size());
    for(const auto& binding:active_) images.push_back(binding.image);
    return stage(std::move(images),true);
} catch(const std::exception& error) { return std::unexpected(error.what()); }
ScResult<std::uint64_t> ScRoomImages::stage(std::vector<ScNamedImage> images,bool refresh) try {
    if(busy()) return std::unexpected("commit or cancel the image request before preparing another set");
    if(images.size()>128||sequence_==UINT32_MAX) return std::unexpected("room image request capacity exhausted");
    std::set<std::string> names;
    for(const auto& image:images)
        if(image.name.empty()||image.name.size()>127||image.name.find('\0')!=image.name.npos||!names.insert(image.name).second)
            return std::unexpected("image names must be distinct nonempty strings up to 127 bytes");
    for(auto& image:images) staged_.push_back({std::move(image),0});
    for(std::size_t i=0;i<staged_.size();++i) {
        auto& binding=staged_[i];
        ScImageId shared{};
        if(refresh) for(std::size_t j=0;j<i;++j) if(active_[j].id==active_[i].id) { shared=staged_[j].id; break; }
        auto acquired=shared?cache_.retain(shared):refresh?cache_.refresh(active_[i].id):cache_.acquire(binding.image.source);
        if(!acquired) { release(staged_); ids_.clear(); return std::unexpected(acquired.error()); }
        binding.id=*acquired; ids_.push_back(*acquired);
    }
    request_=(std::uint64_t{epoch_}<<32)|++sequence_;
    phase_=Phase::pending; failed_image_=0; error_.clear(); return request_;
} catch(const std::exception& error) {
    if(!busy()) { release(staged_); ids_.clear(); }
    return std::unexpected(error.what());
}
ScResult<bool> ScRoomImages::advance(const Upload& upload) {
    if(phase_==Phase::failed) return std::unexpected(error_);
    if(phase_!=Phase::pending) return true;
    for(const auto& binding:staged_) {
        auto ready=cache_.poll(binding.id);
        if(!ready) {
            failed_image_=binding.id; phase_=Phase::failed;
            error_="image "+binding.image.name+": "+ready.error(); return std::unexpected(error_);
        }
        if(!*ready) return false;
    }
    if(upload) {
        auto ready=upload(ids_);
        if(!ready) { phase_=Phase::failed; error_=ready.error(); return std::unexpected(error_); }
        if(!*ready) return false;
    }
    phase_=Phase::ready; return true;
}
ScResult<void> ScRoomImages::commit(std::uint64_t request) {
    if(request!=request_||phase_!=Phase::ready) return std::unexpected("image request is not ready to commit");
    auto published=cache_.publish(ids_); if(!published) return published;
    active_.swap(staged_); release(staged_); ids_.clear(); request_=0; phase_=Phase::idle; return {};
}
ScResult<void> ScRoomImages::cancel(std::uint64_t request) {
    if(!request||request!=request_) return std::unexpected("expired image request");
    release(staged_); ids_.clear(); request_=0; phase_=Phase::idle; return {};
}
ScResult<void> ScRoomImages::retry(std::uint64_t request) {
    if(request!=request_||phase_!=Phase::failed) return std::unexpected("image request has not failed");
    if(failed_image_) { auto result=cache_.retry(failed_image_); if(!result) return result; }
    failed_image_=0; error_.clear(); phase_=Phase::pending; return {};
}
ScResult<ScValue> ScRoomImages::status(std::uint64_t request) const {
    if(!request||request!=request_) return std::unexpected("expired image request");
    ScValue::Object result{{"request",ScValue{double(request_)}},{"status",ScValue{std::string{phase_==Phase::pending?"pending":phase_==Phase::ready?"ready":"failed"}}},
        {"count",ScValue{double(staged_.size())}}};
    if(phase_==Phase::failed) result.emplace("error",ScValue{error_});
    return ScValue{std::move(result)};
}
ScImageId ScRoomImages::image(std::string_view name) const noexcept {
    for(const auto& binding:active_) if(binding.image.name==name) return binding.id;
    return 0;
}
ScImageId ScRoomImages::path(std::string_view path) const noexcept {
    for(const auto& binding:active_) if(binding.image.source.path==path) return binding.id;
    return 0;
}
