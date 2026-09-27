#pragma once
#include "shiny/state.h"
#include <bitset>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

struct ScImageRequest { std::string path; int width{},height{}; };
struct ScImagePixels { int width{},height{}; std::vector<unsigned char> rgba; };

// One application-owned CPU worker for map chunks and PNG images. Inputs/results
// own their storage; no room, Lua, world or GPU reference crosses this boundary.
// Consumers cancel their tickets before destruction; only this service joins.
class ScContentLoader final {
public:
    using Reader=std::function<ScResult<ScValue>(const std::string&,std::size_t)>;
    using ImageReader=std::function<ScResult<ScImagePixels>(const ScImageRequest&)>;
    static constexpr std::size_t image_budget=128u*1024u*1024u;
    explicit ScContentLoader(Reader reader={},ImageReader image_reader={});
    ~ScContentLoader();
    ScContentLoader(const ScContentLoader&)=delete;
    ScContentLoader& operator=(const ScContentLoader&)=delete;
    // Reserve RGBA bytes before queueing. take_image transfers storage and its
    // budget responsibility to the caller, which must enforce a resident budget.
    // nullopt means pending (including brief lock contention); consume once.
    std::uint64_t submit_image(ScImageRequest);
    std::uint64_t try_submit_image(ScImageRequest); // 0 while cancelled/in-flight jobs occupy the queue budget.
    std::optional<ScResult<ScImagePixels>> take_image(std::uint64_t);
    void cancel(std::uint64_t) noexcept;
private:
    struct Layout { int width{},height{}; std::bitset<1000> object_layers; };
    using Payload=std::variant<ScValue,ScImagePixels>;
    struct State;
    std::unique_ptr<State> state_;
    std::uint64_t submit(const std::string&,std::size_t,int,int,const Layout&);
    std::optional<ScResult<Payload>> take(std::uint64_t,bool image);
    std::optional<ScResult<ScValue>> take_chunk(std::uint64_t);
    friend class ScStream;
};
