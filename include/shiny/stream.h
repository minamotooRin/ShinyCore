#pragma once
#include "shiny/content_loader.h"
#include <deque>

// Room-owned cache and schedule. Only advance() publishes at a simulation boundary.
// No Lua, world or GPU data crosses the loader boundary.
class ScStream final {
public:
    using Reader=ScContentLoader::Reader;
    explicit ScStream(const std::string& index_path,std::size_t budget=128u*1024u*1024u,Reader reader={});
    ScStream(const std::string& index_path,ScContentLoader&,std::size_t budget=128u*1024u*1024u);
    ScStream(const std::string& index_path,ScValue index,ScContentLoader&,std::size_t budget=128u*1024u*1024u);
    ~ScStream();
    ScStream(const ScStream&)=delete;
    ScStream& operator=(const ScStream&)=delete;
    std::uint64_t request(int x,int y,std::uint64_t frame);
    ScResult<bool> advance(std::uint64_t frame); // false: due batch pending; error: retain previous visible set.
    ScResult<std::optional<ScValue>> get(int x,int y);
    void release(int x,int y);
    ScValue failure() const; // Only failures observed by advance(), never worker timing.
    void retry(std::uint64_t sequence); // Retains pins, reservation and the scheduled boundary.
    ScValue statistics() const;
    const ScValue& metadata() const noexcept { return metadata_; }
private:
    struct Key { int x,y; auto operator<=>(const Key&) const = default; };
    struct Entry {
        std::string path;
        std::size_t bytes{},charge{};
        std::uint64_t stamp{},sequence{},frame{},ticket{};
        unsigned references{};
        bool ready{},pending{},visible{};
        ScResult<ScValue> value=std::unexpected("not loaded");
    };
    ScStream(const std::string&,std::size_t,ScContentLoader*,Reader);
    ScStream(const std::string&,ScValue,std::size_t,ScContentLoader*,Reader);
    std::unique_ptr<ScContentLoader> owned_loader_; // Standalone native use only.
    ScContentLoader* loader_{};
    ScContentLoader::Layout layout_;
    ScValue metadata_;
    std::size_t budget_{},resident_{};
    std::uint64_t clock_{},sequence_{},frame_{};
    std::map<Key,Entry> entries_;
    std::optional<Key> failed_;
    std::deque<Key> pending_;
};
