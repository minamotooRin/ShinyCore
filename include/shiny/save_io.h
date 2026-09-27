#pragma once
#include "shiny/save.h"
#include <functional>
#include <memory>
#include <variant>

// Owns explicit checkpoint data, never a room, VM, or native resource handle.
struct ScSaveWriteRequest {
    std::string path,project;
    double data_version=1;
    ScValue record;
    ScValue::Object changes;
};
struct ScSaveReadRequest {
    std::string path,project;
    double data_version=1;
    std::vector<std::string> keys;
    ScValue snapshot; // Optional previously selected index; never select a different backup mid-read.
};
struct ScSaveDeleteRequest {
    std::string path,project;
    double data_version=1;
};
struct ScSaveReadResult {
    ScValue snapshot; // Null only when neither slot nor backup exists.
    ScValue::Object chunks; // Missing keys are omitted; no partial result on failure.
};
inline constexpr std::size_t SC_SAVE_ASYNC_BYTES=1024*1024;
inline constexpr std::size_t SC_SAVE_READ_KEYS=1024;

// Application-owned disk IO, one read, write or delete transaction at a time. Owner-thread calls only.
// The owner must exclude other disk operations on this slot until release().
// Destruction drains an accepted mutation; room destruction must not own this service.
class ScSaveIo final {
public:
    using Request=std::variant<ScSaveWriteRequest,ScSaveReadRequest,ScSaveDeleteRequest>;
    using Write=std::function<ScResult<void>(const ScSaveWriteRequest&)>;
    using Read=std::function<ScResult<ScSaveReadResult>(const ScSaveReadRequest&)>;
    using Delete=std::function<ScResult<void>(const ScSaveDeleteRequest&)>;
    explicit ScSaveIo(Write write={},Read read={},Delete remove={});
    ~ScSaveIo();
    ScSaveIo(const ScSaveIo&)=delete;
    ScSaveIo& operator=(const ScSaveIo&)=delete;
    std::uint64_t submit(Request request); // Pure-data preflight; no disk IO.
    ScResult<bool> advance(std::uint64_t request); // Pending=false, committed=true, failed=error.
    void retry(std::uint64_t request); // Only an observed failure; identical frozen payload.
    void release(std::uint64_t request); // Only an observed result; invalidates its ID.
    bool active() const noexcept { return active_; }
    bool reading() const noexcept { return active_&&reading_; }
    bool deleting() const noexcept { return active_&&deleting_; }
    std::uint64_t request() const noexcept { return active_?sequence_:0; }
    const std::optional<ScResult<ScSaveReadResult>>& outcome(std::uint64_t request) const { check(request); return observed_; }
private:
    struct State;
    std::unique_ptr<State> state_;
    std::optional<ScResult<ScSaveReadResult>> observed_;
    std::uint64_t sequence_{};
    bool active_{},reading_{},deleting_{};
    void check(std::uint64_t) const;
};
