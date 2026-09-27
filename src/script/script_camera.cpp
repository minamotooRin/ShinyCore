#include "script_camera.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {
ScScript& script(lua_State* L) { return **static_cast<ScScript**>(lua_getextraspace(L)); }
void arguments(lua_State* L,int minimum,int maximum) {
    if(lua_gettop(L)<minimum||lua_gettop(L)>maximum) luaL_error(L,"invalid camera argument count");
}
void writable(lua_State* L) {
    if(script(L).phase>=2) luaL_error(L,"camera mutation requires load/init/update");
}
double number(const ScValue& value,double low,double high,const char* name) {
    const auto* n=std::get_if<double>(&value.data);
    if(!n||!std::isfinite(*n)||*n<low||*n>high) throw std::invalid_argument(std::string("invalid camera.")+name);
    return *n;
}
struct Field { const char* name; float ScCamera::*member; double minimum,maximum; const char* description; };
constexpr Field fields[]={
    {"zoom",&ScCamera::zoom,.125,8,"Magnification about the logical viewport center."},
    {"rotation",&ScCamera::rotation,-1000,1000,"Clockwise screen rotation in radians; normalized on commit."},
    {"smoothing",&ScCamera::smoothing,0,1,"Follow fraction per fixed update; 1 snaps, 0 holds."}
};
int set(lua_State* L) {
    arguments(L,1,1); writable(L);
    auto value=sc_lua_read(L,1); if(!value) throw std::invalid_argument(value.error());
    const auto* object=std::get_if<ScValue::Object>(&value->data);
    if(!object) throw std::invalid_argument("camera.set requires a plain object");
    auto& w=*script(L).world; auto next=w.camera; float x=w.camera_x,y=w.camera_y; bool detach=false;
    for(const auto& [name,item]:*object) {
        bool matched=false;
        for(const auto& field:fields) if(name==field.name) {
            next.*field.member=static_cast<float>(number(item,field.minimum,field.maximum,field.name)); matched=true; break;
        }
        if(matched) continue;
        if(name=="x"||name=="y") { (name=="x"?x:y)=static_cast<float>(number(item,-1e6,1e6,name.c_str())); detach=true; }
        else if(name=="pixel_snap") {
            const auto* b=std::get_if<bool>(&item.data); if(!b) throw std::invalid_argument("camera.pixel_snap requires boolean"); next.pixel_snap=*b;
        } else if(name=="bounds") {
            if(const auto* mode=std::get_if<std::string>(&item.data);mode&&*mode=="map") next.bounds=ScCameraBounds::map;
            else if(const auto* disabled=std::get_if<bool>(&item.data);disabled&&!*disabled) next.bounds=ScCameraBounds::none;
            else if(const auto* rectangle=std::get_if<ScValue::Object>(&item.data)) {
                if(rectangle->size()!=4||!rectangle->contains("x")||!rectangle->contains("y")||!rectangle->contains("w")||!rectangle->contains("h"))
                    throw std::invalid_argument("camera.bounds requires x,y,w,h");
                next.rectangle={static_cast<float>(number(rectangle->at("x"),-1e6,1e6,"bounds.x")),static_cast<float>(number(rectangle->at("y"),-1e6,1e6,"bounds.y")),
                    static_cast<float>(number(rectangle->at("w"),.001,2e6,"bounds.w")),static_cast<float>(number(rectangle->at("h"),.001,2e6,"bounds.h"))};
                if(next.rectangle.x+next.rectangle.w>1e6||next.rectangle.y+next.rectangle.h>1e6) throw std::invalid_argument("camera.bounds exceeds coordinate limit");
                next.bounds=ScCameraBounds::custom;
            } else throw std::invalid_argument("camera.bounds requires 'map', false or a rectangle");
        } else throw std::invalid_argument("unknown camera field: "+name);
    }
    next.rotation=std::remainder(next.rotation,2*std::numbers::pi_v<float>);
    w.camera=next; w.camera_x=x; w.camera_y=y; if(detach) w.camera_target=0; sc_camera_step(w,false); return 0;
}
int follow(lua_State* L) {
    arguments(L,1,1); writable(L); auto& w=*script(L).world;
    if(lua_isnil(L,1)) { w.camera_target=0; return 0; }
    if(!lua_isinteger(L,1)) return luaL_error(L,"camera.follow requires a live entity handle or nil");
    const auto value=lua_tointeger(L,1);
    const auto* e=value>0&&value<=static_cast<lua_Integer>(SC_ID_MAX)?sc_entity(&w,static_cast<ScEntityId>(value)):nullptr;
    if(!e) return luaL_error(L,"camera.follow requires a live entity handle");
    w.camera_target=e->id; w.camera_x=e->x+e->w*.5f-static_cast<float>(w.view_width)*.5f;
    w.camera_y=e->y+e->h*.5f-static_cast<float>(w.view_height)*.5f; sc_camera_step(w,false); return 0;
}
float argument_number(lua_State* L,int index,float low,float high) {
    if(lua_type(L,index)!=LUA_TNUMBER) luaL_error(L,"camera requires a finite number");
    const auto n=lua_tonumber(L,index);
    if(!std::isfinite(n)||n<low||n>high) luaL_error(L,"camera number outside supported range");
    return static_cast<float>(n);
}
int shake(lua_State* L) {
    arguments(L,2,3); writable(L);
    const float amplitude=argument_number(L,1,0,256),duration=argument_number(L,2,0,60);
    std::uint32_t seed=1;
    if(lua_gettop(L)==3) {
        if(!lua_isinteger(L,3)||lua_tointeger(L,3)<0||lua_tointeger(L,3)>UINT32_MAX) return luaL_error(L,"camera shake seed requires uint32");
        seed=static_cast<std::uint32_t>(lua_tointeger(L,3));
    }
    auto& c=script(L).world->camera; c.shake_amplitude=amplitude; c.shake_frame=0;
    c.shake_frames=static_cast<std::uint32_t>(std::ceil(duration*60)); c.shake_seed=seed;
    c.shake_pending=c.shake_frames>0&&amplitude>0; return 0;
}
int convert(lua_State* L,bool inverse) {
    arguments(L,2,2); const ScCameraPoint point{argument_number(L,1,-1e7,1e7),argument_number(L,2,-1e7,1e7)};
    const auto& s=script(L);
    const auto v=sc_display_camera(*s.world,s.phase==2?s.draw_alpha:1);
    const auto p=inverse?sc_camera_to_world(v,point):sc_camera_to_screen(v,point);
    lua_createtable(L,0,2); lua_pushnumber(L,p.x); lua_setfield(L,-2,"x"); lua_pushnumber(L,p.y); lua_setfield(L,-2,"y"); return 1;
}
int to_world(lua_State* L) { return convert(L,true); }
int to_screen(lua_State* L) { return convert(L,false); }
int read(lua_State* L) {
    arguments(L,0,0); auto& s=script(L);
    {
        const auto& w=*s.world; const auto& c=w.camera; const auto v=sc_display_camera(w,s.phase==2?s.draw_alpha:1); using V=ScValue;
        const auto anchor=sc_display_camera_anchor(w,s.phase==2?s.draw_alpha:1);
        V bounds=c.bounds==ScCameraBounds::none?V{false}:V{std::string("map")};
        if(c.bounds==ScCameraBounds::custom) bounds=V{V::Object{{"x",V{double(c.rectangle.x)}},{"y",V{double(c.rectangle.y)}},{"w",V{double(c.rectangle.w)}},{"h",V{double(c.rectangle.h)}}}};
        s.scratch=V{V::Object{{"x",V{double(w.camera_x)}},{"y",V{double(w.camera_y)}},{"zoom",V{double(c.zoom)}},{"rotation",V{double(c.rotation)}},
            {"smoothing",V{double(c.smoothing)}},{"pixel_snap",V{c.pixel_snap}},{"target",V{double(w.camera_target)}},{"bounds",std::move(bounds)},
            {"center_x",V{double(v.center.x)}},{"center_y",V{double(v.center.y)}},
            {"anchor_x",V{double(anchor.x)}},{"anchor_y",V{double(anchor.y)}},
            {"view_zoom",V{double(v.zoom)}},{"view_rotation",V{double(v.rotation)}},
            {"visible",V{V::Object{{"x",V{double(v.visible.x)}},{"y",V{double(v.visible.y)}},{"w",V{double(v.visible.w)}},{"h",V{double(v.visible.h)}}}}}}};
    }
    sc_lua_push(L,s.scratch); return 1;
}
constexpr ScLuaParameter set_args[]={{"patch","ScCameraPatch"}},follow_args[]={{"id","ScEntityId|nil"}},
    shake_args[]={{"amplitude","number"},{"duration","number"},{"seed","integer",false}},coords[]={{"x","number"},{"y","number"}};
constexpr ScLuaContract set_contract{set_args,nullptr,ScLuaPhases::mutate},follow_contract{follow_args,nullptr,ScLuaPhases::mutate},
    shake_contract{shake_args,nullptr,ScLuaPhases::mutate},read_contract{{},"ScCameraState",ScLuaPhases::read},convert_contract{coords,"ScCameraPoint",ScLuaPhases::read};
const ScLuaApi api[]={
    {"set",sc_lua_guard<set>,"set(patch)","Atomically patch camera settings; x/y detach follow. Bounds clamp immediately.",&set_contract},
    {"follow",sc_lua_guard<follow>,"follow(id)","Attach and snap to a live entity center, or detach with nil; subsequent fixed updates use smoothing.",&follow_contract},
    {"read",sc_lua_guard<read>,"read() -> state","Copy fixed camera settings and effective view including pixel snapping and shake; draw uses display interpolation. anchor_x/y expose the interpolated unshaken origin for parallax. target=0 means detached.",&read_contract},
    {"shake",sc_lua_guard<shake>,"shake(amplitude,duration,seed?)","Replace shake: amplitude 0..256 world pixels, duration 0..60 seconds, uint32 seed defaults to 1. Zero cancels; no shared RNG consumed.",&shake_contract},
    {"to_world",sc_lua_guard<to_world>,"to_world(x,y) -> point","Convert logical viewport pixels (e.g. sc.input.mouse coordinates) to world coordinates. Check the separate inside result; black bars are outside the viewport.",&convert_contract},
    {"to_screen",sc_lua_guard<to_screen>,"to_screen(x,y) -> point","Convert world coordinates to logical viewport pixels using the current view (interpolated during draw, fixed elsewhere). Coordinates accept finite -1e7..1e7.",&convert_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_camera_register(lua_State* L) { lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"camera"); }
void sc_script_camera_describe() { sc_api_describe(api,"sc.camera."); }
ScValue sc_script_camera_contracts() {
    using V=ScValue;
    auto field=[](const char* name,const char* type,const char* description,bool required=false) {
        return V::Object{{"name",V{std::string(name)}},{"type",V{std::string(type)}},{"required",V{required}},{"description",V{std::string(description)}}};
    };
    V::Array patch;
    for(const char* name:{"x","y"}) { auto f=field(name,"number","Unscaled viewport top-left anchor; -1e6..1e6, default 0. Zoom and rotation pivot about anchor + half viewport."); f.emplace("default",V{0.}); patch.emplace_back(std::move(f)); }
    const ScCamera defaults;
    for(const auto& n:fields) { auto f=field(n.name,"number",n.description); f.emplace("minimum",V{n.minimum}); f.emplace("maximum",V{n.maximum}); f.emplace("default",V{double(defaults.*n.member)}); patch.emplace_back(std::move(f)); }
    auto snap=field("pixel_snap","boolean","Floor effective camera origin and drawn object origins; disable for fractional motion."); snap.emplace("default",V{true}); patch.emplace_back(std::move(snap));
    auto bounds=field("bounds","'map'|false|ScCameraRect","Map bounds, unbounded coordinates or a custom rectangle. Undersized axes align the visible top/left edge; shake may extend outside bounds."); bounds.emplace("default",V{std::string("map")}); patch.emplace_back(std::move(bounds));
    V::Array state=patch;
    for(auto& item:state) std::get<V::Object>(item.data)["required"]=V{true};
    for(const char* name:{"target","center_x","center_y","anchor_x","anchor_y","view_zoom","view_rotation"}) state.emplace_back(field(name,"number","Read-only effective camera state; center, anchor and view_* values interpolate in draw.",true));
    state.emplace_back(field("visible","ScCameraRect","Conservative world AABB of rotated viewport, including shake.",true));
    V::Array point{V{field("x","number","X coordinate.",true)},V{field("y","number","Y coordinate, positive down.",true)}};
    auto rect=point; for(const char* name:{"w","h"}) rect.emplace_back(field(name,"number","Positive extent; custom bounds .001..2e6, endpoints within -1e6..1e6.",true));
    auto type=[](V::Array f) { return V{V::Object{{"fields",V{std::move(f)}},{"constraints",V{V::Array{}}}}}; };
    return V{V::Object{{"ScCameraPatch",type(std::move(patch))},{"ScCameraState",type(std::move(state))},{"ScCameraPoint",type(std::move(point))},{"ScCameraRect",type(std::move(rect))}}};
}
