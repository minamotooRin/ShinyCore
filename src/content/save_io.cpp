#include "shiny/save_io.h"
#include "shiny/identity.h"
#include "shiny/path.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
ScResult<ScSaveReadResult> read_chunks(const ScSaveReadRequest& request) {
    ScSaveReadResult result{request.snapshot,{}};
    if(std::holds_alternative<std::monostate>(result.snapshot.data)) {
        if(!std::filesystem::exists(sc_path(request.path))&&!std::filesystem::exists(sc_path(request.path+".bak")))
            return result;
        auto snapshot=sc_save_read(request.path,request.project,request.data_version);
        if(!snapshot) return std::unexpected(snapshot.error());
        result.snapshot=std::move(*snapshot);
    }
    std::size_t bytes=2;
    for(const auto& key:request.keys) {
        auto chunk=sc_save_read_chunk(request.path,result.snapshot,key);
        if(!chunk) return std::unexpected(chunk.error());
        if(*chunk) {
            bytes+=key.size()+sc_json_write(**chunk).size()+64;
            if(bytes>SC_SAVE_ASYNC_BYTES) return std::unexpected("async read chunks exceed 1 MiB");
            result.chunks.emplace(key,std::move(**chunk));
        }
    }
    return result;
}
}
struct ScSaveIo::State {
    struct Job {
        Request request;
        bool queued=true;
        std::optional<ScResult<ScSaveReadResult>> result;
    };
    Write write;
    Read read;
    std::mutex mutex;
    std::condition_variable condition;
    std::unique_ptr<Job> job;
    bool stopping{},failed{};
    std::jthread worker;
    void work() {
        for(;;) {
            Job* current;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock,[&]{return stopping||(job&&job->queued);});
                if(!job||!job->queued) return; // Shutdown drains a queued transaction.
                current=job.get(); current->queued=false;
            }
            ScResult<ScSaveReadResult> result;
            try {
                if(const auto* request=std::get_if<ScSaveWriteRequest>(&current->request)) {
                    auto written=write(*request);
                    if(!written) result=std::unexpected(written.error());
                } else result=read(std::get<ScSaveReadRequest>(current->request));
            }
            catch(const std::exception& error) { result=std::unexpected(error.what()); }
            catch(...) { result=std::unexpected("unexpected save IO exception"); }
            { std::lock_guard lock(mutex); current->result=std::move(result); }
        }
    }
};
ScSaveIo::ScSaveIo(Write write,Read read) : state_(std::make_unique<State>()) {
    state_->write=write?std::move(write):Write{[](const ScSaveWriteRequest& request) {
        return sc_save_write(request.path,request.record,request.project,request.data_version,request.changes);
    }};
    state_->read=read?std::move(read):Read{read_chunks};
}
ScSaveIo::~ScSaveIo() {
    { std::lock_guard lock(state_->mutex); state_->stopping=true; }
    state_->condition.notify_one();
    if(state_->worker.joinable()) state_->worker.join();
}
std::uint64_t ScSaveIo::submit(Request request) {
    if(active_) throw std::runtime_error("save IO already has an unreleased transaction");
    if(sequence_==((std::uint64_t{1}<<52)-1)) throw std::runtime_error("save request sequence exhausted");
    std::visit([](auto& value) {
        if(value.path.empty()||value.path.find('\0')!=value.path.npos)
            throw std::runtime_error("save path must be nonempty without NUL");
        if(value.project.empty()||!std::isfinite(value.data_version)||value.data_version<1||std::floor(value.data_version)!=value.data_version)
            throw std::runtime_error("save IO requires project and positive integer data version");
    },request);
    if(auto* write=std::get_if<ScSaveWriteRequest>(&request)) {
        auto valid=sc_save_validate(write->record,write->project,write->data_version);
        if(!valid) throw std::runtime_error(valid.error());
        valid=sc_save_validate_changes(write->changes);
        if(!valid) throw std::runtime_error(valid.error());
        // Retained payload is bounded even when native callers bypass the Lua budget.
        std::size_t bytes=sc_json_write(write->record).size()+write->path.size()+write->project.size();
        for(const auto& [key,value]:write->changes) {
            bytes+=key.size()+sc_json_write(value).size()+64;
            if(bytes>SC_SAVE_ASYNC_BYTES) break;
        }
        if(bytes>SC_SAVE_ASYNC_BYTES) throw std::runtime_error("async save payload exceeds 1 MiB");
    } else {
        auto& read=std::get<ScSaveReadRequest>(request);
        if(read.keys.size()>SC_SAVE_READ_KEYS) throw std::runtime_error("async read exceeds 1024 keys");
        std::sort(read.keys.begin(),read.keys.end());
        for(std::size_t i=0;i<read.keys.size();++i)
            if(!sc_identity_name_valid(read.keys[i])||(i&&read.keys[i]==read.keys[i-1]))
                throw std::runtime_error("async read keys must be valid and unique");
        if(!std::holds_alternative<std::monostate>(read.snapshot.data)) {
            auto valid=sc_save_validate(read.snapshot,read.project,read.data_version);
            if(!valid) throw std::runtime_error(valid.error());
        }
        if(read.path.size()+read.project.size()>SC_SAVE_ASYNC_BYTES)
            throw std::runtime_error("async read path/project exceed 1 MiB");
    }
    auto& s=*state_; std::lock_guard lock(s.mutex);
    if(s.stopping) throw std::runtime_error("save IO stopped");
    const bool reading=std::holds_alternative<ScSaveReadRequest>(request);
    auto job=std::make_unique<State::Job>(State::Job{std::move(request),true,{}});
    if(!s.worker.joinable()) s.worker=std::jthread([&s] {
        try { s.work(); }
        catch(...) { std::lock_guard failed(s.mutex); s.failed=true; s.stopping=true; }
    });
    s.job=std::move(job); active_=true; reading_=reading; ++sequence_; s.condition.notify_one();
    return sequence_;
}
void ScSaveIo::check(std::uint64_t request) const {
    if(!active_||request!=sequence_) throw std::runtime_error("invalid or expired save request");
}
ScResult<bool> ScSaveIo::advance(std::uint64_t request) {
    check(request);
    if(!observed_) {
        auto& s=*state_; std::unique_lock lock(s.mutex,std::try_to_lock);
        if(!lock.owns_lock()) return false;
        if(s.failed) observed_=std::unexpected("save IO stopped before publishing a result");
        else if(s.job->result) observed_=std::move(s.job->result);
        else return false;
    }
    if(!*observed_) return std::unexpected(observed_->error());
    return true;
}
void ScSaveIo::retry(std::uint64_t request) {
    check(request);
    if(!observed_||*observed_) throw std::runtime_error("retry requires an observed save failure");
    auto& s=*state_; std::lock_guard lock(s.mutex);
    if(s.stopping) throw std::runtime_error("save IO stopped");
    s.job->result.reset(); s.job->queued=true; observed_.reset(); s.condition.notify_one();
}
void ScSaveIo::release(std::uint64_t request) {
    check(request);
    if(!observed_) throw std::runtime_error("observe save completion before release");
    auto& s=*state_; std::lock_guard lock(s.mutex);
    s.job.reset(); observed_.reset(); active_=false;
}
