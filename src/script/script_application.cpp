#include "script_application.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/settings.h"
#include <stdexcept>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void arguments(lua_State* L,int minimum,int maximum) {
    if(lua_gettop(L)<minimum||lua_gettop(L)>maximum) luaL_error(L,"invalid application argument count");
}
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"operation requires load/init/update or ui_update"); }
void read(lua_State* L,int index) {
    auto value=sc_lua_read(L,index); if(!value) throw std::invalid_argument(value.error()); script(L)->scratch=std::move(*value);
}
constexpr ScLuaParameter pause_parameters[]={{"paused","boolean",true,"Exact boolean; room-local, initially false."}};
constexpr ScLuaContract pause_contract{pause_parameters,nullptr,ScLuaPhases::ui_mutate};
constexpr ScLuaContract paused_contract{{},"boolean",ScLuaPhases::read};
constexpr ScLuaContract quit_contract{{},nullptr,ScLuaPhases::ui_mutate};
const ScValue persist_default{true};
const ScLuaParameter settings_apply_parameters[]={
    {"patch","ScSettingsPatch",true,"Partial plain table. Missing fields retain current values; volume merges by bus, bindings replaces the whole object."},
    {"persist","boolean|nil",false,"Omitted/nil means true. Without project.id/save root, changes remain in memory.",&persist_default}};
constexpr ScLuaContract settings_get_contract{{},"ScSettings",ScLuaPhases::read};
const ScLuaContract settings_apply_contract{settings_apply_parameters,"boolean|nil",ScLuaPhases::update,"bindings: 16 KiB serialized plain object; all input: 256 KiB, depth 16","string|nil"};
constexpr ScLuaContract settings_error_contract{{},"string",ScLuaPhases::read};
int pause(lua_State* L) { arguments(L,1,1); if(script(L)->phase!=4) mutable_phase(L); luaL_checktype(L,1,LUA_TBOOLEAN); script(L)->world->simulation_paused=lua_toboolean(L,1)!=0; return 0; }
int paused(lua_State* L) { arguments(L,0,0); lua_pushboolean(L,script(L)->world->simulation_paused); return 1; }
int quit(lua_State* L) { arguments(L,0,0); if(script(L)->phase!=4) mutable_phase(L); script(L)->world->exit_requested=true; return 0; }
const ScLuaApi app_api[]={
    {"pause",sc_lua_guard<pause>,"pause(boolean)","Pause native entity motion, physics, projectiles and particles. Lua update/ui_update, tick, camera and application services continue; scripts must gate their own gameplay.",&pause_contract},
    {"paused",sc_lua_guard<paused>,"paused() -> boolean","Read the current room simulation pause flag, initially false.",&paused_contract},
    {"quit",sc_lua_guard<quit>,"quit()","Request orderly application shutdown when this room is active; candidate requests remain room-local until commit.",&quit_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
int settings_get(lua_State* L) {
    arguments(L,0,0);
    auto* s=script(L);
    if(!s->settings) return luaL_error(L,"settings service unavailable in this host");
    s->scratch=sc_settings_value(s->settings->current);
    sc_lua_push(L,s->scratch); return 1;
}
int settings_apply(lua_State* L) {
    arguments(L,1,2);
    auto* s=script(L);
    if(s->phase!=1||s->checking) return luaL_error(L,"settings.apply requires update outside check mode");
    if(!s->settings) return luaL_error(L,"settings service unavailable in this host");
    bool persist=true;
    if(!lua_isnoneornil(L,2)) { luaL_checktype(L,2,LUA_TBOOLEAN); persist=lua_toboolean(L,2)!=0; }
    read(L,1); bool ok=false;
    { auto result=s->settings->apply(s->scratch,persist); ok=result.has_value(); }
    if(!ok) { lua_pushnil(L); lua_pushstring(L,s->settings->last_error.c_str()); return 2; }
    s->audio->audio_gains=s->settings->current.volume;
    lua_pushboolean(L,true); return 1;
}
int settings_error(lua_State* L) {
    arguments(L,0,0);
    auto* s=script(L);
    if(!s->settings) return luaL_error(L,"settings service unavailable in this host");
    lua_pushstring(L,s->settings->last_error.c_str()); return 1;
}
const ScLuaApi settings_api[]={
    {"get",sc_lua_guard<settings_get>,"get() -> settings","Copy current application settings, including nested volume and bindings. Omitted patch fields retain these values.",&settings_get_contract},
    {"apply",sc_lua_guard<settings_apply>,"apply(patch,persist?) -> true|nil,error","Apply a settings patch, optionally persist it. Semantic/device/I/O failure returns nil,error and retains logical settings; success returns true and clears error. Bad arity/types, unconvertible data or forbidden phase raise. Update outside check mode only.",&settings_apply_contract},
    {"error",sc_lua_guard<settings_error>,"error() -> string","Read last service load/application error; empty when none. Rejected Lua call boundaries do not replace it.",&settings_error_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_application_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,app_api); lua_setfield(L,-2,"app");
    lua_newtable(L); sc_api_register(L,settings_api); lua_setfield(L,-2,"settings");
}
void sc_script_application_describe() { sc_api_describe(app_api,"sc.app."); sc_api_describe(settings_api,"sc.settings."); }
ScValue sc_script_application_contracts() {
    using V=ScValue; V::Object types;
    for(bool patch:{false,true}) {
        V::Array fields,volumes;
        auto field=[&](const char* name,const char* type,const char* description) -> V::Object& {
            fields.emplace_back(V::Object{{"name",V{std::string(name)}},{"type",V{std::string(type)}},
                {"required",V{!patch}},{"readonly",V{!patch}},{"description",V{std::string(description)}}});
            return std::get<V::Object>(fields.back().data);
        };
        auto& width=field("width","integer","Window pixel width; initial engine default 1152. Project/persisted settings may override.");
        width.emplace("minimum",V{320.0});width.emplace("maximum",V{7680.0});
        auto& height=field("height","integer","Window pixel height; initial engine default 648.");
        height.emplace("minimum",V{180.0});height.emplace("maximum",V{4320.0});
        field("mode","'windowed'|'borderless'","Initial engine mode is windowed.");
        field("scale","'integer'|'smooth'","Initial engine scale is integer.");
        field("vsync","boolean","Initial engine value is true.");
        field("volume",patch?"ScVolumePatch":"ScVolume","Four logical gains; patch merges named buses, omitted buses retain their gain.");
        auto& bindings=field("bindings","table","Plain string-keyed object. Patch replaces the entire object; initial value is empty. Gameplay modules own binding semantics.");
        bindings.emplace("maximum_bytes",V{16384.0});
        for(const char* name:{"master","music","sfx","ui"})
            volumes.emplace_back(V::Object{{"name",V{std::string(name)}},{"type",V{std::string("number")}},
                {"required",V{!patch}},{"readonly",V{!patch}},{"finite",V{true}},
                {"minimum",V{0.0}},{"maximum",V{1.0}},
                {"description",V{std::string("Initial engine gain is 1; omitted patch value retains current gain.")}}});
        auto record=[&](V::Array values) {
            V::Array constraints;
            constraints.emplace_back(std::string(patch?
                "Missing fields retain current values; nil keys are absent in Lua tables. Exact types; no metatables.":
                "Independent copied snapshot, including nested data. Modifying the copy does not apply settings."));
            return V{V::Object{{"fields",V{std::move(values)}},{"unknown_fields",V{std::string("reject")}},
                {"constraints",V{std::move(constraints)}}}};
        };
        types.emplace(patch?"ScSettingsPatch":"ScSettings",record(std::move(fields)));
        types.emplace(patch?"ScVolumePatch":"ScVolume",record(std::move(volumes)));
    }
    return V{std::move(types)};
}
