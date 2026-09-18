#include "shiny/stream.h"
#include "shiny/path.h"
#include <filesystem>
#include <cmath>
#include <climits>
#include <stdexcept>

namespace {
int integer(const ScValue* v,int low,int high) {
    if(!v||!std::holds_alternative<double>(v->data)) throw std::runtime_error("stream index requires integer coordinates");
    double n=v->number();
    if(n<low||n>high||std::floor(n)!=n) throw std::runtime_error("stream index integer outside range");
    return static_cast<int>(n);
}
}
ScStream::ScStream(const std::string& path,std::size_t budget) : budget_(budget) {
    if(budget<65536||budget>128u*1024u*1024u) throw std::invalid_argument("stream cache budget must be 64 KiB..128 MiB");
    auto index=sc_json_file(path,16*1024*1024,32);
    if(!index) throw std::runtime_error(index.error());
    if(integer(index->get("format"),1,1)!=1||integer(index->get("chunk_size"),32,32)!=32) throw std::runtime_error("unsupported stream format");
    const auto parent=sc_path(path).parent_path().generic_u8string();
    root_.assign(reinterpret_cast<const char*>(parent.data()),parent.size());
    const auto* chunks=index->get("chunks");
    const auto* list=chunks?std::get_if<ScValue::Array>(&chunks->data):nullptr;
    if(!list||list->size()>1048576) throw std::runtime_error("stream index requires bounded chunks");
    for(const auto& chunk:*list) {
        Key key{integer(chunk.get("x"),-31250,31250),integer(chunk.get("y"),-31250,31250)};
        auto source=chunk.get("path"); auto relative=source?source->text():"";
        if(relative.empty()||relative.find_first_of("/\\:")!=std::string::npos||relative=="."||relative=="..") throw std::runtime_error("chunk path must be a filename");
        Entry entry; entry.path=root_+"/"+relative;
        if(!entries_.emplace(key,std::move(entry)).second) throw std::runtime_error("duplicate stream chunk");
    }
    metadata_=std::move(*index);
    worker_=std::jthread([this]{
        try { work(); } catch(...) { { std::lock_guard lock(mutex_); stopping_=true; } condition_.notify_all(); }
    });
}
ScStream::~ScStream() {
    { std::lock_guard lock(mutex_); stopping_=true; }
    condition_.notify_all();
    if(worker_.joinable()) worker_.join();
}
void ScStream::request(int x,int y) {
    std::lock_guard lock(mutex_);
    auto found=entries_.find({x,y});
    if(found==entries_.end()) return; // Absent sparse chunks are empty, not pending terrain.
    auto& entry=found->second;
    if(entry.references==UINT_MAX) throw std::runtime_error("chunk reference overflow");
    ++entry.references; entry.stamp=++clock_;
    if(!entry.ready&&!entry.queued) {
        if(queue_.size()>=1024) { --entry.references; throw std::runtime_error("chunk request queue exhausted"); }
        entry.queued=true; queue_.push_back(found->first); condition_.notify_one();
    }
}
ScResult<ScValue> ScStream::get(int x,int y) {
    std::unique_lock lock(mutex_);
    auto found=entries_.find({x,y});
    if(found==entries_.end()) return ScValue{ScValue::Object{{"x",ScValue{double(x)}},{"y",ScValue{double(y)}},{"layers",ScValue{ScValue::Object{}}}}};
    auto& entry=found->second;
    if(!entry.references) return std::unexpected("request chunk before get");
    condition_.wait(lock,[&]{return stopping_||entry.ready;});
    if(stopping_) return std::unexpected("stream closed");
    entry.stamp=++clock_; return entry.value;
}
void ScStream::release(int x,int y) {
    std::lock_guard lock(mutex_);
    auto found=entries_.find({x,y});
    if(found==entries_.end()) return;
    if(!found->second.references) throw std::runtime_error("unbalanced chunk release");
    --found->second.references;
}
ScValue ScStream::statistics() const {
    std::lock_guard lock(mutex_); std::size_t loaded=0,pinned=0;
    for(const auto& [key,entry]:entries_) { (void)key; loaded+=entry.ready&&entry.value.has_value(); pinned+=entry.references>0; }
    return ScValue{ScValue::Object{{"resident_bytes",ScValue{double(resident_)}},{"budget_bytes",ScValue{double(budget_)}},
        {"loaded",ScValue{double(loaded)}},{"pinned",ScValue{double(pinned)}},{"queued",ScValue{double(queue_.size())}}}};
}
void ScStream::work() {
    for(;;) {
        Key key{}; std::string path;
        {
            std::unique_lock lock(mutex_); condition_.wait(lock,[&]{return stopping_||!queue_.empty();});
            if(stopping_) return;
            key=queue_.front(); queue_.pop_front(); path=entries_.at(key).path;
        }
        ScResult<ScValue> result=std::unexpected("chunk read failed"); std::size_t charge=0;
        try {
            // A conservative upper bound includes strings, map nodes and parsed values.
            auto bytes=std::filesystem::file_size(sc_path(path));
            if(bytes>budget_/64) result=std::unexpected("chunk exceeds cache allocation budget");
            else { charge=static_cast<std::size_t>(bytes)*64; result=sc_json_file(path,budget_/64,20); }
            if(result) {
                if(integer(result->get("x"),-31250,31250)!=key.x||integer(result->get("y"),-31250,31250)!=key.y)
                    throw std::runtime_error("chunk coordinates disagree with index");
                auto layers=result->get("layers");
                auto object=layers?std::get_if<ScValue::Object>(&layers->data):nullptr;
                if(!object||object->size()>64) result=std::unexpected("chunk requires at most 64 layers");
                else for(const auto& [name,value]:*object) {
                    (void)name; auto cells=std::get_if<ScValue::Array>(&value.data);
                    if(!cells||cells->size()!=1024) { result=std::unexpected("chunk layers require 1024 cells"); break; }
                    for(const auto& cell:*cells) {
                        double n=cell.number(-1);
                        if(n<0||n>UINT32_MAX||std::floor(n)!=n) { result=std::unexpected("invalid chunk GID"); break; }
                    }
                    if(!result) break;
                }
            }
        } catch(const std::exception& error) { result=std::unexpected(error.what()); }
        {
            std::lock_guard lock(mutex_); auto& entry=entries_.at(key);
            while(result&&resident_+charge>budget_) {
                auto victim=entries_.end();
                for(auto it=entries_.begin();it!=entries_.end();++it)
                    if(it->second.ready&&it->second.charge&&!it->second.references&&(victim==entries_.end()||it->second.stamp<victim->second.stamp)) victim=it;
                if(victim==entries_.end()) { result=std::unexpected("cache budget exhausted by pinned chunks"); break; }
                resident_-=victim->second.charge; victim->second.value=std::unexpected("evicted"); victim->second.charge=0; victim->second.ready=false;
            }
            entry.charge=result?charge:0; resident_+=entry.charge;
            entry.value=std::move(result); entry.ready=true; entry.queued=false;
        }
        condition_.notify_all();
    }
}
