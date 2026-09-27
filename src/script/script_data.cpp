#include "script_api.h"
#include "shiny/script_data.h"
#include <lua.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "shiny/path.h"
#include "script_save.h"
#include "shiny/capabilities.h"
#include "shiny/settings.h"
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
    if(lua_gettop(L)!=1) return luaL_error(L,"invalid state_get argument count");
    const char* name=key(L,1); const auto* value=script(L)->state.get(name);
    if (value) sc_lua_push(L,*value); else lua_pushnil(L); return 1;
}
int state_set(lua_State* L) {
    if(lua_gettop(L)!=2) return luaL_error(L,"invalid state_set argument count");
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
    if(lua_gettop(L)!=1) return luaL_error(L,"invalid module_load argument count");
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
    if(script(L)->phase>=2) return luaL_error(L,"first module load is forbidden in draw(), ui_update(), or migration");
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
constexpr ScLuaParameter module_parameters[]={{"module","string",true,"1..128 bytes: letters, digits, underscore and dots; must resolve to a project-relative Lua path. shiny.* resolves under lib/."}};
constexpr ScLuaContract module_contract{module_parameters,"any",ScLuaPhases::read,"First load only in load/init/update; cached reads also allowed in draw/ui_update"};
constexpr ScLuaParameter state_get_parameters[]={{"key","string",true,"1..128 bytes without NUL."}};
constexpr ScLuaParameter state_set_parameters[]={
    {"key","string",true,"1..128 bytes without NUL."},
    {"value","boolean|number|string|table|nil",true,"Copied plain UTF-8 data; nil deletes. Finite numbers, dense arrays or string-keyed objects; no metatables."}};
constexpr ScLuaContract state_get_contract{state_get_parameters,"boolean|number|string|table|nil",ScLuaPhases::read};
constexpr ScLuaContract state_set_contract{state_set_parameters,nullptr,ScLuaPhases::mutate,"Total state: 256 KiB serialized UTF-8 and depth 16; conversion also bounded"};
const ScLuaApi module_api[]={
    {"require",sc_lua_guard<module_load>,"require(module) -> value","Cached project-local module; nil return becomes true. Cycles fail; failed loads clear their loading marker for retry. Cached reads preserve returned object identity.",&module_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
const ScLuaApi state_api[]={
    {"get",sc_lua_guard<state_get>,"get(key) -> copy|nil","Read explicit cross-room state by key. Returns an independent nested copy, or nil when absent.",&state_get_contract},
    {"set",sc_lua_guard<state_set>,"set(key,value)","Atomically replace a key, or delete it with explicit nil. Failure retains all prior state. Candidate rooms mutate their own copy, published only on room commit. Forbidden in draw/ui_update/migration.",&state_set_contract},
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
    sc_script_save_register(L);
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
            if(name!="id"&&name!="entry"&&name!="rooms"&&name!="resources"&&name!="data_version"&&name!="limits"&&name!="modules"&&name!="display"&&name!="stream_indexes") throw std::runtime_error("unknown project field: "+name);
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
        if(auto indexes=project->get("stream_indexes")) {
#ifndef SC_HAS_STREAMING
            (void)indexes;
            throw std::runtime_error("project.stream_indexes requires the streaming build module");
#else
            const auto* entries=std::get_if<ScValue::Object>(&indexes->data);
            if(!entries||entries->size()>256) throw std::runtime_error("project.stream_indexes requires an object of at most 256 room paths");
            for(const auto& [room,index]:*entries) {
                if(room.size()>=SC_PATH_MAX||!sc_script_validate_path(room.c_str())||!room.ends_with(".lua"))
                    throw std::runtime_error("project.stream_indexes room key requires a relative .lua path: "+room);
                const auto path=index.text();
                if(path.size()>=SC_PATH_MAX||path.find('\0')!=std::string::npos||
                   !sc_script_validate_path(path.c_str())||!path.ends_with(".json"))
                    throw std::runtime_error("project.stream_indexes."+room+" requires a relative .json path");
            }
#endif
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
                else if(name=="identities") {
                    if(s->world->identities && s->world->identities->size()) throw std::runtime_error("configure persistent ID capacity before declaring or spawning named objects");
                    s->world->identities.reset(capacity?new ScIdentities(capacity):nullptr);
                }
                else if(name=="projectiles") {
                    if(s->world->projectiles) throw std::runtime_error("configure projectile capacity before allocating the pool");
                    s->projectile_limit=capacity;
                }
                else if(name=="particles") s->world->particles.configure(capacity);
                else if(name=="draws" && capacity>0) s->world->draws.resize(capacity);
                else if(name=="contacts" && capacity>0) s->world->contacts.resize(capacity);
                else if(name=="sound_voices" && capacity<=32) s->audio->sound_voice_limit=capacity;
                else throw std::runtime_error("unknown or zero capacity: "+name);
            }
        }
        if(s->settings) {
            const auto* id=project->get("id");
            auto initialized=s->settings->initialize(project->get("display"),s->save_directory,id?id->text():"");
            if(!initialized) throw std::runtime_error(initialized.error());
            s->audio->audio_gains=s->settings->current.volume;
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
    sc_api_describe(module_api,""); sc_api_describe(state_api,"sc.state."); sc_script_save_describe();
}
