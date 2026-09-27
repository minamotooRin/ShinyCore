#include "script_identity.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/identity.h"
#include <cstring>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void count(lua_State* L) { if(lua_gettop(L)!=1) luaL_error(L,"expected one argument"); }
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"identity mutation forbidden in draw"); }
const char* name_at(lua_State* L,int index) {
    if(lua_type(L,index)!=LUA_TSTRING) luaL_error(L,"persistent_id must be a string");
    std::size_t length=0; const char* name=lua_tolstring(L,index,&length);
    if(!sc_identity_name_valid({name,length})) luaL_error(L,"persistent_id requires 1..127 ASCII name bytes without empty or dot path segments");
    return name;
}
ScEntity* entity_at(lua_State* L) {
    if(lua_type(L,1)!=LUA_TNUMBER || !lua_isinteger(L,1)) luaL_error(L,"entity ID must be an integer");
    auto id=lua_tointeger(L,1);
    auto* entity=id>0?sc_entity(script(L)->world,static_cast<ScEntityId>(id)):nullptr;
    if(!entity) luaL_error(L,"entity ID is stale or unknown");
    if(!entity->persistent_id[0]) luaL_error(L,"entity has no persistent_id");
    return entity;
}
int declare(lua_State* L) {
    count(L); mutable_phase(L); auto* s=script(L); const char* name=name_at(L,1);
    if(!s->world->identities) return luaL_error(L,"persistent object registry is disabled");
    const char* error=nullptr;
    { auto result=s->world->identities->declare(name); if(!result) error=result.error(); }
    if(error) return luaL_error(L,"%s (usage %I/%I)",error,static_cast<lua_Integer>(s->world->identities->size()),static_cast<lua_Integer>(s->world->identities->capacity()));
    lua_pushboolean(L,true); return 1;
}
int resolve(lua_State* L) {
    count(L); auto* s=script(L); const char* name=nullptr; bool other_room=false;
    if(lua_istable(L,1)) {
        if(lua_getmetatable(L,1)) return luaL_error(L,"persistent object reference must be a plain table");
        lua_pushnil(L);
        while(lua_next(L,1)) {
            if(lua_type(L,-2)!=LUA_TSTRING) return luaL_error(L,"persistent object reference keys must be strings");
            std::size_t length=0; const char* key=lua_tolstring(L,-2,&length);
            if((length!=4||std::memcmp(key,"room",4))&&(length!=13||std::memcmp(key,"persistent_id",13)))
                return luaL_error(L,"unknown persistent object reference field");
            lua_pop(L,1);
        }
        lua_getfield(L,1,"room");
        if(lua_type(L,-1)!=LUA_TSTRING) return luaL_error(L,"persistent object reference room must be a string");
        std::size_t length=0; const char* room=lua_tolstring(L,-1,&length);
        if(std::memchr(room,0,length)||!sc_script_validate_path(room)||length<4||std::strcmp(room+length-4,".lua"))
            return luaL_error(L,"persistent object reference room requires a relative .lua path");
        other_room=std::strcmp(room,s->entry)!=0; lua_pop(L,1);
        lua_getfield(L,1,"persistent_id"); name=name_at(L,-1);
    } else name=name_at(L,1);
    const auto* record=!other_room&&s->world->identities?s->world->identities->find(name):nullptr;
    lua_createtable(L,0,2);
    lua_pushstring(L,other_room?"room_inactive":sc_identity_status_name(record?record->status:ScIdentityStatus::absent));
    lua_setfield(L,-2,"status");
    if(record&&record->status==ScIdentityStatus::active) {
        lua_pushinteger(L,static_cast<lua_Integer>(record->entity)); lua_setfield(L,-2,"id");
    }
    return 1;
}
int reference(lua_State* L) {
    count(L); const auto* entity=entity_at(L);
    lua_createtable(L,0,2);
    lua_pushstring(L,script(L)->entry); lua_setfield(L,-2,"room");
    lua_pushstring(L,entity->persistent_id); lua_setfield(L,-2,"persistent_id");
    return 1;
}
int unload(lua_State* L) {
    count(L); mutable_phase(L); auto* entity=entity_at(L);
    lua_pushboolean(L,sc_destroy(script(L)->world,entity->id,false)); return 1;
}
int remove(lua_State* L) {
    count(L); mutable_phase(L); auto* s=script(L); const char* name=name_at(L,1);
    if(!s->world->identities) return luaL_error(L,"persistent object registry is disabled");
    if(const auto* record=s->world->identities->find(name);record&&record->status==ScIdentityStatus::active) {
        lua_pushboolean(L,sc_destroy(s->world,record->entity)); return 1;
    }
    const char* error=nullptr;
    { auto result=s->world->identities->erase(name); if(!result) error=result.error(); }
    if(error) return luaL_error(L,"%s",error);
    lua_pushboolean(L,true); return 1;
}
constexpr ScLuaParameter name_parameters[]={{"persistent_id","string"}}, target_parameters[]={{"reference","string|ScIdentityReference"}}, id_parameters[]={{"id","ScEntityId"}};
constexpr ScLuaContract declare_contract{name_parameters,"boolean",ScLuaPhases::mutate,"project.limits.identities"};
constexpr ScLuaContract resolve_contract{target_parameters,"ScIdentityResolution",ScLuaPhases::read};
constexpr ScLuaContract reference_contract{id_parameters,"ScIdentityReference",ScLuaPhases::read};
constexpr ScLuaContract unload_contract{id_parameters,"boolean",ScLuaPhases::mutate};
const ScLuaApi api[]={
    {"declare",sc_lua_guard<declare>,"declare(persistent_id) -> true","Register an unloaded persistent object ID; existing active/deleted names retain their status.",&declare_contract},
    {"resolve",sc_lua_guard<resolve>,"resolve(name|reference) -> resolution","Resolve a persistent object in this room; other rooms report room_inactive without guessing object existence.",&resolve_contract},
    {"reference",sc_lua_guard<reference>,"reference(id) -> reference","Copy room path and persistent object ID; rejects unnamed or stale entities.",&reference_contract},
    {"unload",sc_lua_guard<unload>,"unload(id) -> true","Release a persistent object and invalidate its handle while retaining unloaded status.",&unload_contract},
    {"remove",sc_lua_guard<remove>,"remove(persistent_id) -> true","Mark a persistent object deleted, releasing its entity if active; explicit spawn with the same name restores it.",&declare_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_identity_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"identity");
}
void sc_script_identity_describe() { sc_api_describe(api,"sc.identity."); }
ScValue sc_script_identity_contracts() {
    auto field=[](const char* name,const char* type,bool required,const char* description) {
        return ScValue{ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{required}},{"description",ScValue{std::string(description)}}}};
    };
    return ScValue{ScValue::Object{
        {"ScIdentityReference",ScValue{ScValue::Object{{"fields",ScValue{ScValue::Array{
            field("room","string",true,"Project-relative .lua room entry path."),
            field("persistent_id","string",true,"Nonempty immutable persistent object ID within that room.")}}},
            {"constraints",ScValue{ScValue::Array{ScValue{std::string("Plain persistent data containing no runtime handle. Unknown fields are rejected.")}}}}}}},
        {"ScIdentityResolution",ScValue{ScValue::Object{{"fields",ScValue{ScValue::Array{
            field("status","'absent'|'unloaded'|'active'|'deleted'|'room_inactive'",true,"Current room registry state; another room's object existence is not inferred."),
            field("id","ScEntityId",false,"Present only for an active entity in the current room. Never persist this handle.")}}},
            {"constraints",ScValue{ScValue::Array{ScValue{std::string("Unloaded/deleted records last for the room lifetime; explicit persistent state must restore them after rebuilding the room.")}}}}}}}
    }};
}
