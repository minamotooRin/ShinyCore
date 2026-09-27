#include "shiny/stream.h"
#include "shiny/path.h"
#include <filesystem>
#include <cmath>
#include <climits>
#include <stdexcept>
#include <algorithm>

namespace {
constexpr std::uint64_t max_sequence=(std::uint64_t{1}<<52)-1;
int integer(const ScValue* v,int low,int high) {
    if(!v||!std::holds_alternative<double>(v->data)) throw std::runtime_error("stream index requires integer coordinates or byte counts");
    double n=v->number();
    if(!std::isfinite(n)||n<low||n>high||std::floor(n)!=n) throw std::runtime_error("stream index integer outside range");
    return static_cast<int>(n);
}
void coordinates(int x,int y) {
    if(x<-31250||x>31250||y<-31250||y>31250) throw std::runtime_error("chunk coordinates outside range");
}
ScValue read_index(const std::string& path,std::size_t budget) {
    if(budget<65536||budget>128u*1024u*1024u) throw std::invalid_argument("stream cache budget must be 64 KiB..128 MiB");
    auto index=sc_json_file(path,16*1024*1024,32);
    if(!index) throw std::runtime_error(index.error());
    return std::move(*index);
}
}
ScStream::ScStream(const std::string& path,std::size_t budget,Reader reader)
    : ScStream(path,budget,nullptr,std::move(reader)) {}
ScStream::ScStream(const std::string& path,ScContentLoader& loader,std::size_t budget)
    : ScStream(path,budget,&loader,{}) {}
ScStream::ScStream(const std::string& path,ScValue index,ScContentLoader& loader,std::size_t budget)
    : ScStream(path,std::move(index),budget,&loader,{}) {}
ScStream::ScStream(const std::string& path,std::size_t budget,ScContentLoader* loader,Reader reader)
    : ScStream(path,read_index(path,budget),budget,loader,std::move(reader)) {}
ScStream::ScStream(const std::string& path,ScValue index,std::size_t budget,ScContentLoader* loader,Reader reader) : budget_(budget) {
    if(budget<65536||budget>128u*1024u*1024u) throw std::invalid_argument("stream cache budget must be 64 KiB..128 MiB");
    if(!index.get("format")||index.get("format")->number()!=3)
        throw std::runtime_error("unsupported stream index format; expected 3");
    integer(index.get("chunk_size"),32,32);
    layout_.width=integer(index.get("tilewidth"),1,256); layout_.height=integer(index.get("tileheight"),1,256);
    const auto* layers=index.get("layers");
    const auto* declared=layers?std::get_if<ScValue::Array>(&layers->data):nullptr;
    if(!declared||declared->size()>1000) throw std::runtime_error("stream index requires at most 1000 layers");
    for(std::size_t i=0;i<declared->size();++i)
        if(const auto* type=(*declared)[i].get("type");type&&type->text()=="objectgroup") layout_.object_layers.set(i);
    const auto parent=sc_path(path).parent_path().generic_u8string();
    const std::string root(reinterpret_cast<const char*>(parent.data()),parent.size());
    const auto* chunks=index.get("chunks");
    const auto* list=chunks?std::get_if<ScValue::Array>(&chunks->data):nullptr;
    if(!list||list->size()>65536) throw std::runtime_error("stream index requires at most 65536 chunks");
    for(const auto& chunk:*list) {
        Key key{integer(chunk.get("x"),-31250,31250),integer(chunk.get("y"),-31250,31250)};
        auto source=chunk.get("path"); auto relative=source?source->text():"";
        if(relative.empty()||relative.size()>255||relative.find('\0')!=relative.npos||relative.find_first_of("/\\:")!=relative.npos||relative=="."||relative=="..")
            throw std::runtime_error("chunk path must be a filename");
        Entry entry; entry.path=root+"/"+relative;
        entry.bytes=static_cast<std::size_t>(integer(chunk.get("bytes"),1,2*1024*1024));
        if(!entries_.emplace(key,std::move(entry)).second) throw std::runtime_error("duplicate stream chunk");
    }
    metadata_=std::move(index);
    if(loader) loader_=loader;
    else { owned_loader_=std::make_unique<ScContentLoader>(std::move(reader)); loader_=owned_loader_.get(); }
}
ScStream::~ScStream() {
    for(const auto& [key,entry]:entries_) if(entry.ticket) loader_->cancel(entry.ticket);
}
void ScStream::prefetch(int x,int y) {
    coordinates(x,y);
    auto found=entries_.find({x,y});
    if(found==entries_.end()) return;
    auto& entry=found->second;
    if(entry.references||entry.pending||entry.ready||entry.ticket||prefetching_.size()>=16) return;
    const auto charge=entry.charge?0:entry.bytes*64;
    if(charge>budget_) return;
    std::size_t available=budget_-resident_;
    for(const auto& [key,cached]:entries_)
        if(cached.charge&&!cached.references&&!cached.pending&&!cached.ticket) available+=cached.charge;
    if(available<charge) return;
    prefetching_.push_back(found->first);
    std::uint64_t ticket{};
    try { ticket=loader_->try_submit_chunk(entry.path,entry.bytes,x,y,layout_); }
    catch(...) { prefetching_.pop_back(); throw; }
    if(!ticket) { prefetching_.pop_back(); return; }
    while(resident_+charge>budget_) {
        auto victim=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it)
            if(it->second.charge&&!it->second.references&&!it->second.pending&&!it->second.ticket&&
               (victim==entries_.end()||it->second.stamp<victim->second.stamp)) victim=it;
        auto& old=victim->second; resident_-=old.charge; old.charge=0; old.ready=false;
        old.value=std::unexpected("evicted");
    }
    entry.charge+=charge; resident_+=charge; entry.ticket=ticket;
}
std::uint64_t ScStream::request(int x,int y,std::uint64_t frame) {
    coordinates(x,y);
    if(frame<frame_||frame>max_sequence) throw std::runtime_error("stream commit frame outside supported range");
    auto found=entries_.find({x,y});
    if(found==entries_.end()) return 0; // Known empty sparse regions need no disk gate.
    auto& entry=found->second;
    if(entry.references==UINT_MAX) throw std::runtime_error("chunk reference overflow");
    if(entry.references) {
        if(entry.pending&&entry.frame!=frame) throw std::runtime_error("pending chunk already has a different commit frame");
        ++entry.references; return entry.sequence;
    }
    if(entry.pending) throw std::runtime_error("chunk cancellation awaits its scheduled boundary");
    if(pending_.size()>=1024||sequence_==max_sequence) throw std::runtime_error("chunk request capacity exhausted");
    if(!pending_.empty()&&entries_.at(pending_.back()).frame>frame) throw std::runtime_error("chunk commit frames must follow request order");
    // Check the reservation before IO, but do not evict a reusable chunk until
    // the worker accepts the new request. Queue exhaustion must leave this cache
    // and its accounting unchanged.
    const auto charge=entry.charge?0:entry.bytes*64;
    if(charge) {
        if(charge>budget_) throw std::runtime_error("chunk exceeds cache allocation budget");
        std::size_t available=budget_-resident_;
        for(const auto& [key,cached]:entries_) if(!cached.references&&!cached.pending) available+=cached.charge;
        if(available<charge) throw std::runtime_error("cache budget exhausted by pinned or scheduled chunks");
    }
    pending_.push_back(found->first);
    try { if(!entry.ready&&!entry.ticket) entry.ticket=loader_->submit(entry.path,entry.bytes,x,y,layout_); }
    catch(...) { pending_.pop_back(); throw; }
    if(entry.ticket) {
        const auto hint=std::find(prefetching_.begin(),prefetching_.end(),found->first);
        if(hint!=prefetching_.end()) prefetching_.erase(hint);
    }
    if(charge) {
        while(resident_+charge>budget_) {
            auto victim=entries_.end();
            for(auto it=entries_.begin();it!=entries_.end();++it)
                if(it->second.charge&&!it->second.references&&!it->second.pending&&
                   (victim==entries_.end()||it->second.stamp<victim->second.stamp)) victim=it;
            auto& old=victim->second;
            if(old.ticket) {
                loader_->cancel(old.ticket); old.ticket=0;
                const auto hint=std::find(prefetching_.begin(),prefetching_.end(),victim->first);
                if(hint!=prefetching_.end()) prefetching_.erase(hint);
            }
            resident_-=old.charge; old.charge=0; old.ready=false;
            old.value=std::unexpected("evicted");
        }
        entry.charge=charge; resident_+=charge;
    }
    entry.pending=true; entry.references=1; entry.sequence=++sequence_; entry.frame=frame; entry.stamp=++clock_;
    return entry.sequence;
}
ScResult<bool> ScStream::advance(std::uint64_t frame) {
    if(frame<frame_||frame>max_sequence) return std::unexpected("stream frame must advance monotonically");
    frame_=frame;
    // Preflight the entire due prefix, then publish it atomically in request order.
    std::size_t due=0;
    for(const auto& key:pending_) {
        auto& entry=entries_.at(key);
        if(entry.frame>frame) break;
        if(!entry.ready) {
            auto result=loader_->take_chunk(entry.ticket);
            if(!result) return false;
            entry.value=std::move(*result); entry.ticket=0; entry.ready=true;
        }
        if(entry.references&&!entry.value) {
            failed_=key;
            return std::unexpected("chunk request "+std::to_string(entry.sequence)+": "+entry.value.error());
        }
        ++due;
    }
    for(std::size_t i=0;i<due;++i) {
        auto& entry=entries_.at(pending_.front()); pending_.pop_front();
        entry.pending=false; entry.visible=entry.references>0; entry.stamp=++clock_;
        if(!entry.value||!entry.references) {
            resident_-=entry.charge; entry.charge=0; entry.ready=false; entry.value=std::unexpected("released");
        }
    }
    failed_.reset();
    for(auto it=prefetching_.begin();it!=prefetching_.end();) {
        auto& entry=entries_.at(*it);
        auto result=loader_->take_chunk(entry.ticket);
        if(!result) { ++it; continue; }
        entry.ticket=0; entry.value=std::move(*result);
        entry.ready=bool(entry.value); entry.stamp=++clock_;
        it=prefetching_.erase(it);
    }
    return true;
}
ScResult<std::optional<ScValue>> ScStream::get(int x,int y) {
    coordinates(x,y);
    auto found=entries_.find({x,y});
    if(found==entries_.end()) return ScResult<std::optional<ScValue>>{std::in_place,std::in_place,ScValue::Object{
        {"x",ScValue{double(x)}},{"y",ScValue{double(y)}},{"layers",ScValue{ScValue::Object{}}},{"objects",ScValue{ScValue::Array{}}}}};
    auto& entry=found->second;
    if(!entry.references) return std::unexpected("request chunk before get");
    if(!entry.visible) return ScResult<std::optional<ScValue>>{std::in_place,std::nullopt};
    entry.stamp=++clock_;
    return ScResult<std::optional<ScValue>>{std::in_place,std::in_place,*entry.value};
}
void ScStream::release(int x,int y) {
    coordinates(x,y);
    auto found=entries_.find({x,y});
    if(found==entries_.end()) return;
    auto& entry=found->second;
    if(!entry.references) throw std::runtime_error("unbalanced chunk release");
    if(!--entry.references) {
        entry.visible=false;
        if(failed_&&*failed_==found->first) failed_.reset();
    }
}
ScValue ScStream::failure() const {
    if(!failed_) return {};
    const auto& entry=entries_.at(*failed_);
    return ScValue{ScValue::Object{{"sequence",ScValue{double(entry.sequence)}},{"frame",ScValue{double(entry.frame)}},
        {"x",ScValue{double(failed_->x)}},{"y",ScValue{double(failed_->y)}},{"message",ScValue{entry.value.error()}}}};
}
void ScStream::retry(std::uint64_t sequence) {
    if(!failed_||entries_.at(*failed_).sequence!=sequence)
        throw std::runtime_error("retry requires the current failed chunk request sequence");
    auto& entry=entries_.at(*failed_);
    // Enqueue first: allocation failure leaves the reported failure untouched.
    entry.ticket=loader_->submit(entry.path,entry.bytes,failed_->x,failed_->y,layout_);
    entry.ready=false;
    failed_.reset();
}
ScValue ScStream::statistics() const {
    std::size_t loaded=0,pinned=0;
    for(const auto& [key,entry]:entries_) { loaded+=entry.visible; pinned+=entry.references>0; }
    return ScValue{ScValue::Object{{"resident_bytes",ScValue{double(resident_)}},{"budget_bytes",ScValue{double(budget_)}},
        {"loaded",ScValue{double(loaded)}},{"pinned",ScValue{double(pinned)}},{"queued",ScValue{double(pending_.size())}},
        {"last_sequence",ScValue{double(sequence_)}}}};
}
