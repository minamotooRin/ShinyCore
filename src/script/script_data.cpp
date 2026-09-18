#include "script_api.h"
#include "shiny/script_data.h"
#include <lua.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "shiny/path.h"
#include "shiny/save.h"
#include "shiny/capabilities.h"
#include "shiny/settings.h"
#include <chrono>
#include <stdexcept>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
ScValue read(lua_State* L,int index,int depth,std::size_t& budget) {
    if (depth>16 || !lua_checkstack(L,4)) throw std::runtime_error("data exceeds nesting depth 16");
    if (budget<16) throw std::runtime_error("data exceeds 256 KiB");
    budget-=16; index=lua_absindex(L,index);
    switch(lua_type(L,index)) {
        case LUA_TNIL: return {};
        case LUA_TBOOLEAN: return ScValue(lua_toboolean(L,index)!=0);
        case LUA_TNUMBER: {
            double number=lua_tonumber(L,index);
            if (!std::isfinite(number)) throw std::runtime_error("data numbers must be finite");
            return ScValue(number);
        }
        case LUA_TSTRING: {
            size_t length=0; const char* bytes=lua_tolstring(L,index,&length);
            if (length>budget) throw std::runtime_error("data exceeds 256 KiB");
            budget-=length; return ScValue(std::string(bytes,length));
        }
        case LUA_TTABLE: break;
        default: throw std::runtime_error("data must contain only plain tables, strings, numbers and booleans");
    }
    if (lua_getmetatable(L,index)) { lua_pop(L,1); throw std::runtime_error("data tables cannot have metatables"); }
    ScValue::Object object; ScValue::Array array;
    size_t length=lua_rawlen(L,index),count=0;
    lua_pushnil(L);
    while(lua_next(L,index)) {
        ++count;
        if (lua_type(L,-2)==LUA_TSTRING && length==0) {
            size_t size=0; const char* key=lua_tolstring(L,-2,&size);
            if (!size || size>128 || std::memchr(key,0,size)) throw std::runtime_error("data keys must be nonempty strings of at most 128 bytes");
            object.emplace(std::string(key,size),read(L,-1,depth+1,budget));
        } else if (lua_isinteger(L,-2) && length>0) {
            auto key=lua_tointeger(L,-2);
            if (key<1 || static_cast<lua_Unsigned>(key)>length) throw std::runtime_error("data arrays must be dense");
        } else throw std::runtime_error("data tables cannot mix object and array keys");
        lua_pop(L,1);
    }
    if (!length) return ScValue(std::move(object));
    if (count!=length || length>SC_STATE_BYTES/16) throw std::runtime_error("data arrays must be dense and bounded");
    for(size_t i=1;i<=length;++i) { lua_rawgeti(L,index,static_cast<lua_Integer>(i)); array.push_back(read(L,-1,depth+1,budget)); lua_pop(L,1); }
    return ScValue(std::move(array));
}
const char* key(lua_State* L,int index) {
    if (lua_type(L,index)!=LUA_TSTRING) luaL_error(L,"expected string key");
    size_t length=0; const char* value=lua_tolstring(L,index,&length);
    if (!length || length>128 || std::memchr(value,0,length)) luaL_error(L,"key must have 1..128 bytes without NUL");
    return value;
}
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"mutation is forbidden in draw() or migration"); }
void error_text(ScScript* s,const char* message) { std::snprintf(s->error,sizeof s->error,"%s",message); }
int state_get(lua_State* L) {
    const char* name=key(L,1); const auto* value=script(L)->state.get(name);
    if (value) sc_lua_push(L,*value); else lua_pushnil(L); return 1;
}
int state_set(lua_State* L) {
    mutable_phase(L); const char* name=key(L,1); auto* s=script(L); bool ok=false;
    try {
        auto value=sc_lua_read(L,2);
        if (!value) error_text(s,value.error().c_str());
        else {
            auto candidate=s->state;
            auto& object=std::get<ScValue::Object>(candidate.data);
            if(lua_isnil(L,2)) object.erase(name); else object.insert_or_assign(name,std::move(*value));
            auto validated=sc_state_validate(candidate);
            if(!validated) error_text(s,validated.error().c_str());
            else { s->state=std::move(*validated); ok=true; }
        }
    } catch(const std::exception& e) { error_text(s,e.what()); }
    if(!ok) return luaL_error(L,"state: %s",s->error);
    return 0;
}
int module_load(lua_State* L) {
    const char* name=key(L,1); char relative[SC_PATH_MAX]{};
    auto length=std::strlen(name);
    for(size_t i=0;i<length;++i) {
        char c=name[i];
        if (!(c=='.' || c=='_' || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9')))
            return luaL_error(L,"module names use letters, digits, underscores and dots");
        relative[i]=c=='.'?'/':c;
    }
    std::strcat(relative,".lua");
    if (!sc_script_validate_path(relative)) return luaL_error(L,"invalid module path");
    lua_getfield(L,LUA_REGISTRYINDEX,"shiny.modules"); lua_getfield(L,-1,name);
    if (!lua_isnil(L,-1)) return 1;
    lua_pop(L,1);
    if(script(L)->phase==2) return luaL_error(L,"first module load is forbidden in draw()");
    lua_getfield(L,LUA_REGISTRYINDEX,"shiny.loading"); lua_getfield(L,-1,name);
    if(lua_toboolean(L,-1)) return luaL_error(L,"circular module dependency: %s",name);
    lua_pop(L,1); lua_pushboolean(L,1); lua_setfield(L,-2,name);
    char path[SC_PATH_MAX*2]; std::snprintf(path,sizeof path,"%s/%s%s",script(L)->root,std::strncmp(name,"shiny.",6)==0?"lib/":"",relative);
    int status=luaL_loadfilex(L,path,"t");
    if(status==LUA_OK) status=lua_pcall(L,0,1,0);
    lua_pushnil(L); lua_setfield(L,-3,name); // clear loading on success and failure
    if(status!=LUA_OK) return lua_error(L);
    if(lua_isnil(L,-1)) { lua_pop(L,1); lua_pushboolean(L,1); }
    lua_pushvalue(L,-1); lua_setfield(L,-4,name);
    if(script(L)->instruction_budget<=0) return luaL_error(L,"instruction budget exceeded");
    return 1;
}
const char* save_slot(lua_State* L,int argument) {
    const char* slot=key(L,argument);
    for(const char* p=slot;*p;++p) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))
        luaL_error(L,"slot uses letters, digits, underscore or hyphen");
    return slot;
}
int save_operation(lua_State* L,bool loading) {
    auto* s=script(L);
    if(s->phase!=1 || s->checking) return luaL_error(L,"save operations require update(), outside --check");
    const char* slot=save_slot(L,1);
    bool ok=false;
    char saved_scene[SC_PATH_MAX]{};
    try {
        const auto* id=s->project.get("id");
        if (!id || id->text().empty()) throw std::runtime_error("saving requires project.id");
        auto version=s->project.get("data_version"); double v=version?version->number(1):1;
        auto path=s->save_directory+"/"+id->text()+"/"+slot+".json";
        if (!loading) {
            ScValue record(ScValue::Object{{"format",ScValue(double(SC_SAVE_FORMAT))},{"project",*id},{"data_version",ScValue(v)},
                {"scene",ScValue(std::string(s->entry))},{"state",s->state},
                {"frame",ScValue{double(s->world->tick)}},
                {"saved_at",ScValue{double(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count())}}});
            if(s->save_directory.empty()) {
                if(!s->memory_saves.contains(slot)&&s->memory_saves.size()>=16) throw std::runtime_error("memory save capacity exhausted (16)");
                s->memory_saves.insert_or_assign(slot,std::move(record));
            }
            else { auto result=sc_save_write(path,record,id->text(),v); if(!result) throw std::runtime_error(result.error()); }
        } else {
            if(s->pending_scene[0]) throw std::runtime_error("a scene transition is already pending");
            ScResult<ScValue> record=std::unexpected("save slot does not exist");
            if(s->save_directory.empty()) { auto it=s->memory_saves.find(slot); if(it!=s->memory_saves.end()) record=it->second; }
            else record=sc_save_read(path,id->text(),v);
            if(!record) throw std::runtime_error(record.error());
            auto valid=sc_save_validate(*record,id->text(),v);
            if(!valid) throw std::runtime_error(valid.error());
            auto scene=record->get("scene"),state=record->get("state");
            auto entry=scene->text();
            if(entry.size()>=SC_PATH_MAX||entry.find('\0')!=std::string::npos||!sc_script_validate_path(entry.c_str())||!entry.ends_with(".lua")) throw std::runtime_error("invalid saved scene");
            auto bounded=sc_state_validate(*state); if(!bounded) throw std::runtime_error(bounded.error());
            s->scratch=std::move(*bounded);
            std::snprintf(saved_scene,sizeof saved_scene,"%s",entry.c_str());
        }
        ok=true;
    } catch(const std::exception& e) { error_text(s,e.what()); }
    if(ok&&loading) {
        s->pending_state=std::move(s->scratch); s->has_pending_state=true;
        std::memcpy(s->pending_scene,saved_scene,sizeof saved_scene);
    }
    if(ok) { lua_pushboolean(L,1); return 1; }
    lua_pushnil(L); lua_pushstring(L,s->error); return 2;
}
int save_write(lua_State* L) { return save_operation(L,false); }
int save_load(lua_State* L) { return save_operation(L,true); }
int save_read(lua_State* L) {
    const char* slot=save_slot(L,1); auto* s=script(L); bool ok=false;
    {
        const auto* id=s->project.get("id");
        if(!id) throw std::runtime_error("saving requires project.id");
        const auto* version=s->project.get("data_version"); double v=version?version->number():1;
        ScResult<ScValue> record=std::unexpected("save slot does not exist");
        if(s->save_directory.empty()) { auto found=s->memory_saves.find(slot); if(found!=s->memory_saves.end()) record=found->second; }
        else record=sc_save_read(s->save_directory+"/"+id->text()+"/"+slot+".json",id->text(),v);
        if(record) { s->scratch=std::move(*record); ok=true; }
        else error_text(s,record.error().c_str());
    }
    if(ok) { sc_lua_push(L,s->scratch); return 1; }
    lua_pushnil(L); lua_pushstring(L,s->error); return 2;
}
int save_list(lua_State* L) {
    auto* s=script(L);
    {
        const auto* id=s->project.get("id");
        if(!id) throw std::runtime_error("saving requires project.id");
        const auto* version=s->project.get("data_version"); double v=version?version->number():1;
        std::map<std::string,ScResult<ScValue>,std::less<>> records;
        if(s->save_directory.empty()) for(const auto& [name,value]:s->memory_saves) records.emplace(name,value);
        else {
            auto directory=sc_path(s->save_directory+"/"+id->text());
            if(std::filesystem::exists(directory)) for(const auto& entry:std::filesystem::directory_iterator(directory)) {
                if(!entry.is_regular_file()||entry.path().extension()!=".json") continue;
                if(records.size()>=128) throw std::runtime_error("save listing exceeds 128 slots");
                auto name=entry.path().stem().string();
                if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos) continue;
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
    auto* s=script(L);
    if(s->phase!=1||s->checking) return luaL_error(L,"save operations require update(), outside --check");
    const char* slot=save_slot(L,1);
    {
        const auto* id=s->project.get("id"); if(!id) throw std::runtime_error("saving requires project.id");
        if(s->save_directory.empty()) s->memory_saves.erase(slot);
        else {
            auto path=s->save_directory+"/"+id->text()+"/"+slot+".json";
            std::filesystem::remove(sc_path(path+".bak")); std::filesystem::remove(sc_path(path));
        }
    }
    lua_pushboolean(L,true); return 1;
}
const ScLuaApi module_api[]={
    {"require",sc_lua_guard<module_load>,"require(module) -> value","Cached project-local module; dots map to directories, max 128 bytes. Cycles error; first load forbidden in draw."},
    {nullptr,nullptr,nullptr,nullptr}
};
const ScLuaApi state_api[]={
    {"get",sc_lua_guard<state_get>,"get(key) -> copy|nil","Read explicit cross-room state by key (1..128 bytes). Returns an independent copy."},
    {"set",sc_lua_guard<state_set>,"set(key,value)","Atomic plain data update; nil deletes. Total 256 KiB, depth 16, finite numbers, dense arrays and UTF-8. Forbidden in draw/migration."},
    {nullptr,nullptr,nullptr,nullptr}
};
const ScLuaApi save_api[]={
    {"list",sc_lua_guard<save_list>,"list() -> slots","Sorted slot metadata; bounded to 128 disk slots, including invalid record diagnostics."},
    {"read",sc_lua_guard<save_read>,"read(slot) -> record|nil,error","Read current-format data without changing rooms; recover from previous valid backup when needed."},
    {"delete",sc_lua_guard<save_delete>,"delete(slot) -> true","Delete a slot and its backup; update only. Missing slots are harmless."},
    {"write",sc_lua_guard<save_write>,"write(slot) -> true|nil,error","Atomic versioned checkpoint of state and scene; update only, disabled in check. Slot uses alnum/_/-. Headless defaults to 16 in-memory slots."},
    {"load",sc_lua_guard<save_load>,"load(slot) -> true|nil,error","Validate and request room reconstruction with restored state, using only the current explicit format version. Does not restore VM or solver state."},
    {nullptr,nullptr,nullptr,nullptr}
};
}
ScResult<ScValue> sc_lua_read(lua_State* L,int index) {
    int top=lua_gettop(L); std::size_t budget=SC_STATE_BYTES;
    try { auto value=read(L,index,0,budget); lua_settop(L,top); return value; }
    catch(const std::exception& e) { lua_settop(L,top); return std::unexpected(e.what()); }
}
void sc_lua_push(lua_State* L,const ScValue& value) {
    luaL_checkstack(L,4,"data nesting");
    if(auto value1=std::get_if<bool>(&value.data)) lua_pushboolean(L,*value1);
    else if(auto value2=std::get_if<double>(&value.data)) lua_pushnumber(L,*value2);
    else if(auto value3=std::get_if<std::string>(&value.data)) lua_pushlstring(L,value3->data(),value3->size());
    else if(auto value4=std::get_if<ScValue::Array>(&value.data)) {
        lua_createtable(L,static_cast<int>(value4->size()),0); lua_Integer i=1;
        for(const auto& item:*value4) { sc_lua_push(L,item); lua_rawseti(L,-2,i++); }
    } else if(auto value5=std::get_if<ScValue::Object>(&value.data)) {
        lua_createtable(L,0,static_cast<int>(value5->size()));
        for(const auto& [name,item]:*value5) { lua_pushlstring(L,name.data(),name.size()); sc_lua_push(L,item); lua_rawset(L,-3); }
    } else lua_pushnil(L);
}
void sc_script_data_register(lua_State* L) {
    lua_newtable(L); lua_setfield(L,LUA_REGISTRYINDEX,"shiny.modules");
    lua_newtable(L); lua_setfield(L,LUA_REGISTRYINDEX,"shiny.loading");
    lua_pushglobaltable(L); sc_api_register(L,module_api); lua_pop(L,1);
    lua_newtable(L); sc_api_register(L,state_api); lua_setfield(L,-2,"state");
    lua_newtable(L); sc_api_register(L,save_api); lua_setfield(L,-2,"save");
}
void sc_script_project_load(lua_State* L) {
    auto* s=script(L); bool exists=false;
    try { exists=std::filesystem::exists(sc_path(s->root)/"project.lua"); }
    catch(const std::exception& e) { error_text(s,e.what()); }
    if(!exists) return;
    lua_getglobal(L,"require"); lua_pushliteral(L,"project"); lua_call(L,1,1);
    bool ok=false;
    try {
        auto project=sc_lua_read(L,-1);
        if(!project) throw std::runtime_error(project.error());
        if(!std::holds_alternative<ScValue::Object>(project->data)) throw std::runtime_error("project must return a data table");
        for(const auto& [name,value]:std::get<ScValue::Object>(project->data)) {
            (void)value;
            if(name!="id"&&name!="entry"&&name!="rooms"&&name!="resources"&&name!="data_version"&&name!="limits"&&name!="modules"&&name!="display") throw std::runtime_error("unknown project field: "+name);
        }
        if(auto id=project->get("id")) {
            auto name=id->text();
            if(name.empty()||name.size()>128||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=std::string::npos||name=="."||name=="..")
                throw std::runtime_error("invalid project.id");
        }
        if(auto version=project->get("data_version")) if(version->number()<1||std::floor(version->number())!=version->number()) throw std::runtime_error("invalid data_version");
        auto entry_path=[](const ScValue& value) {
            auto path=value.text();
            if(path.size()>=SC_PATH_MAX||path.find('\0')!=std::string::npos||!sc_script_validate_path(path.c_str())||!path.ends_with(".lua")) throw std::runtime_error("project entry/rooms require relative .lua paths");
        };
        if(auto entry=project->get("entry")) entry_path(*entry);
        if(auto rooms=project->get("rooms")) {
            const auto* entries=std::get_if<ScValue::Array>(&rooms->data);
            if(!entries||entries->size()>256) throw std::runtime_error("project.rooms requires an array of at most 256 paths");
            for(const auto& entry:*entries) entry_path(entry);
        }
        if(auto modules=project->get("modules")) {
            const auto* required=std::get_if<ScValue::Array>(&modules->data);
            if(!required||required->size()>32) throw std::runtime_error("project.modules requires an array of module names");
            for(const auto& module:*required) {
                const auto name=module.text(); bool found=false;
                for(const auto& capability:SC_CAPABILITIES) if(capability.name==name) {
                    found=true;
                    if(!capability.available) throw std::runtime_error("required module unavailable: "+name);
                }
                if(!found) throw std::runtime_error("unknown required module: "+name);
            }
        }
        if (auto limits=project->get("limits")) {
            const auto* fields=std::get_if<ScValue::Object>(&limits->data);
            if(!fields) throw std::runtime_error("project.limits must be an object");
            for(const auto& [name,value]:*fields) {
                auto n=value.number(-1);
                if(n<0||n>65536||std::floor(n)!=n) throw std::runtime_error("invalid capacity: "+name);
                auto capacity=static_cast<size_t>(n);
                if(name=="entities" && capacity>0) { s->world->entities.resize(capacity); s->world->generations.assign(capacity,1); }
                else if(name=="particles") s->world->particles.resize(capacity);
                else if(name=="draws" && capacity>0) s->world->draws.resize(capacity);
                else if(name=="contacts" && capacity>0) s->world->contacts.resize(capacity);
                else if(name=="sound_voices" && capacity<=32) s->world->sound_voice_limit=capacity;
                else throw std::runtime_error("unknown or zero capacity: "+name);
            }
        }
        if(s->settings) {
            const auto* id=project->get("id");
            auto initialized=s->settings->initialize(project->get("display"),s->save_directory,id?id->text():"");
            if(!initialized) throw std::runtime_error(initialized.error());
            s->world->audio_gains=s->settings->current.volume;
        } else if(auto display=project->get("display")) {
            auto validated=sc_settings_patch(ScSettings{},*display);
            if(!validated) throw std::runtime_error("project.display: "+validated.error());
        }
        s->project=std::move(*project); ok=true;
    } catch(const std::exception& e) { error_text(s,e.what()); }
    lua_pop(L,1);
    if(!ok) luaL_error(L,"project: %s",s->error);
}
void sc_script_data_describe() {
    sc_api_describe(module_api,""); sc_api_describe(state_api,"sc.state."); sc_api_describe(save_api,"sc.save.");
}
