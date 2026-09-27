#include "shiny/save.h"
#include "shiny/path.h"
#include "shiny/core.h"
#include "shiny/identity.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

namespace {
const ScValue::Object empty_chunks;
const ScValue::Object& chunks(const ScValue& record) {
    const auto* value=record.get("chunks");
    return value?std::get<ScValue::Object>(value->data):empty_chunks;
}
bool hex(std::string_view value) {
    return value.size()==16 && value.find_first_not_of("0123456789abcdef")==value.npos;
}
bool chunk_file(std::string_view name) {
    if(name.size()<23 || !hex(name.substr(0,16)) || name[16]!='-' || !name.ends_with(".json")) return false;
    auto suffix=name.substr(17,name.size()-22);
    return !suffix.empty() && suffix.size()<=6 && suffix.find_first_not_of("0123456789")==suffix.npos;
}
std::string digest(const ScValue& state) {
    char text[17]; std::snprintf(text,sizeof text,"%016llx",static_cast<unsigned long long>(sc_data_hash(state)));
    return text;
}
std::string chunk_path(const std::string& path,const ScValue& reference) {
    return path+".chunks/"+reference.get("file")->text();
}
bool reference_valid(const ScValue& reference) {
    auto object=std::get_if<ScValue::Object>(&reference.data);
    auto file=reference.get("file"),hash=reference.get("hash"),bytes=reference.get("bytes");
    return object && object->size()==3 && file && hash && bytes && chunk_file(file->text()) &&
        hex(hash->text()) && file->text().starts_with(hash->text()+"-") &&
        std::isfinite(bytes->number(-1)) && bytes->number(-1)>=2 && bytes->number()<=SC_STATE_BYTES &&
        std::floor(bytes->number())==bytes->number();
}
ScResult<ScValue> read_chunk(const std::string& path,const ScValue& reference) {
    if(!reference_valid(reference)) return std::unexpected("invalid chunk reference");
    const auto file=chunk_path(path,reference);
    std::error_code error;
    const auto bytes=std::filesystem::file_size(sc_path(file),error);
    if(error || bytes!=static_cast<std::size_t>(reference.get("bytes")->number()))
        return std::unexpected("chunk size mismatch or missing: "+file);
    auto value=sc_json_file(file,SC_STATE_BYTES);
    if(!value) return std::unexpected(value.error());
    auto valid=sc_state_validate(*value);
    if(!valid) return std::unexpected(file+": "+valid.error());
    if(digest(*value)!=reference.get("hash")->text()) return std::unexpected("chunk checksum mismatch: "+file);
    return value;
}
ScResult<ScValue> read_snapshot(const std::string& index,const std::string& path,std::string_view project,double version) {
    auto record=sc_json_file(index,SC_SAVE_BYTES,17);
    if(!record) return record;
    auto valid=sc_save_validate(*record,project,version);
    if(!valid) return std::unexpected(valid.error());
    // Validate one bounded chunk at a time: failure selects the entire backup,
    // never a mixture of current state and a previous chunk revision.
    for(const auto& [key,reference]:chunks(*record)) {
        auto value=read_chunk(path,reference);
        if(!value) return std::unexpected("chunk '"+key+"': "+value.error());
    }
    return record;
}
ScResult<ScValue> write_chunk(const std::string& path,const ScValue& state) {
    const auto bytes=sc_json_write(state),hash=digest(state);
    for(unsigned collision=0;collision<100000;++collision) {
        auto name=hash+"-"+std::to_string(collision)+".json";
        auto target=path+".chunks/"+name;
        bool reusable=false;
        if(std::filesystem::exists(sc_path(target))) {
            auto existing=sc_json_file(target,SC_STATE_BYTES);
            reusable=existing && std::filesystem::file_size(sc_path(target))==bytes.size() && sc_json_write(*existing)==bytes;
            if(!reusable) continue; // Immutable files, including corrupt/colliding ones.
        } else {
            auto written=sc_atomic_write(target,bytes);
            if(!written) return std::unexpected(written.error());
        }
        return ScValue{ScValue::Object{{"file",ScValue{name}},{"hash",ScValue{hash}},
            {"bytes",ScValue{static_cast<double>(bytes.size())}}}};
    }
    return std::unexpected("chunk filename collision budget exhausted");
}
void collect(const std::string& path,const ScValue& current,const ScValue* previous) noexcept {
    try {
        std::set<std::string,std::less<>> keep;
        for(const auto& [key,reference]:chunks(current)) keep.insert(reference.get("file")->text());
        if(previous) for(const auto& [key,reference]:chunks(*previous)) keep.insert(reference.get("file")->text());
        std::error_code error;
        const auto directory=sc_path(path+".chunks");
        std::filesystem::directory_iterator it(directory,error),end;
        while(!error && it!=end) {
            const auto file=it->path(); auto name=file.filename().string();
            const bool temporary=name.ends_with(".tmp");
            if(temporary) name.resize(name.size()-4);
            if(chunk_file(name) && (temporary || !keep.contains(name)) && it->is_regular_file(error))
                std::filesystem::remove(file,error);
            it.increment(error);
        }
    } catch(...) {}
    // Collection is best-effort after commit. A cleanup failure must not report
    // the already committed transaction as failed; a later write retries it.
}
}
ScResult<void> sc_save_validate(const ScValue& record,std::string_view project,double data_version) {
    auto format=record.get("format"),owner=record.get("project"),version=record.get("data_version");
    auto scene=record.get("scene"),state=record.get("state");
    if(!format||format->number(-1)!=SC_SAVE_FORMAT) return std::unexpected("unsupported save format version");
    if(!owner||owner->text()!=project) return std::unexpected("save belongs to another project");
    if(!version||version->number(-1)!=data_version) return std::unexpected("unsupported save data version");
    if(!scene) return std::unexpected("save scene is missing");
    auto entry=scene->text();
    if(entry.size()>=SC_PATH_MAX||!sc_relative_path(entry)||!entry.ends_with(".lua")) return std::unexpected("invalid saved scene");
    if(!state) return std::unexpected("save state is missing");
    auto valid=sc_state_validate(*state);
    if(!valid) return std::unexpected(valid.error());
    for(const char* name:{"frame","saved_at"}) if(const auto* value=record.get(name)) {
        const double number=value->number(-1);
        if(!std::isfinite(number)||number<0||number>9007199254740991.0||std::floor(number)!=number)
            return std::unexpected(std::string("invalid save metadata: ")+name);
    }
    if(auto value=record.get("chunks")) {
        auto entries=std::get_if<ScValue::Object>(&value->data);
        if(!entries || entries->size()>SC_SAVE_CHUNKS) return std::unexpected("save chunks exceed capacity or are not an object");
        std::size_t total=0;
        for(const auto& [key,reference]:*entries) {
            if(!sc_identity_name_valid(key)) return std::unexpected("invalid save chunk key: "+key);
            if(!reference_valid(reference)) return std::unexpected("invalid save chunk reference: "+key);
            auto bytes=reference.get("bytes");
            total+=static_cast<std::size_t>(bytes->number());
            if(total>SC_SAVE_WORLD_BYTES) return std::unexpected("save world exceeds 1 GiB");
        }
    }
    if(sc_json_write(record).size()>SC_SAVE_BYTES) return std::unexpected("save index exceeds 4 MiB");
    return {};
}
ScResult<ScValue> sc_save_read(const std::string& path,std::string_view project,double version) {
    auto primary=read_snapshot(path,path,project,version);
    if(primary) return primary;
    auto backup=read_snapshot(path+".bak",path,project,version);
    if(backup) return backup;
    return std::unexpected(primary.error()+"; backup: "+backup.error());
}
ScResult<std::optional<ScValue>> sc_save_read_chunk(const std::string& path,const ScValue& snapshot,std::string_view key) {
    if(!sc_identity_name_valid(key)) return std::unexpected("invalid save chunk key");
    auto entries=snapshot.get("chunks");
    if(!entries) return ScResult<std::optional<ScValue>>{std::in_place,std::nullopt};
    if(!std::holds_alternative<ScValue::Object>(entries->data)) return std::unexpected("invalid chunk index");
    auto reference=entries->get(key);
    if(!reference) return ScResult<std::optional<ScValue>>{std::in_place,std::nullopt};
    auto value=read_chunk(path,*reference);
    if(!value) return std::unexpected(value.error());
    return ScResult<std::optional<ScValue>>{std::in_place,std::in_place,std::move(*value)};
}
ScResult<void> sc_save_validate_changes(const ScValue::Object& changes) {
    if(changes.size()>SC_SAVE_CHUNKS) return std::unexpected("chunk changes exceed capacity");
    // Preflight the entire batch before writing anything.
    for(const auto& [key,state]:changes) {
        if(!sc_identity_name_valid(key)) return std::unexpected("invalid save chunk key: "+key);
        if(std::holds_alternative<std::monostate>(state.data)) continue;
        auto bounded=sc_state_validate(state);
        if(!bounded) return std::unexpected("chunk '"+key+"': "+bounded.error());
    }
    return {};
}
ScResult<void> sc_save_write(const std::string& path,const ScValue& record,std::string_view project,double version,
                             const ScValue::Object& changes) {
    auto valid=sc_save_validate(record,project,version);
    if(!valid) return valid;
    valid=sc_save_validate_changes(changes);
    if(!valid) return valid;
    ScResult<ScValue> previous=std::unexpected("new slot");
    if(std::filesystem::exists(sc_path(path)) || std::filesystem::exists(sc_path(path+".bak"))) {
        previous=sc_save_read(path,project,version);
        if(!previous) return std::unexpected(previous.error()); // Do not erase a damaged world's unseen chunks.
    }
    auto candidate=record;
    auto references=previous?chunks(*previous):empty_chunks;
    for(const auto& [key,state]:changes) {
        if(std::holds_alternative<std::monostate>(state.data)) references.erase(key);
        else {
            // Placeholder references permit count/total/index size preflight before IO.
            references.insert_or_assign(key,ScValue{ScValue::Object{{"file",ScValue{digest(state)+"-99999.json"}},
                {"hash",ScValue{digest(state)}},{"bytes",ScValue{static_cast<double>(sc_json_write(state).size())}}}});
        }
    }
    auto& fields=std::get<ScValue::Object>(candidate.data);
    fields.insert_or_assign("chunks",ScValue{std::move(references)});
    valid=sc_save_validate(candidate,project,version);
    if(!valid) return valid;
    for(const auto& [key,state]:changes) if(!std::holds_alternative<std::monostate>(state.data)) {
        auto reference=write_chunk(path,state);
        if(!reference) return std::unexpected(reference.error());
        std::get<ScValue::Object>(fields.at("chunks").data).insert_or_assign(key,std::move(*reference));
    }
    if(previous) {
        auto copied=sc_atomic_write(path+".bak",sc_json_write(*previous));
        if(!copied) return copied;
    }
    auto committed=sc_atomic_write(path,sc_json_write(candidate));
    if(!committed) return committed;
    collect(path,candidate,previous?&*previous:nullptr);
    return {};
}
ScResult<void> sc_save_delete(const std::string& path) {
    // Removing the primary first leaves a usable backup if interrupted.
    std::error_code error;
    std::filesystem::remove(sc_path(path),error);
    if(error) return std::unexpected(error.message());
    std::filesystem::remove(sc_path(path+".bak"),error);
    if(error) return std::unexpected(error.message());
    collect(path,ScValue{},nullptr);
    std::filesystem::remove(sc_path(path+".chunks"),error); // Never recursively delete unrelated files.
    if(error) return std::unexpected(error.message());
    return {};
}
