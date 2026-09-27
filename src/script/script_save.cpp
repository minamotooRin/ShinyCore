#include "script_save.h"
#include "script_api.h"
#include "shiny/script_data.h"
#include "shiny/identity.h"
#include "shiny/path.h"
#include "shiny/save.h"
#include "shiny/save_io.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void error_text(ScScript* s,const char* message) { std::snprintf(s->error,sizeof s->error,"%s",message); }
const char* key(lua_State* L,int index) {
    if (lua_type(L,index)!=LUA_TSTRING) luaL_error(L,"expected string key");
    size_t length=0; const char* value=lua_tolstring(L,index,&length);
    if (!length || length>128 || std::memchr(value,0,length)) luaL_error(L,"key must have 1..128 bytes without NUL");
    return value;
}
const char* save_slot(lua_State* L,int argument) {
    const char* slot=key(L,argument);
    for(const char* p=slot;*p;++p) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))
        luaL_error(L,"slot uses letters, digits, underscore or hyphen");
    return slot;
}
void pin_save_snapshot(ScScript* s,const char* slot,ScValue record) {
    // Prepare allocations before replacing the cached slot/index pair.
    std::string name(slot);
    s->save_snapshot=std::move(record); s->save_snapshot_slot=std::move(name);
}
ScValue save_summary(const ScValue& record) {
    if(std::holds_alternative<std::monostate>(record.data)) return {};
    ScValue::Object summary;
    for(const char* name:{"format","project","data_version","scene","state","frame","saved_at"})
        if(auto value=record.get(name)) summary.emplace(name,*value);
    const auto* entries=record.get("chunks");
    summary.emplace("chunk_count",ScValue{entries?static_cast<double>(std::get<ScValue::Object>(entries->data).size()):0.0});
    return ScValue{std::move(summary)};
}
void idle_writer(lua_State* L) {
    if(auto* writer=script(L)->save_io;writer&&writer->active())
        luaL_error(L,"release the asynchronous save request before other save operations");
}
int save_operation(lua_State* L,bool loading,bool chunked=false,bool asynchronous=false) {
    if(lua_gettop(L)!=(chunked?2:1)) return luaL_error(L,"unexpected save argument count");
    auto* s=script(L);
    if(s->phase!=1 || s->checking) return luaL_error(L,"save operations require update(), outside --check");
#ifdef SC_HAS_STREAMING
    if(loading&&s->images&&s->images->busy()) return luaL_error(L,"commit or cancel the image request before loading a save");
#endif
    const char* slot=save_slot(L,1);
    idle_writer(L);
    bool ok=false;
    std::uint64_t request=0;
    char saved_scene[SC_PATH_MAX]{};
    try {
        const auto* id=s->project.get("id");
        if (!id || id->text().empty()) throw std::runtime_error("saving requires project.id");
        auto version=s->project.get("data_version"); double v=version?version->number(1):1;
        auto path=s->save_directory+"/"+id->text()+"/"+slot+".json";
        if(asynchronous&&(!s->save_io||s->save_directory.empty()||s->pending_scene[0]))
            throw std::runtime_error("async save requires a host, disk save directory and no pending scene transition");
        ScValue::Object changes;
        if(chunked) {
            if(s->save_directory.empty()) throw std::runtime_error("chunk saves require a disk save directory; use --save-dir in headless mode");
            auto values=sc_lua_read(L,2);
            if(!values) throw std::runtime_error(values.error());
            auto object=std::get_if<ScValue::Object>(&values->data);
            if(!object) throw std::runtime_error("chunk changes must map keys to state objects or false for deletion");
            changes=std::move(*object);
            for(auto& [name,value]:changes) if(auto boolean=std::get_if<bool>(&value.data);boolean&&!*boolean) value=ScValue{};
        }
        if (!loading) {
            ScValue record(ScValue::Object{{"format",ScValue(double(SC_SAVE_FORMAT))},{"project",*id},{"data_version",ScValue(v)},
                {"scene",ScValue(std::string(s->entry))},{"state",s->state},
                {"frame",ScValue{double(s->world->tick)}},
                {"saved_at",ScValue{double(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count())}}});
            if(asynchronous) request=s->save_io->submit(ScSaveWriteRequest{path,id->text(),v,std::move(record),std::move(changes)});
            else if(s->save_directory.empty()) {
                if(!s->memory_saves.contains(slot)&&s->memory_saves.size()>=16) throw std::runtime_error("memory save capacity exhausted (16)");
                s->memory_saves.insert_or_assign(slot,std::move(record));
            }
            else { auto result=sc_save_write(path,record,id->text(),v,changes); if(!result) throw std::runtime_error(result.error()); }
            s->save_snapshot=ScValue{}; s->save_snapshot_slot.clear();
        } else {
            if(s->pending_scene[0]) throw std::runtime_error("a scene transition is already pending");
            ScResult<ScValue> record=std::unexpected("save slot does not exist");
            if(s->save_directory.empty()) { auto it=s->memory_saves.find(slot); if(it!=s->memory_saves.end()) record=it->second; }
            else if(s->save_snapshot_slot==slot && !std::holds_alternative<std::monostate>(s->save_snapshot.data))
                record=s->save_snapshot;
            else record=sc_save_read(path,id->text(),v);
            if(!record) throw std::runtime_error(record.error());
            auto valid=sc_save_validate(*record,id->text(),v);
            if(!valid) throw std::runtime_error(valid.error());
            auto scene=record->get("scene"),state=record->get("state");
            auto entry=scene->text();
            if(entry.size()>=SC_PATH_MAX||entry.find('\0')!=std::string::npos||!sc_script_validate_path(entry.c_str())||!entry.ends_with(".lua")) throw std::runtime_error("invalid saved scene");
            auto bounded=sc_state_validate(*state); if(!bounded) throw std::runtime_error(bounded.error());
            pin_save_snapshot(s,slot,*record);
            s->scratch=std::move(*bounded);
            std::snprintf(saved_scene,sizeof saved_scene,"%s",entry.c_str());
        }
        ok=true;
    } catch(const std::exception& e) { error_text(s,e.what()); }
    if(ok&&loading) {
        s->pending_state=std::move(s->scratch); s->has_pending_state=true;
        std::memcpy(s->pending_scene,saved_scene,sizeof saved_scene);
    }
    if(ok) { if(asynchronous) lua_pushinteger(L,static_cast<lua_Integer>(request)); else lua_pushboolean(L,1); return 1; }
    lua_pushnil(L); lua_pushstring(L,s->error); return 2;
}
int save_write(lua_State* L) { return save_operation(L,false); }
int save_write_chunks(lua_State* L) { return save_operation(L,false,true); }
int save_write_async(lua_State* L) { return save_operation(L,false,false,true); }
int save_write_chunks_async(lua_State* L) { return save_operation(L,false,true,true); }
int save_load(lua_State* L) { return save_operation(L,true); }
int save_read_chunks_async(lua_State* L) {
    if(lua_gettop(L)!=2) return luaL_error(L,"read_chunks_async expects slot and keys");
    auto* s=script(L);
    if(s->phase!=1||s->checking||s->candidate) return luaL_error(L,"async chunk reads require update in an active room");
    const char* slot=save_slot(L,1);
    idle_writer(L);
    std::uint64_t request=0;
    try {
        if(!s->save_io||s->save_directory.empty()||s->pending_scene[0])
            throw std::runtime_error("async read requires a host, disk save directory and no pending scene transition");
        auto id=s->project.get("id"),version=s->project.get("data_version");
        if(!id) throw std::runtime_error("saving requires project.id");
        auto values=sc_lua_read(L,2);
        if(!values) throw std::runtime_error(values.error());
        ScSaveReadRequest input{s->save_directory+"/"+id->text()+"/"+slot+".json",id->text(),version?version->number():1,{},
            s->save_snapshot_slot==slot?s->save_snapshot:ScValue{}};
        if(const auto* keys=std::get_if<ScValue::Array>(&values->data)) {
            for(const auto& value:*keys) {
                const auto* key=std::get_if<std::string>(&value.data);
                if(!key) throw std::runtime_error("async read keys require an array of strings");
                input.keys.push_back(*key);
            }
        } else if(const auto* empty=std::get_if<ScValue::Object>(&values->data);!empty||!empty->empty())
            throw std::runtime_error("async read keys require an array of strings");
        std::string name(slot); // Allocate before accepting the request.
        request=s->save_io->submit(std::move(input));
        s->save_request_slot=std::move(name);
    } catch(const std::exception& error) { error_text(s,error.what()); }
    if(request) { lua_pushinteger(L,static_cast<lua_Integer>(request)); return 1; }
    lua_pushnil(L); lua_pushstring(L,s->error); return 2;
}
std::uint64_t request_id(lua_State* L) {
    if(lua_gettop(L)!=1||lua_type(L,1)!=LUA_TNUMBER) luaL_error(L,"expected one save request integer");
    int valid=0; const auto id=lua_tointegerx(L,1,&valid);
    if(!valid||id<1||id>((lua_Integer{1}<<52)-1)) luaL_error(L,"save request outside 1..2^52-1");
    if(!script(L)->save_io) luaL_error(L,"async save service unavailable in this host");
    return static_cast<std::uint64_t>(id);
}
int save_status(lua_State* L) {
    auto* s=script(L); const auto id=request_id(L);
    {
        const auto& result=s->save_io->outcome(id);
        ScValue::Object fields{{"request",ScValue{double(id)}},
            {"operation",ScValue{std::string(s->save_io->reading()?"read":"write")}},
            {"status",ScValue{std::string(!result?"pending":*result?"complete":"failed")}}};
        if(result&&!*result) fields.emplace("error",ScValue{result->error()});
        s->scratch=ScValue{std::move(fields)};
    }
    sc_lua_push(L,s->scratch); return 1;
}
int save_result(lua_State* L) {
    auto* s=script(L); const auto id=request_id(L);
    {
        const auto& result=s->save_io->outcome(id);
        if(!s->save_io->reading()||!result||!*result)
            throw std::runtime_error("result requires a successfully completed read request");
        ScValue::Object fields{{"chunks",ScValue{(*result)->chunks}}};
        if(!std::holds_alternative<std::monostate>((*result)->snapshot.data))
            fields.emplace("record",save_summary((*result)->snapshot));
        s->scratch=ScValue{std::move(fields)};
    }
    sc_lua_push(L,s->scratch); return 1;
}
template<bool Retry> int save_request_action(lua_State* L) {
    auto* s=script(L); const auto id=request_id(L);
    if((s->phase!=1&&s->phase!=4)||s->checking||s->candidate)
        return luaL_error(L,"save request actions require update or ui_update in an active room");
    if constexpr(Retry) s->save_io->retry(id);
    else {
        // Copy/pin before releasing the result; allocation failure leaves it retryable.
        const auto& result=s->save_io->outcome(id);
        const bool pin=s->save_io->reading()&&result&&*result
            &&!std::holds_alternative<std::monostate>((*result)->snapshot.data);
        if(pin) pin_save_snapshot(s,s->save_request_slot.c_str(),(*result)->snapshot);
        s->save_io->release(id);
        if(!pin) { s->save_snapshot=ScValue{}; s->save_snapshot_slot.clear(); }
        s->save_request_slot.clear();
    }
    lua_pushboolean(L,true); return 1;
}
int save_read(lua_State* L) {
    if(lua_gettop(L)!=1) return luaL_error(L,"read expects one slot");
    const char* slot=save_slot(L,1); auto* s=script(L); bool ok=false;
    idle_writer(L);
    {
        const auto* id=s->project.get("id");
        if(!id) throw std::runtime_error("saving requires project.id");
        const auto* version=s->project.get("data_version"); double v=version?version->number():1;
        ScResult<ScValue> record=std::unexpected("save slot does not exist");
        if(s->save_directory.empty()) { auto found=s->memory_saves.find(slot); if(found!=s->memory_saves.end()) record=found->second; }
        else record=sc_save_read(s->save_directory+"/"+id->text()+"/"+slot+".json",id->text(),v);
        if(record) {
            auto summary=save_summary(*record);
            pin_save_snapshot(s,slot,std::move(*record));
            s->scratch=std::move(summary); ok=true;
        }
        else error_text(s,record.error().c_str());
    }
    if(ok) { sc_lua_push(L,s->scratch); return 1; }
    lua_pushnil(L); lua_pushstring(L,s->error); return 2;
}
int save_read_chunk(lua_State* L) {
    if(lua_gettop(L)!=2) return luaL_error(L,"read_chunk expects slot and key");
    auto* s=script(L); const char* slot=save_slot(L,1); const char* name=key(L,2);
    if(s->phase>=2 || s->checking) return luaL_error(L,"chunk reads require load, init or update, outside check mode");
    idle_writer(L);
    bool ok=false,found=false;
    try {
        if(!sc_identity_name_valid(name)) throw std::runtime_error("invalid save chunk key");
        if(s->save_directory.empty()) throw std::runtime_error("chunk saves require a disk save directory; use --save-dir in headless mode");
        auto id=s->project.get("id"),version=s->project.get("data_version");
        if(!id) throw std::runtime_error("saving requires project.id");
        auto path=s->save_directory+"/"+id->text()+"/"+slot+".json";
        const bool missing=s->save_snapshot_slot!=slot&&!std::filesystem::exists(sc_path(path))
            &&!std::filesystem::exists(sc_path(path+".bak"));
        if(!missing) {
            if(s->save_snapshot_slot!=slot) {
                auto record=sc_save_read(path,id->text(),version?version->number():1);
                if(!record) throw std::runtime_error(record.error());
                pin_save_snapshot(s,slot,std::move(*record));
            }
            auto value=sc_save_read_chunk(path,s->save_snapshot,name);
            if(!value) throw std::runtime_error(value.error());
            found=value->has_value();
            if(found) s->scratch=std::move(**value);
        }
        ok=true;
    } catch(const std::exception& error) { error_text(s,error.what()); }
    if(!ok) { lua_pushnil(L); lua_pushstring(L,s->error); return 2; }
    if(found) sc_lua_push(L,s->scratch); else lua_pushnil(L);
    return 1;
}
int save_list(lua_State* L) {
    if(lua_gettop(L)!=0) return luaL_error(L,"list expects no arguments");
    auto* s=script(L);
    idle_writer(L);
    {
        const auto* id=s->project.get("id");
        if(!id) throw std::runtime_error("saving requires project.id");
        const auto* version=s->project.get("data_version"); double v=version?version->number():1;
        std::map<std::string,ScResult<ScValue>,std::less<>> records;
        if(s->save_directory.empty()) for(const auto& [name,value]:s->memory_saves) records.emplace(name,value);
        else {
            auto directory=sc_path(s->save_directory+"/"+id->text());
            if(std::filesystem::exists(directory)) for(const auto& entry:std::filesystem::directory_iterator(directory)) {
                if(!entry.is_regular_file()) continue;
                auto name=entry.path().filename().string();
                if(name.ends_with(".json.bak")) name.resize(name.size()-9);
                else if(name.ends_with(".json")) name.resize(name.size()-5);
                else continue;
                if(name.empty()||name.size()>128||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos) continue;
                if(records.contains(name)) continue;
                if(records.size()>=128) throw std::runtime_error("save listing exceeds 128 slots");
                records.emplace(name,sc_save_read(s->save_directory+"/"+id->text()+"/"+name+".json",id->text(),v));
            }
        }
        ScValue::Array output;
        for(auto& [name,record]:records) {
            ScValue::Object item{{"slot",ScValue{name}},{"valid",ScValue{record.has_value()}}};
            if(record) for(const char* field:{"scene","data_version","frame","saved_at"}) if(auto value=record->get(field)) item.emplace(field,*value);
            if(!record) item.emplace("error",ScValue{record.error()});
            output.push_back(ScValue{std::move(item)});
        }
        s->scratch=ScValue{std::move(output)};
    }
    sc_lua_push(L,s->scratch); return 1;
}
int save_delete(lua_State* L) {
    if(lua_gettop(L)!=1) return luaL_error(L,"delete expects one slot");
    auto* s=script(L);
    if(s->phase!=1||s->checking) return luaL_error(L,"save operations require update(), outside --check");
    const char* slot=save_slot(L,1);
    idle_writer(L);
    {
        const auto* id=s->project.get("id"); if(!id) throw std::runtime_error("saving requires project.id");
        if(s->save_snapshot_slot==slot) { s->save_snapshot=ScValue{}; s->save_snapshot_slot.clear(); }
        if(s->save_directory.empty()) s->memory_saves.erase(slot);
        else {
            auto path=s->save_directory+"/"+id->text()+"/"+slot+".json";
            auto removed=sc_save_delete(path);
            if(!removed) throw std::runtime_error(removed.error());
        }
    }
    lua_pushboolean(L,true); return 1;
}
constexpr ScLuaParameter slot_parameter{"slot","string",true,"1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace."};
constexpr ScLuaParameter slot_parameters[]={slot_parameter};
constexpr ScLuaParameter read_chunk_parameters[]={slot_parameter,
    {"key","string",true,"1..127 bytes using persistent-ID syntax; no empty, leading/trailing slash or . / .. path segment."}};
constexpr ScLuaParameter write_chunks_parameters[]={slot_parameter,
    {"changes","table<string,table<string,ScData>|false>",true,"Plain object mapping persistent-ID chunk keys to state objects or false for deletion; empty object checkpoints without changing chunks."}};
constexpr ScLuaReturn read_results[]={
    {"record","ScSaveRecord|nil","Independent checkpoint summary, or nil on missing/invalid save."},
    {"error","string|nil","Diagnostic on failure; absent on success."}};
constexpr ScLuaReturn chunk_results[]={
    {"state","table<string,ScData>|nil","Independent chunk state; nil for a missing slot/chunk or failure."},
    {"error","string|nil","Diagnostic on failure; absent for a missing slot/chunk."}};
constexpr ScLuaReturn operation_results[]={
    {"ok","boolean|nil","True on success, nil on an expected save/load failure."},
    {"error","string|nil","Diagnostic on failure; absent on success."}};
constexpr ScLuaContract list_contract{.parameters={},.result="ScSaveSlot[]",.phases=ScLuaPhases::read,
    .capacity="128 distinct disk slots; 16 in-memory slots"};
constexpr ScLuaContract read_contract{.parameters=slot_parameters,.result=nullptr,.phases=ScLuaPhases::read,
    .capacity="256 KiB state; native chunk index is not returned",.results=read_results};
constexpr ScLuaContract operation_contract{.parameters=slot_parameters,.result=nullptr,.phases=ScLuaPhases::update,
    .capacity="16 in-memory slots; 4 MiB disk index; 256 KiB state",.results=operation_results};
constexpr ScLuaContract delete_contract{slot_parameters,"boolean",ScLuaPhases::update};
constexpr ScLuaContract read_chunk_contract{.parameters=read_chunk_parameters,.result=nullptr,.phases=ScLuaPhases::mutate,
    .capacity="256 KiB per chunk",.results=chunk_results};
constexpr ScLuaContract write_chunks_contract{.parameters=write_chunks_parameters,.result=nullptr,.phases=ScLuaPhases::update,
    .capacity="256 KiB combined changes; 16384 chunks; 1 GiB world",.results=operation_results};
constexpr ScLuaParameter request_parameters[]={{"request","integer",true,"Live request returned by an asynchronous read or write.",nullptr,1,4503599627370495.0}};
constexpr ScLuaParameter read_chunks_parameters[]={slot_parameter,
    {"keys","string[]",true,"0..1024 unique chunk keys using persistent-ID syntax; an empty array reads only the checkpoint summary."}};
constexpr ScLuaReturn async_results[]={
    {"request","integer|nil","Accepted request ID; acceptance is not disk completion."},
    {"error","string|nil","Preflight/submission error; no request was accepted."}};
constexpr ScLuaContract async_write_contract{.parameters=slot_parameters,.result=nullptr,.phases=ScLuaPhases::update,
    .capacity="One unreleased transaction per application; 1 MiB native encoded payload; 256 KiB state",.results=async_results};
constexpr ScLuaContract async_chunks_contract{.parameters=write_chunks_parameters,.result=nullptr,.phases=ScLuaPhases::update,
    .capacity="One unreleased transaction; 256 KiB combined Lua changes; 1 MiB native encoded payload",.results=async_results};
constexpr ScLuaContract status_contract{request_parameters,"ScSaveStatus",ScLuaPhases::read};
constexpr ScLuaContract result_contract{request_parameters,"ScSaveReadResult",ScLuaPhases::read};
constexpr ScLuaContract async_read_contract{.parameters=read_chunks_parameters,.result=nullptr,.phases=ScLuaPhases::update,
    .capacity="One unreleased transaction; 1024 keys; 1 MiB combined encoded chunks plus 4 MiB internal index",.results=async_results};
constexpr ScLuaContract request_action_contract{request_parameters,"boolean",ScLuaPhases::update_ui};
const ScLuaApi save_api[]={
    {"read_chunks_async",sc_lua_guard<save_read_chunks_async>,"read_chunks_async(slot,keys) -> request|nil,error","Read selected chunks and a checkpoint summary on the application IO worker, using the pinned snapshot when available or selecting a complete valid snapshot. Missing slot/keys are absent data, corruption is a failure. No state or scene mutation. Host gates the next fixed update.",&async_read_contract},
    {"result",sc_lua_guard<save_result>,"result(request) -> result","Copy a successfully completed read result with record summary and requested chunks. Errors for pending, failed, write or expired requests; does not release or modify state.",&result_contract},
    {"write_async",sc_lua_guard<save_write_async>,"write_async(slot) -> request|nil,error","Submit a frozen checkpoint to the application writer; disk required. Host waits before the next fixed update, keeping UI/devices alive. Submission is not success; inspect status then release. No other save operation or scene change while unreleased.",&async_write_contract},
    {"write_chunks_async",sc_lua_guard<save_write_chunks_async>,"write_chunks_async(slot,changes) -> request|nil,error","Asynchronously commit a frozen checkpoint plus chunk changes with the same format and atomicity as write_chunks. Changes are copied at submission; false deletes a chunk. Host gates the next fixed update.",&async_chunks_contract},
    {"status",sc_lua_guard<save_status>,"status(request) -> status","Copy the host-observed request status; never poll worker timing from Lua. Pending until the next fixed boundary; invalid/expired request errors.",&status_contract},
    {"retry",sc_lua_guard<save_request_action<true>>,"retry(request) -> true","Retry an observed failed read/write using its original payload, selected snapshot and request ID. Update/ui_update only; candidate/check/draw forbidden.",&request_action_contract},
    {"release",sc_lua_guard<save_request_action<false>>,"release(request) -> true","Release an observed result and invalidate its ID; never cancel pending IO or undo a commit. Successful reads pin their selected index; writes, failed or missing reads clear it. A released failure permits continuing the old world.",&request_action_contract},
    {"read_chunk",sc_lua_guard<save_read_chunk>,"read_chunk(slot,key) -> state|nil,error","Read one disk chunk from a pinned complete snapshot; absent chunk or missing slot and backup returns nil without error. Invalid existing saves retain diagnostics. Load/init/update only, disabled in check. Keys use persistent-ID syntax.",&read_chunk_contract},
    {"write_chunks",sc_lua_guard<save_write_chunks>,"write_chunks(slot,changes) -> true|nil,error","Atomically save scene, shared state and chunk changes; a key maps to a state object or false to delete. Combined changes bounded to 256 KiB; update only, disk required.",&write_chunks_contract},
    {"list",sc_lua_guard<save_list>,"list() -> slots","Sorted distinct slot metadata, including backup-only slots and invalid record diagnostics. Synchronous; requires project.id. Invalid arguments, directory errors and capacity overflow raise Lua errors.",&list_contract},
    {"read",sc_lua_guard<save_read>,"read(slot) -> record|nil,error","Read current-format checkpoint data and chunk_count without exposing the native chunk index; recover the complete previous valid backup when needed. Synchronous, pins the chosen snapshot for chunk reads; requires project.id.",&read_contract},
    {"delete",sc_lua_guard<save_delete>,"delete(slot) -> true","Delete a slot, backup and owned chunks; update only, disabled in check. Missing slots are harmless; invalid arguments/context and I/O failures raise Lua errors.",&delete_contract},
    {"write",sc_lua_guard<save_write>,"write(slot) -> true|nil,error","Atomic versioned checkpoint of state and scene; update only, disabled in check. Requires project.id. Headless defaults to 16 in-memory slots. Existing chunks survive; disk calls are synchronous.",&operation_contract},
    {"load",sc_lua_guard<save_load>,"load(slot) -> true|nil,error","Validate and request room reconstruction with restored state, using only the current explicit format/data version. Update only, disabled in check; requires project.id. True means transition requested, not committed. Does not restore VM or solver state.",&operation_contract},
    {nullptr,nullptr,nullptr,nullptr}
};

} // namespace
void sc_script_save_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,save_api); lua_setfield(L,-2,"save");
}
void sc_script_save_describe() { sc_api_describe(save_api,"sc.save."); }
ScValue sc_script_save_contracts() {
    auto field=[](const char* name,const char* type,bool required,const char* description) {
        return ScValue{ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{required}},{"readonly",ScValue{true}},{"description",ScValue{std::string(description)}}}};
    };
    auto type=[](ScValue::Array fields,const char* constraint) {
        return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},
            {"constraints",ScValue{ScValue::Array{ScValue{std::string(constraint)}}}}}};
    };
    const auto frame=field("frame","integer",false,"Saved simulation frame, 0..9007199254740991; absent in records written without metadata.");
    const auto timestamp=field("saved_at","integer",false,"Unix seconds, 0..9007199254740991; not deterministic gameplay state. Optional for native-authored records.");
    return ScValue{ScValue::Object{
        {"ScSaveStatus",type({field("request","integer",true,"Live request ID, 1..2^52-1."),
            field("operation","'read'|'write'",true,"Operation accepted by the application IO worker."),
            field("status","'pending'|'complete'|'failed'",true,"Published by the host at a fixed boundary; Lua never observes raw worker timing."),
            field("error","string",false,"Present only on failure; retry preserves the original frozen payload.")},
            "Independent status copy. Release invalidates the request; pending requests cannot be released.")},
        {"ScSaveReadResult",type({field("record","ScSaveRecord",false,"Complete checkpoint summary; absent only when both slot and backup are missing."),
            field("chunks","table<string,table<string,ScData>>",true,"Requested chunks from that same snapshot; missing keys omitted. Combined encoded size at most 1 MiB including key/container allowance.")},
            "Independent data copy, available only after a read completes. Does not restore shared state or the scene. No partial result on failure; release pins the selected index.")},
        {"ScSaveRecord",type({field("format","integer",true,"Current native save format; see --api save.format."),
            field("project","string",true,"Matches project.id."),field("data_version","integer",true,"Must match project.data_version (default 1); no migration."),
            field("scene","string",true,"Project-relative .lua entry to reconstruct."),field("state","table<string,ScData>",true,"Explicit shared state, at most 256 KiB and depth 16."),
            frame,timestamp,field("chunk_count","integer",true,"0..16384; native chunk references are not exposed.")},
            "Independent returned data. Editing it does not change storage, the pinned snapshot or current sc.state.")},
        {"ScSaveSlot",type({field("slot","string",true,"ASCII slot name, 1..128 bytes; list sorts lexicographically."),
            field("valid","boolean",true,"True if either the primary or complete backup validates."),
            field("scene","string",false,"Present only for valid slots."),field("data_version","integer",false,"Present only for valid slots."),
            frame,timestamp,field("error","string",false,"Present only for invalid slots; no partial state is returned.")},
            "Primary and backup share one list entry. A missing primary does not hide a recoverable backup.")}}};
}
