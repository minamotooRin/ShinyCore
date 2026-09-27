#include "script_presentation.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/attachment.h"
#include <cmath>
#include <string_view>

namespace {
ScScript& script(lua_State* L) { return **static_cast<ScScript**>(lua_getextraspace(L)); }
void arguments(lua_State* L,int count) { if(lua_gettop(L)!=count) luaL_error(L,"invalid presentation argument count"); }
void writable(lua_State* L) { if(script(L).phase>=2) luaL_error(L,"presentation mutation requires load/init/update"); }
const ScEntity& entity(lua_State* L,int index=1) {
    if(!lua_isinteger(L,index)) luaL_error(L,"presentation requires a live integer entity handle");
    const auto id=lua_tointeger(L,index);
    auto* value=id>0&&id<=static_cast<lua_Integer>(SC_ID_MAX)?sc_entity(script(L).world,static_cast<ScEntityId>(id)):nullptr;
    if(!value) luaL_error(L,"presentation requires a live entity handle");
    return *value;
}
int interpolate(lua_State* L) {
    arguments(L,1); writable(L);
    if(!lua_isboolean(L,1)) return luaL_error(L,"interpolate requires boolean");
    auto& s=script(L); const bool enabled=lua_toboolean(L,1)!=0;
    if(enabled&&!s.world->presentation&&s.phase!=0) return luaL_error(L,"reserve interpolation during load/init before enabling it in update");
    sc_presentation_configure(*s.world,enabled); return 0;
}
int pose(lua_State* L) {
    arguments(L,1); const auto& e=entity(L); auto& s=script(L);
    const auto p=sc_display_pose(*s.world,e,s.phase==2?s.draw_alpha:1);
    lua_createtable(L,0,5);
    lua_pushnumber(L,p.x); lua_setfield(L,-2,"x"); lua_pushnumber(L,p.y); lua_setfield(L,-2,"y");
    lua_pushnumber(L,p.angle); lua_setfield(L,-2,"angle");
    lua_pushnumber(L,e.w); lua_setfield(L,-2,"w"); lua_pushnumber(L,e.h); lua_setfield(L,-2,"h"); return 1;
}
int snap(lua_State* L) {
    arguments(L,1); writable(L); const auto id=entity(L).id;
    sc_presentation_snap(*script(L).world,id); return 0;
}
int snap_camera(lua_State* L) { arguments(L,0); writable(L); sc_presentation_snap_camera(*script(L).world); return 0; }
int attach(lua_State* L) {
    arguments(L,3); writable(L);
    const auto child=entity(L).id,parent=entity(L,2).id;
    if(!lua_istable(L,3)||lua_getmetatable(L,3)) return luaL_error(L,"attachment offset requires a plain table");
    lua_pushnil(L);
    while(lua_next(L,3)) {
        const auto key=lua_type(L,-2)==LUA_TSTRING?std::string_view{lua_tostring(L,-2),lua_rawlen(L,-2)}:std::string_view{};
        if(key!="x"&&key!="y"&&key!="angle")
            return luaL_error(L,"attachment offset only accepts x, y and angle");
        lua_pop(L,1);
    }
    ScPose local;
    for(const auto& field:{std::pair{"x",&local.x},std::pair{"y",&local.y},std::pair{"angle",&local.angle}}) {
        lua_getfield(L,3,field.first);
        if(!lua_isnil(L,-1)) {
            if(lua_type(L,-1)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,-1))||std::abs(lua_tonumber(L,-1))>1e6)
                return luaL_error(L,"attachment %s must be finite within +/-1000000",field.first);
            *field.second=static_cast<float>(lua_tonumber(L,-1));
        }
        lua_pop(L,1);
    }
    const char* error=nullptr;
    { auto result=sc_attach(*script(L).world,child,parent,local); if(!result) error=result.error(); }
    if(error) return luaL_error(L,"attach: %s",error);
    return 0;
}
int detach(lua_State* L) {
    arguments(L,1); writable(L); const auto id=entity(L).id;
    const char* error=nullptr;
    { auto result=sc_detach(*script(L).world,id); if(!result) error=result.error(); }
    if(error) return luaL_error(L,"detach: %s",error);
    return 0;
}
int attachment(lua_State* L) {
    arguments(L,1); const auto& e=entity(L);
    if(!e.parent) { lua_pushnil(L); return 1; }
    lua_createtable(L,0,4); lua_pushinteger(L,static_cast<lua_Integer>(e.parent)); lua_setfield(L,-2,"parent");
    lua_pushnumber(L,e.local_pose.x); lua_setfield(L,-2,"x");
    lua_pushnumber(L,e.local_pose.y); lua_setfield(L,-2,"y");
    lua_pushnumber(L,e.local_pose.angle); lua_setfield(L,-2,"angle"); return 1;
}
int stats(lua_State* L) {
    arguments(L,0); auto& s=script(L);
    {
        const auto& w=*s.world; const auto* history=w.presentation.get(); using V=ScValue;
        const auto entities=history?history->entities.size():0;
        const auto particles=w.particles.previous_display.size();
        const auto projectiles=w.projectiles?w.projectiles->previous_display.size():0;
        const auto bytes=entities*sizeof(ScPreviousEntity)+(particles+projectiles)*sizeof(ScDisplayPoint)+(history?sizeof(ScPresentation):0);
        s.scratch=V{V::Object{{"enabled",V{history&&history->enabled}},{"reserved",V{history!=nullptr}},
            {"entities",V{double(entities)}},{"particles",V{double(particles)}},{"projectiles",V{double(projectiles)}},
            {"bytes",V{double(bytes)}},{"ready",V{sc_presentation_ready(w)}}}};
    }
    sc_lua_push(L,s.scratch); return 1;
}
constexpr ScLuaParameter toggle_args[]={{"enabled","boolean"}},id_args[]={{"id","ScEntityId"}};
constexpr ScLuaParameter attach_args[]={{"child","ScEntityId"},{"parent","ScEntityId"},{"offset","ScAttachmentOffset"}};
constexpr ScLuaContract attach_contract{attach_args,nullptr,ScLuaPhases::mutate,"At most 32 parent edges; bodyless child with zero velocity; no allocation."},
    attachment_contract{id_args,"{parent:ScEntityId,x:number,y:number,angle:number}|nil",ScLuaPhases::read};
constexpr ScLuaContract toggle_contract{toggle_args,nullptr,ScLuaPhases::mutate,"Startup entity/particle/projectile capacities; enabling for the first time requires load/init."},
    pose_contract{id_args,"ScDisplayPose",ScLuaPhases::read},snap_contract{id_args,nullptr,ScLuaPhases::mutate},
    camera_contract{{},nullptr,ScLuaPhases::mutate},stats_contract{{},"{enabled:boolean,reserved:boolean,ready:boolean,entities:integer,particles:integer,projectiles:integer,bytes:integer}",ScLuaPhases::read};
const ScLuaApi api[]={
    {"attach",sc_lua_guard<attach>,"attach(child,parent,offset)","Set visual parent and local top-left/angle offset. Atomically reject stale handles, bodies, velocity, cycles, depth or invalid pose. Synchronize after physics; preserve child dimensions/flip/layer. Destroying a parent detaches direct children at their current world poses.",&attach_contract},
    {"detach",sc_lua_guard<detach>,"detach(id)","Remove a visual parent while preserving fixed world pose; an already detached live entity is unchanged.",&snap_contract},
    {"attachment",sc_lua_guard<attachment>,"attachment(id) -> relation|nil","Copy runtime parent handle and local offset; nil for roots. Not a save format. sc.get continues to return fixed world coordinates.",&attachment_contract},
    {"interpolate",sc_lua_guard<interpolate>,"interpolate(enabled)","Default false. Reserve fixed history during load/init; update may toggle already-reserved interpolation. Storage remains reserved when disabled.",&toggle_contract},
    {"pose",sc_lua_guard<pose>,"pose(id) -> pose","Read display position/shortest-arc angle in draw, fixed pose elsewhere. Dimensions use current geometry. sc.get always remains fixed state.",&pose_contract},
    {"snap",sc_lua_guard<snap>,"snap(id)","Skip interpolation for this live entity during the current tick, e.g. after a teleport. Does not change simulation position or follow camera.",&snap_contract},
    {"snap_camera",sc_lua_guard<snap_camera>,"snap_camera()","Skip camera interpolation for this tick, e.g. after a camera cut. Does not change camera settings.",&camera_contract},
    {"stats",sc_lua_guard<stats>,"stats() -> stats","Read history capacities and payload bytes, excluding allocator overhead. No trace or state hash is required.",&stats_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_presentation_register(lua_State* L) { lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"presentation"); }
void sc_script_presentation_describe() { sc_api_describe(api,"sc.presentation."); }
ScValue sc_script_presentation_contracts() {
    using V=ScValue; V::Array fields;
    for(const char* name:{"x","y","angle","w","h"}) fields.emplace_back(V::Object{
        {"name",V{std::string(name)}},{"type",V{std::string("number")}},{"required",V{true}},
        {"description",V{std::string("Read-only display geometry; pixels, angle in radians. Position/angle interpolate only in draw.")}}});
    V::Array offsets;
    for(const char* name:{"x","y","angle"}) offsets.emplace_back(V::Object{
        {"name",V{std::string(name)}},{"type",V{std::string("number")}},{"required",V{false}},
        {"default",V{0.}},{"minimum",V{-1e6}},{"maximum",V{1e6}},{"finite",V{true}},
        {"description",V{std::string("Finite local offset; x/y from parent's unrotated top-left, angle in radians. Dimensions/flip are not inherited.")}}});
    V::Object types;
    types.emplace("ScDisplayPose",V::Object{{"fields",V{std::move(fields)}},{"constraints",V{V::Array{}}}});
    V::Array constraints{V{std::string("Plain table; only x, y, angle. Missing fields reset to zero; this is not a partial patch.")}};
    types.emplace("ScAttachmentOffset",V::Object{{"fields",V{std::move(offsets)}},{"constraints",V{std::move(constraints)}}});
    return V{std::move(types)};
}
