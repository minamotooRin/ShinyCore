#include "script_api.h"
#include "shiny/script_data.h"
#include <lua.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "shiny/path.h"
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
    char path[SC_PATH_MAX*2]; std::snprintf(path,sizeof path,"%s/%s",script(L)->root,relative);
    int status=luaL_loadfilex(L,path,"t");
    if(status==LUA_OK) status=lua_pcall(L,0,1,0);
    lua_pushnil(L); lua_setfield(L,-3,name); // clear loading on success and failure
    if(status!=LUA_OK) return lua_error(L);
    if(lua_isnil(L,-1)) { lua_pop(L,1); lua_pushboolean(L,1); }
    lua_pushvalue(L,-1); lua_setfield(L,-4,name);
    if(script(L)->instruction_budget<=0) return luaL_error(L,"instruction budget exceeded");
    return 1;
}
// The outer pcall also protects argument construction and module loading from OOM.
int migrate_state(lua_State* L) {
    auto* s=script(L);
    lua_getglobal(L,"require"); sc_lua_push(L,*s->project.get("migrate")); lua_call(L,1,1);
    if(!lua_isfunction(L,-1)) return luaL_error(L,"project.migrate must name a module returning a function");
    lua_pushvalue(L,1); lua_pushvalue(L,2); sc_lua_push(L,s->scratch); lua_call(L,3,1);
    bool ok=false;
    {
        auto converted=sc_lua_read(L,-1);
        if(converted) converted=sc_state_validate(*converted);
        if(converted) { s->scratch=std::move(*converted); ok=true; }
        else error_text(s,"migration must return a bounded state object");
    }
    if(!ok) return luaL_error(L,"%s",s->error);
    return 0;
}
int save_operation(lua_State* L,bool loading) {
    auto* s=script(L);
    if(s->phase!=1 || s->checking) return luaL_error(L,"save operations require update(), outside --check");
    const char* slot=key(L,1);
    for(const char* p=slot;*p;++p) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))
        return luaL_error(L,"slot uses letters, digits, underscore or hyphen");
    bool ok=false,needs_migration=false;
    char saved_scene[SC_PATH_MAX]{};
    double old_version=0,new_version=0;
    try {
        const auto* id=s->project.get("id");
        if (!id || id->text().empty()) throw std::runtime_error("saving requires project.id");
        auto version=s->project.get("data_version"); double v=version?version->number(1):1;
        auto path=s->save_directory+"/"+id->text()+"/"+slot+".json";
        if (!loading) {
            ScValue record(ScValue::Object{{"format",ScValue(1.0)},{"project",*id},{"data_version",ScValue(v)},
                {"scene",ScValue(std::string(s->entry))},{"state",s->state}});
            if(s->save_directory.empty()) {
                if(!s->memory_saves.contains(slot)&&s->memory_saves.size()>=16) throw std::runtime_error("memory save capacity exhausted (16)");
                s->memory_saves.insert_or_assign(slot,std::move(record));
            }
            else { auto result=sc_atomic_write(path,sc_json_write(record)); if(!result) throw std::runtime_error(result.error()); }
        } else {
            if(s->pending_scene[0]) throw std::runtime_error("a scene transition is already pending");
            ScResult<ScValue> record=std::unexpected("save slot does not exist");
            if(s->save_directory.empty()) { auto it=s->memory_saves.find(slot); if(it!=s->memory_saves.end()) record=it->second; }
            else record=sc_json_file(path,SC_STATE_BYTES+4096,17); // checkpoint envelope adds one level
            if(!record) throw std::runtime_error(record.error());
            auto format=record->get("format"), project=record->get("project"), data_version=record->get("data_version"), scene=record->get("scene"), state=record->get("state");
            if(!format||format->number()!=1||!project||project->text()!=id->text()||!data_version||!scene||!state||!std::holds_alternative<ScValue::Object>(state->data))
                throw std::runtime_error("invalid save format or project");
            if(data_version->number()<1||std::floor(data_version->number())!=data_version->number()) throw std::runtime_error("invalid save data version");
            if(data_version->number()!=v) {
                auto migrate=s->project.get("migrate");
                if(!migrate||migrate->text().empty()||data_version->number()>v) throw std::runtime_error("save data version mismatch");
                old_version=data_version->number(); new_version=v; needs_migration=true;
            }
            auto entry=scene->text();
            if(entry.size()>=SC_PATH_MAX||entry.find('\0')!=std::string::npos||!sc_script_validate_path(entry.c_str())||!entry.ends_with(".lua")) throw std::runtime_error("invalid saved scene");
            auto bounded=sc_state_validate(*state); if(!bounded) throw std::runtime_error(bounded.error());
            s->scratch=std::move(*bounded);
            std::snprintf(saved_scene,sizeof saved_scene,"%s",entry.c_str());
        }
        ok=true;
    } catch(const std::exception& e) { error_text(s,e.what()); }
    if(ok&&needs_migration) {
        lua_pushcfunction(L,sc_lua_guard<migrate_state>);
        lua_pushnumber(L,old_version); lua_pushnumber(L,new_version);
        int old_phase=s->phase; s->phase=3;
        int status=lua_pcall(L,2,0,0); s->phase=old_phase;
        if(status!=LUA_OK) { lua_pushnil(L); lua_insert(L,-2); return 2; }
    }
    if(ok&&loading) {
        s->pending_state=std::move(s->scratch); s->has_pending_state=true;
        std::memcpy(s->pending_scene,saved_scene,sizeof saved_scene);
    }
    if(ok) { lua_pushboolean(L,1); return 1; }
    lua_pushnil(L); lua_pushstring(L,s->error); return 2;
}
int save_write(lua_State* L) { return save_operation(L,false); }
int save_load(lua_State* L) { return save_operation(L,true); }
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
    {"write",sc_lua_guard<save_write>,"write(slot) -> true|nil,error","Atomic versioned checkpoint of state and scene; update only, disabled in check. Slot uses alnum/_/-. Headless defaults to 16 in-memory slots."},
    {"load",sc_lua_guard<save_load>,"load(slot) -> true|nil,error","Validate and request room reconstruction with restored state, optionally via project.migrate. Does not restore VM or solver state."},
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
            if(name!="id"&&name!="entry"&&name!="rooms"&&name!="resources"&&name!="data_version"&&name!="migrate") throw std::runtime_error("unknown project field: "+name);
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
        if(auto migrate=project->get("migrate")) {
            auto name=migrate->text();
            if(name.empty()||name.size()>128||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._")!=std::string::npos) throw std::runtime_error("project.migrate requires a module name");
        }
        s->project=std::move(*project); ok=true;
    } catch(const std::exception& e) { error_text(s,e.what()); }
    lua_pop(L,1);
    if(!ok) luaL_error(L,"project: %s",s->error);
}
void sc_script_data_describe() {
    sc_api_describe(module_api,""); sc_api_describe(state_api,"sc.state."); sc_api_describe(save_api,"sc.save.");
}
