#pragma once
#include "shiny/state.h"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

// Owns exactly one reader. Worker threads never enter Lua, physics or graphics.
class ScStream final {
public:
    explicit ScStream(const std::string& index_path,std::size_t budget=128u*1024u*1024u);
    ~ScStream();
    ScStream(const ScStream&)=delete;
    ScStream& operator=(const ScStream&)=delete;
    void request(int x,int y);
    ScResult<ScValue> get(int x,int y); // Ordered caller commits; waits without advancing simulation.
    void release(int x,int y);
    ScValue statistics() const;
    const ScValue& metadata() const noexcept { return metadata_; }
private:
    struct Key { int x,y; auto operator<=>(const Key&) const = default; };
    struct Entry {
        std::string path;
        std::size_t charge{};
        std::uint64_t stamp{};
        unsigned references{};
        bool queued{},ready{};
        ScResult<ScValue> value=std::unexpected("not loaded");
    };
    ScValue metadata_;
    std::string root_;
    std::size_t budget_{},resident_{};
    std::uint64_t clock_{};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::map<Key,Entry> entries_;
    std::deque<Key> queue_;
    bool stopping_{};
    std::jthread worker_;
    void work();
};
