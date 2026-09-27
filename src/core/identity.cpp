#include "shiny/identity.h"
#include <bit>
#include <cstring>
#include <stdexcept>

bool sc_identity_name_valid(std::string_view name) noexcept {
    if(name.empty() || name.size()>127 || name.front()=='/' || name.back()=='/') return false;
    for(char byte:name) {
        const auto ch=static_cast<unsigned char>(byte);
        if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch=='.'||ch=='/'||ch==':')) return false;
    }
    for(std::size_t start=0;start<name.size();) {
        auto end=name.find('/',start);
        auto part=name.substr(start,end==name.npos?end:end-start);
        if(part.empty()||part=="."||part=="..") return false;
        if(end==name.npos) break;
        start=end+1;
    }
    return true;
}
const char* sc_identity_status_name(ScIdentityStatus status) noexcept {
    switch(status) {
    case ScIdentityStatus::active: return "active";
    case ScIdentityStatus::unloaded: return "unloaded";
    case ScIdentityStatus::deleted: return "deleted";
    default: return "absent";
    }
}
ScIdentities::ScIdentities(std::size_t capacity):capacity_(capacity) {
    if(capacity>65536) throw std::invalid_argument("persistent ID capacity must be 0..65536");
    if(capacity) slots_.resize(std::bit_ceil(capacity*2));
}
std::size_t ScIdentities::slot(std::string_view name) const noexcept {
    std::uint64_t hash=14695981039346656037ull;
    for(char byte:name) { hash^=static_cast<unsigned char>(byte); hash*=1099511628211ull; }
    auto index=static_cast<std::size_t>(hash)&(slots_.size()-1);
    while(slots_[index].status!=ScIdentityStatus::absent && name!=slots_[index].name)
        index=(index+1)&(slots_.size()-1);
    return index;
}
const ScIdentityRecord* ScIdentities::find(std::string_view name) const noexcept {
    if(slots_.empty()) return nullptr;
    const auto& record=slots_[slot(name)];
    return record.status==ScIdentityStatus::absent?nullptr:&record;
}
std::expected<void,const char*> ScIdentities::declare(std::string_view name) {
    if(!sc_identity_name_valid(name)) return std::unexpected("invalid persistent_id");
    if(auto* existing=find(name);existing) return {};
    if(count_==capacity_) return std::unexpected("persistent ID capacity exhausted");
    auto& record=slots_[slot(name)];
    std::memcpy(record.name,name.data(),name.size()); record.name[name.size()]=0;
    record.status=ScIdentityStatus::unloaded; ++count_;
    return {};
}
std::expected<void,const char*> ScIdentities::bind(std::string_view name,std::uint64_t entity) {
    if(!entity) return std::unexpected("persistent object binding requires a live handle");
    if(auto* record=find(name);record&&record->status==ScIdentityStatus::active) return std::unexpected("duplicate active persistent_id");
    if(auto result=declare(name);!result) return result;
    auto& record=slots_[slot(name)]; record.entity=entity; record.status=ScIdentityStatus::active;
    return {};
}
void ScIdentities::release(std::string_view name,std::uint64_t entity,bool deleted) noexcept {
    if(slots_.empty()||name.empty()) return;
    auto& record=slots_[slot(name)];
    if(record.status!=ScIdentityStatus::active||record.entity!=entity) return;
    record.entity=0; record.status=deleted?ScIdentityStatus::deleted:ScIdentityStatus::unloaded;
}
std::expected<void,const char*> ScIdentities::erase(std::string_view name) {
    if(auto* record=find(name);record&&record->status==ScIdentityStatus::active) return std::unexpected("destroy the active entity before marking it deleted");
    if(auto result=declare(name);!result) return result;
    slots_[slot(name)].status=ScIdentityStatus::deleted;
    return {};
}
