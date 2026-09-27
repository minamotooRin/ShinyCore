#include "shiny/content_loader.h"
#include "shiny/path.h"
#include "shiny/identity.h"
#include "png.h"
#include <filesystem>
#include <cmath>
#include <climits>
#include <stdexcept>
#include <set>
#include <deque>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
constexpr std::uint64_t max_sequence=(std::uint64_t{1}<<52)-1;
int integer(const ScValue* value,int low,int high) {
    const auto n=value?value->number(NAN):NAN;
    if(!std::isfinite(n)||n<low||n>high||std::floor(n)!=n)
        throw std::runtime_error("chunk requires integer coordinates or layer index in range");
    return static_cast<int>(n);
}
void validate_chunk(const ScValue& value,int x,int y,int width,int height,const std::bitset<1000>& object_layers) {
    if(integer(value.get("x"),-31250,31250)!=x||integer(value.get("y"),-31250,31250)!=y)
        throw std::runtime_error("chunk coordinates disagree with index");
    auto layers=value.get("layers");
    auto object=layers?std::get_if<ScValue::Object>(&layers->data):nullptr;
    if(!object||object->size()>64) throw std::runtime_error("chunk requires at most 64 layers");
    for(const auto& [name,layer]:*object) {
        if(name.empty()||name.size()>3||name.find_first_not_of("0123456789")!=name.npos)
            throw std::runtime_error("chunk layer key must be a numeric layer index");
        auto cells=std::get_if<ScValue::Array>(&layer.data);
        if(!cells||cells->size()!=1024) throw std::runtime_error("chunk layers require 1024 cells");
        for(const auto& cell:*cells) {
            double n=cell.number(-1);
            if(!std::isfinite(n)||n<0||n>UINT32_MAX||std::floor(n)!=n) throw std::runtime_error("invalid chunk GID");
        }
    }
    const auto* items=value.get("objects");
    const auto* objects=items?std::get_if<ScValue::Array>(&items->data):nullptr;
    if(!objects||objects->size()>4096) throw std::runtime_error("chunk requires at most 4096 objects");
    std::set<std::string> identities;
    for(const auto& item:*objects) {
        const auto* id=item.get("persistent_id");
        if(!id||!sc_identity_name_valid(id->text())||!identities.insert(id->text()).second)
            throw std::runtime_error("invalid or duplicate chunk persistent_id");
        const auto layer=integer(item.get("layer"),0,999);
        if(!object_layers.test(static_cast<std::size_t>(layer)))
            throw std::runtime_error("chunk object requires an object layer");
        for(const auto* axis:{"x","y"}) {
            const auto* position=item.get(axis);
            const double size=*axis=='x'?width:height;
            const double n=position?position->number(NAN):NAN;
            if(!std::isfinite(n)||std::abs(n/size)>1000000||std::floor(n/(size*32))!=(*axis=='x'?x:y))
                throw std::runtime_error("chunk object position disagrees with owner chunk");
        }
    }
}
std::size_t image_bytes(const ScImageRequest& request) {
    if(request.path.empty()||request.path.find('\0')!=request.path.npos)
        throw std::invalid_argument("PNG path is empty or contains NUL");
    if(request.width<1||request.width>8192||request.height<1||request.height>8192)
        throw std::invalid_argument("PNG dimensions must be 1..8192");
    const auto bytes=static_cast<std::size_t>(request.width)*static_cast<std::size_t>(request.height)*4;
    if(bytes>ScContentLoader::image_budget) throw std::runtime_error("PNG exceeds decoded image budget");
    return bytes;
}
}
struct ScContentLoader::State {
    struct Chunk { std::string path; std::size_t bytes; int x,y; Layout layout; };
    struct Index { std::string path; };
    struct Job {
        std::variant<Chunk,Index,ScImageRequest> request;
        std::size_t charge{};
        bool running{},cancelled{};
        std::optional<ScResult<Payload>> result;
    };
    Reader reader;
    IndexReader index_reader;
    ImageReader image_reader;
    std::mutex mutex;
    std::condition_variable condition;
    std::map<std::uint64_t,std::unique_ptr<Job>> jobs;
    std::deque<std::uint64_t> queue;
    std::uint64_t next{};
    std::size_t image_bytes{},chunk_count{},index_count{},image_count{};
    bool stopping{};
    std::jthread worker;
    void erase(std::uint64_t ticket) {
        const auto charge=jobs.at(ticket)->charge;
        image_bytes-=charge;
        if(charge) --image_count;
        else if(std::holds_alternative<Index>(jobs.at(ticket)->request)) --index_count;
        else --chunk_count;
        jobs.erase(ticket);
    }
    void work() {
        for(;;) {
            Job* job; std::uint64_t ticket;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock,[&]{return stopping||!queue.empty();});
                if(stopping) return;
                ticket=queue.front(); queue.pop_front(); job=jobs.at(ticket).get(); job->running=true;
            }
            ScResult<Payload> result=std::unexpected("content read failed");
            try {
                if(const auto* chunk=std::get_if<Chunk>(&job->request)) {
                    auto value=reader(chunk->path,chunk->bytes);
                    if(value) {
                        validate_chunk(*value,chunk->x,chunk->y,chunk->layout.width,chunk->layout.height,chunk->layout.object_layers);
                        result=Payload{std::move(*value)};
                    } else result=std::unexpected(std::move(value.error()));
                } else if(const auto* index=std::get_if<Index>(&job->request)) {
                    auto value=index_reader(index->path);
                    if(value) result=Payload{std::move(*value)};
                    else result=std::unexpected(std::move(value.error()));
                } else {
                    const auto& request=std::get<ScImageRequest>(job->request);
                    auto pixels=image_reader(request);
                    if(pixels) {
                        if(pixels->width!=request.width||pixels->height!=request.height||pixels->rgba.size()!=job->charge)
                            throw std::runtime_error("image reader returned inconsistent RGBA dimensions");
                        result=Payload{std::move(*pixels)};
                    } else result=std::unexpected(std::move(pixels.error()));
                }
            } catch(const std::exception& error) { result=std::unexpected(error.what()); }
            catch(...) { result=std::unexpected("content reader threw an unknown exception"); }
            {
                std::lock_guard lock(mutex);
                if(job->cancelled) {
                    result=std::unexpected(std::string{}); // Free pixels before releasing their reservation.
                    erase(ticket);
                }
                else { job->result=std::move(result); job->running=false; }
            }
        }
    }
    std::uint64_t submit(std::unique_ptr<Job> job,bool defer=false) {
        std::lock_guard lock(mutex);
        if(stopping) throw std::runtime_error("content loader stopped");
        // Two rooms' queues and at most one cancelled in-flight chunk/image job.
        if(next==max_sequence) throw std::runtime_error("application content job ID capacity exhausted");
        const bool index=std::holds_alternative<Index>(job->request);
        const auto count=index?index_count:job->charge?image_count:chunk_count;
        const auto limit=index?2u:job->charge?128u:2049u;
        if(count>=limit) {
            if(defer) return 0;
            const char* kind=index?"index":job->charge?"image":"chunk";
            throw std::runtime_error(std::string("application content ")+kind+" job capacity exhausted ("+
                std::to_string(count)+"/"+std::to_string(limit)+")");
        }
        if(job->charge>ScContentLoader::image_budget-image_bytes) {
            if(defer) return 0;
            throw std::runtime_error("queued image RGBA budget exhausted");
        }
        if(!worker.joinable()) worker=std::jthread([this]{
            try { work(); } catch(...) { std::lock_guard failed(mutex); stopping=true; }
        });
        const auto ticket=next+1,charge=job->charge;
        jobs.emplace(ticket,std::move(job));
        try { queue.push_back(ticket); } catch(...) { jobs.erase(ticket); throw; }
        image_bytes+=charge;
        if(charge) ++image_count; else if(index) ++index_count; else ++chunk_count;
        next=ticket; condition.notify_one(); return ticket;
    }
};
ScContentLoader::ScContentLoader(Reader reader,ImageReader image_reader,IndexReader index_reader) : state_(std::make_unique<State>()) {
    state_->reader=reader?std::move(reader):Reader{[](const std::string& file,std::size_t bytes)->ScResult<ScValue> {
        std::error_code error;
        const auto actual=std::filesystem::file_size(sc_path(file),error);
        if(error) return std::unexpected("cannot read chunk file (OS code "+std::to_string(error.value())+"): "+file);
        if(actual!=bytes) return std::unexpected("chunk byte count disagrees with index: "+file);
        return sc_json_file(file,bytes,20);
    }};
    state_->image_reader=image_reader?std::move(image_reader):ImageReader{sc_read_png};
    state_->index_reader=index_reader?std::move(index_reader):IndexReader{[](const std::string& path) {
        return sc_json_file(path,16u*1024u*1024u,32);
    }};
}
ScContentLoader::~ScContentLoader() {
    { std::lock_guard lock(state_->mutex); state_->stopping=true; }
    state_->condition.notify_all();
    if(state_->worker.joinable()) state_->worker.join();
}
std::uint64_t ScContentLoader::submit(const std::string& path,std::size_t bytes,int x,int y,const Layout& layout) {
    return state_->submit(std::make_unique<State::Job>(State::Job{State::Chunk{path,bytes,x,y,layout},0,false,false,{}}));
}
std::uint64_t ScContentLoader::try_submit_chunk(const std::string& path,std::size_t bytes,int x,int y,const Layout& layout) {
    return state_->submit(std::make_unique<State::Job>(State::Job{State::Chunk{path,bytes,x,y,layout},0,false,false,{}}),true);
}
std::uint64_t ScContentLoader::submit_image(ScImageRequest request) {
    const auto bytes=image_bytes(request);
    return state_->submit(std::make_unique<State::Job>(State::Job{std::move(request),bytes,false,false,{}}));
}
std::uint64_t ScContentLoader::try_submit_image(ScImageRequest request) {
    const auto bytes=image_bytes(request);
    return state_->submit(std::make_unique<State::Job>(State::Job{std::move(request),bytes,false,false,{}}),true);
}
std::uint64_t ScContentLoader::submit_index(const std::string& path) {
    if(path.empty()||path.find('\0')!=path.npos) throw std::invalid_argument("stream index path is empty or contains NUL");
    return state_->submit(std::make_unique<State::Job>(State::Job{State::Index{path},0,false,false,{}}));
}
std::optional<ScResult<ScContentLoader::Payload>> ScContentLoader::take(std::uint64_t ticket,Kind kind) {
    auto& s=*state_; std::unique_lock lock(s.mutex,std::try_to_lock);
    if(!lock.owns_lock()) return std::nullopt;
    const auto found=s.jobs.find(ticket);
    if(found==s.jobs.end()||found->second->cancelled) throw std::runtime_error("unknown content job");
    auto& job=*found->second;
    if((kind==Kind::chunk&&!std::holds_alternative<State::Chunk>(job.request))||
       (kind==Kind::index&&!std::holds_alternative<State::Index>(job.request))||
       (kind==Kind::image&&!std::holds_alternative<ScImageRequest>(job.request)))
        throw std::runtime_error("content job kind mismatch");
    if(!job.result&&!s.stopping) return std::nullopt;
    if(!job.result) job.result=std::unexpected("content loader stopped");
    auto result=std::move(job.result); s.erase(ticket); return result;
}
std::optional<ScResult<ScValue>> ScContentLoader::take_chunk(std::uint64_t ticket) {
    auto result=take(ticket,Kind::chunk);
    if(!result) return std::nullopt;
    if(!*result) return std::unexpected(std::move(result->error()));
    return std::get<ScValue>(std::move(**result));
}
std::optional<ScResult<ScImagePixels>> ScContentLoader::take_image(std::uint64_t ticket) {
    auto result=take(ticket,Kind::image);
    if(!result) return std::nullopt;
    if(!*result) return std::unexpected(std::move(result->error()));
    return std::get<ScImagePixels>(std::move(**result));
}
std::optional<ScResult<ScValue>> ScContentLoader::take_index(std::uint64_t ticket) {
    auto result=take(ticket,Kind::index);
    if(!result) return std::nullopt;
    if(!*result) return std::unexpected(std::move(result->error()));
    return std::get<ScValue>(std::move(**result));
}
void ScContentLoader::cancel(std::uint64_t ticket) noexcept {
    auto& s=*state_; std::lock_guard lock(s.mutex);
    const auto found=s.jobs.find(ticket); if(found==s.jobs.end()) return;
    if(found->second->running) found->second->cancelled=true;
    else { std::erase(s.queue,ticket); s.erase(ticket); }
}
