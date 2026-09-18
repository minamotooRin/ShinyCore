#include "script_systems.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/projectiles.h"
#include "shiny/navigation.h"
#include "shiny/input_replay.h"
#include "shiny/settings.h"
#include <cmath>
#include <utf8proc.h>
#include <cstring>
#include <stdexcept>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"operation requires init or update"); }
const ScValue::Object& object(const ScValue& v) {
    auto* p=std::get_if<ScValue::Object>(&v.data); if(!p) throw std::invalid_argument("expected object"); return *p;
}
double number(const ScValue& v,double lo,double hi) {
    auto* p=std::get_if<double>(&v.data);
    if(!p||!std::isfinite(*p)||*p<lo||*p>hi) throw std::invalid_argument("number outside allowed range");
    return *p;
}
bool boolean(const ScValue& v) {
    auto* p=std::get_if<bool>(&v.data); if(!p) throw std::invalid_argument("expected boolean"); return *p;
}
void read(lua_State* L,int index) {
    auto value=sc_lua_read(L,index); if(!value) throw std::invalid_argument(value.error()); script(L)->scratch=std::move(*value);
}
int configure(lua_State* L) {
    if(script(L)->phase!=0) return luaL_error(L,"projectiles.configure requires initialization");
    auto capacity=luaL_checkinteger(L,1);
    if(capacity<1||capacity>65536) return luaL_error(L,"projectile capacity must be 1..65536");
    auto* w=script(L)->world;
    if(w->projectiles) return luaL_error(L,"projectiles already configured");
    w->projectiles.reset(new ScProjectiles(static_cast<std::size_t>(capacity)));
    return 0;
}
int spawn(lua_State* L) {
    mutable_phase(L); auto* s=script(L);
    if(!s->world->projectiles) return luaL_error(L,"configure projectiles during init first");
    read(L,1);
    {
        const auto* items=std::get_if<ScValue::Array>(&s->scratch.data);
        if(!items) throw std::invalid_argument("projectiles.spawn expects a dense array");
        std::vector<ScProjectileSpec> batch; batch.reserve(items->size());
        for(const auto& item:*items) {
            ScProjectileSpec p;
            for(const auto& [key,value]:object(item)) {
                if(key=="x") p.x=static_cast<float>(number(value,-1e6,1e6));
                else if(key=="y") p.y=static_cast<float>(number(value,-1e6,1e6));
                else if(key=="vx") p.vx=static_cast<float>(number(value,-1e6,1e6));
                else if(key=="vy") p.vy=static_cast<float>(number(value,-1e6,1e6));
                else if(key=="ax") p.ax=static_cast<float>(number(value,-1e6,1e6));
                else if(key=="ay") p.ay=static_cast<float>(number(value,-1e6,1e6));
                else if(key=="radius") p.radius=static_cast<float>(number(value,.001,256));
                else if(key=="life") p.life=static_cast<float>(number(value,.001,3600));
                else if(key=="mask"||key=="color") {
                    double n=number(value,0,UINT32_MAX); if(std::floor(n)!=n) throw std::invalid_argument("mask/color require integer");
                    if(key=="mask") p.mask=static_cast<std::uint32_t>(n); else p.color=static_cast<std::uint32_t>(n);
                }
                else if(key=="terrain") p.terrain=boolean(value);
                else if(key=="piercing") p.piercing=boolean(value);
                else throw std::invalid_argument("unknown projectile field: "+key);
            }
            batch.push_back(p);
        }
        auto ids=s->world->projectiles->spawn(batch); ScValue::Array output; output.reserve(ids.size());
        for(auto id:ids) output.push_back(ScValue{static_cast<double>(id)});
        s->scratch=ScValue{std::move(output)};
    }
    sc_lua_push(L,s->scratch); return 1;
}
int hits(lua_State* L) {
    auto* s=script(L);
    {
        ScValue::Array result;
        if(s->world->projectiles) for(const auto& hit:s->world->projectiles->hits)
            result.push_back(ScValue{ScValue::Object{{"projectile",ScValue{static_cast<double>(hit.projectile)}},{"target",ScValue{static_cast<double>(hit.target)}},
                {"fraction",ScValue{static_cast<double>(hit.fraction)}},{"x",ScValue{static_cast<double>(hit.x)}},{"y",ScValue{static_cast<double>(hit.y)}}}});
        s->scratch=ScValue{std::move(result)};
    }
    sc_lua_push(L,s->scratch); return 1;
}
int count(lua_State* L) { auto* p=script(L)->world->projectiles.get(); lua_pushinteger(L,p?static_cast<lua_Integer>(p->count):0); return 1; }
int clear(lua_State* L) { mutable_phase(L); if(auto* p=script(L)->world->projectiles.get()) p->clear(); return 0; }
int path(lua_State* L) {
    const auto sx=luaL_checkinteger(L,1),sy=luaL_checkinteger(L,2);
    const auto gx=luaL_checkinteger(L,3),gy=luaL_checkinteger(L,4);
    auto budget=luaL_optinteger(L,5,16384); auto* s=script(L); const auto& map=s->world->map;
    if(sx<0||sy<0||gx<0||gy<0||sx>=map.width||gx>=map.width||sy>=map.height||gy>=map.height||budget<1||budget>1048576)
        return luaL_error(L,"navigation coordinates or budget outside range");
    {
        auto result=sc_path(map,static_cast<int>(sy*map.width+sx),static_cast<int>(gy*map.width+gx),static_cast<std::size_t>(budget)); ScValue::Array points;
        for(int cell:result.cells) points.push_back(ScValue{ScValue::Object{{"x",ScValue{double(cell%map.width)}},{"y",ScValue{double(cell/map.width)}}}});
        s->scratch=ScValue{ScValue::Object{{"status",ScValue{std::string(result.status)}},{"visited",ScValue{double(result.visited)}},{"points",ScValue{std::move(points)}}}};
    }
    sc_lua_push(L,s->scratch); return 1;
}
int pause(lua_State* L) { mutable_phase(L); luaL_checktype(L,1,LUA_TBOOLEAN); script(L)->world->simulation_paused=lua_toboolean(L,1)!=0; return 0; }
int flow(lua_State* L) {
    mutable_phase(L); auto* s=script(L); const auto& map=s->world->map;
    auto x=luaL_checkinteger(L,1),y=luaL_checkinteger(L,2);
    auto budget=luaL_optinteger(L,3,16384),slot=luaL_optinteger(L,4,1);
    if(x<0||y<0||x>=map.width||y>=map.height||budget<1||budget>1048576||slot<1||slot>16)
        return luaL_error(L,"flow coordinates, budget or slot outside range");
    auto& field=s->flow_fields[static_cast<size_t>(slot-1)];
    if(!field) field=std::make_unique<ScFlowField>();
    field->build(map,static_cast<int>(y*map.width+x),static_cast<size_t>(budget));
    lua_pushinteger(L,(static_cast<lua_Integer>(s->world->epoch)<<8)|slot);
    lua_pushlstring(L,field->status.data(),field->status.size());
    lua_pushinteger(L,static_cast<lua_Integer>(field->visited)); return 3;
}
ScFlowField* flow_at(lua_State* L,int argument) {
    auto id=luaL_checkinteger(L,argument); auto* s=script(L); auto slot=id&255;
    if(id<0||(id>>8)!=s->world->epoch||slot<1||slot>16||!s->flow_fields[static_cast<size_t>(slot-1)])
        luaL_error(L,"invalid flow field handle");
    return s->flow_fields[static_cast<size_t>(slot-1)].get();
}
int flow_direction(lua_State* L) {
    auto* field=flow_at(L,1); double x=luaL_checknumber(L,2),y=luaL_checknumber(L,3);
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1e6||std::fabs(y)>1e6) return luaL_error(L,"flow position outside range");
    auto [dx,dy]=field->direction(script(L)->world->map,static_cast<float>(x),static_cast<float>(y));
    lua_pushnumber(L,dx); lua_pushnumber(L,dy); return 2;
}
int steer(lua_State* L) {
    mutable_phase(L); auto* field=flow_at(L,1); auto* s=script(L);
    double speed=luaL_checknumber(L,3);
    if(!std::isfinite(speed)||speed<0||speed>1e6) return luaL_error(L,"steering speed outside range");
    luaL_checktype(L,2,LUA_TTABLE); const size_t length=lua_rawlen(L,2);
    if(length>s->world->entities.size()) return luaL_error(L,"steering batch exceeds entity capacity");
    // Validate the complete batch before changing any velocity.
    s->batch_entities.resize(length);
    for(size_t i=0;i<length;++i) {
        lua_rawgeti(L,2,static_cast<lua_Integer>(i+1)); auto id=luaL_checkinteger(L,-1); lua_pop(L,1);
        auto* entity=sc_entity(s->world,static_cast<ScEntityId>(id));
        if(!entity) return luaL_error(L,"invalid entity in steering batch");
        s->batch_entities[i]=*entity;
    }
    for(const auto& value:s->batch_entities) {
        auto* entity=sc_entity(s->world,value.id);
        auto [dx,dy]=field->direction(s->world->map,entity->x+entity->w*.5f,entity->y+entity->h*.5f);
        entity->vx=dx*static_cast<float>(speed); entity->vy=dy*static_cast<float>(speed);
    }
    lua_pushinteger(L,static_cast<lua_Integer>(length)); return 1;
}
int paused(lua_State* L) { lua_pushboolean(L,script(L)->world->simulation_paused); return 1; }
int quit(lua_State* L) { mutable_phase(L); script(L)->world->exit_requested=true; return 0; }
int watch(lua_State* L) {
    mutable_phase(L); auto* s=script(L); const char* key=luaL_checkstring(L,1);
    if(!*key||std::strlen(key)>128) return luaL_error(L,"watch name requires 1..128 bytes");
    if(!s->watches.contains(key)&&s->watches.size()>=64) return luaL_error(L,"watch capacity exhausted (64)");
    read(L,2);
    s->watches[std::string(key)]=s->scratch;
    return 0;
}
int input_snapshot(lua_State* L) { auto* s=script(L); s->scratch=sc_input_snapshot(s->world->input); sc_lua_push(L,s->scratch); return 1; }
int mouse(lua_State* L) {
    const auto& i=script(L)->world->input;
    lua_pushnumber(L,i.mouse_x); lua_pushnumber(L,i.mouse_y); lua_pushboolean(L,i.mouse_inside); return 3;
}
int text_input(lua_State* L) { const auto& i=script(L)->world->input; lua_pushstring(L,i.text.data()); lua_pushstring(L,i.composition.data()); return 2; }
template<int Edge> int mouse_button(lua_State* L) {
    int id=sc_input_id(SC_MOUSE_BUTTONS,luaL_checkstring(L,1)); if(id<0) return luaL_error(L,"unknown mouse button");
    const auto& in=script(L)->world->input; auto bits=Edge==0?in.mouse_buttons:Edge==1?in.mouse_pressed:in.mouse_released;
    lua_pushboolean(L,(bits&(1u<<id))!=0); return 1;
}
template<int Edge> int keyboard(lua_State* L) {
    size_t length=0; const char* name=luaL_checklstring(L,1,&length);
    int id=sc_input_id(SC_KEYS,std::string_view(name,length));
    if(lua_gettop(L)!=1||id<0) return luaL_error(L,"expected one keyboard control name");
    const auto& in=script(L)->world->input;
    const auto& bits=Edge==0?in.keys:Edge==1?in.key_pressed:in.key_released;
    lua_pushboolean(L,bits[static_cast<size_t>(id)]); return 1;
}
ScPadInput pad_input(lua_State* L,int argument) {
    const auto& in=script(L)->world->input;
    if(lua_isnoneornil(L,argument)) return {in.connected,in.buttons,in.button_pressed,in.button_released,in.axes};
    auto slot=luaL_checkinteger(L,argument);
    if(slot<1||slot>4) luaL_error(L,"gamepad slot must be 1..4");
    return in.pads[static_cast<size_t>(slot-1)];
}
template<int Edge> int pad_button(lua_State* L) {
    size_t length=0; const char* name=luaL_checklstring(L,1,&length);
    int id=sc_input_id(SC_BUTTONS,std::string_view(name,length));
    if(lua_gettop(L)>2||id<0) return luaL_error(L,"unknown gamepad button or argument");
    const auto pad=pad_input(L,2);
    auto bits=Edge==0?pad.buttons:Edge==1?pad.pressed:pad.released;
    lua_pushboolean(L,(bits&(1u<<id))!=0); return 1;
}
int pad_connected(lua_State* L) {
    if(lua_gettop(L)>1) return luaL_error(L,"expected optional gamepad slot");
    lua_pushboolean(L,pad_input(L,1).connected); return 1;
}
int pad_axis(lua_State* L) {
    size_t length=0; const char* name=luaL_checklstring(L,1,&length);
    int id=sc_input_id(SC_AXES,std::string_view(name,length));
    double deadzone=luaL_optnumber(L,2,.2);
    if(lua_gettop(L)>3||id<0||!std::isfinite(deadzone)||deadzone<0||deadzone>=1) return luaL_error(L,"invalid axis, deadzone or argument");
    const auto pad=pad_input(L,3);
    double value=pad.connected?pad.axes[static_cast<size_t>(id)]:0;
    lua_pushnumber(L,std::fabs(value)<=deadzone?0:std::copysign((std::fabs(value)-deadzone)/(1-deadzone),value)); return 1;
}
int wheel(lua_State* L) { const auto& in=script(L)->world->input; lua_pushnumber(L,in.wheel_x); lua_pushnumber(L,in.wheel_y); return 2; }
int boundaries(lua_State* L) {
    size_t size=0; const char* bytes=luaL_checklstring(L,1,&size);
    if(size>65536) return luaL_error(L,"text exceeds 65536 bytes");
    lua_newtable(L); lua_Integer count=0; utf8proc_int32_t previous=0,state=0; size_t offset=0;
    while(offset<size) {
        utf8proc_int32_t code=0;
        auto length=utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(bytes+offset),static_cast<utf8proc_ssize_t>(size-offset),&code);
        if(length<=0) return luaL_error(L,"invalid UTF-8 text");
        if(offset==0||utf8proc_grapheme_break_stateful(previous,code,&state)) { lua_pushinteger(L,static_cast<lua_Integer>(offset+1)); lua_rawseti(L,-2,++count); }
        previous=code; offset+=static_cast<size_t>(length);
    }
    lua_pushinteger(L,static_cast<lua_Integer>(size+1)); lua_rawseti(L,-2,++count); return 1;
}
int focus_text(lua_State* L) {
    mutable_phase(L); auto* w=script(L)->world;
    if(lua_isboolean(L,1)&&!lua_toboolean(L,1)) { w->text_focus=false; return 0; }
    double x=luaL_checknumber(L,1),y=luaL_checknumber(L,2);
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1e6||std::fabs(y)>1e6) return luaL_error(L,"text focus coordinates outside range");
    w->text_x=static_cast<float>(x); w->text_y=static_cast<float>(y); w->text_focus=true; return 0;
}
int clipboard(lua_State* L) {
    auto* w=script(L)->world;
    if(lua_gettop(L)==0) { lua_pushstring(L,w->input.clipboard.data()); return 1; }
    mutable_phase(L); size_t length=0; const char* text=luaL_checklstring(L,1,&length);
    if(length>=w->clipboard_out.size()||std::memchr(text,0,length)) return luaL_error(L,"clipboard requires at most 4095 UTF-8 bytes");
    std::memcpy(w->clipboard_out.data(),text,length); w->clipboard_out[length]=0; w->clipboard_write=true; return 0;
}
const ScLuaApi input_api[]={
    {"key_down",sc_lua_guard<keyboard<0>>,"key_down(name) -> boolean","Fixed snapshot keyboard held state."},
    {"key_pressed",sc_lua_guard<keyboard<1>>,"key_pressed(name) -> boolean","Fixed snapshot keyboard press edge."},
    {"key_released",sc_lua_guard<keyboard<2>>,"key_released(name) -> boolean","Fixed snapshot keyboard release edge."},
    {"gamepad_down",sc_lua_guard<pad_button<0>>,"gamepad_down(name,slot?) -> boolean","Held button; optional stable slot 1..4, default selected controller."},
    {"gamepad_pressed",sc_lua_guard<pad_button<1>>,"gamepad_pressed(name,slot?) -> boolean","Press edge, including taps between ticks."},
    {"gamepad_released",sc_lua_guard<pad_button<2>>,"gamepad_released(name,slot?) -> boolean","Release edge, including device disconnection."},
    {"gamepad_connected",sc_lua_guard<pad_connected>,"gamepad_connected(slot?) -> boolean","Read connection of selected controller or explicit slot 1..4."},
    {"gamepad_axis",sc_lua_guard<pad_axis>,"gamepad_axis(name,deadzone?,slot?) -> number","Read normalized axis with deadzone in [0,1); default .2."},
    {"focus_text",sc_lua_guard<focus_text>,"focus_text(x,y) / focus_text(false)","Set logical-screen IME candidate position or end text focus."},
    {"clipboard",sc_lua_guard<clipboard>,"clipboard(text?) -> text?","Queue a clipboard write or read the recorded paste input for this tick."},
    {"boundaries",sc_lua_guard<boundaries>,"boundaries(text) -> byte_offsets","UTF-8 grapheme starts plus end position; Lua one-based byte offsets."},
    {"snapshot",sc_lua_guard<input_snapshot>,"snapshot() -> input","Fixed-tick input data including four pad slots, pointer and text."},
    {"mouse",sc_lua_guard<mouse>,"mouse() -> x,y,inside","Pointer in logical viewport coordinates."},
    {"text",sc_lua_guard<text_input>,"text() -> committed,composition","UTF-8 text for this fixed tick."},
    {"mouse_down",sc_lua_guard<mouse_button<0>>,"mouse_down(name) -> boolean","Mouse held state."},
    {"mouse_pressed",sc_lua_guard<mouse_button<1>>,"mouse_pressed(name) -> boolean","Mouse press edge."},
    {"mouse_released",sc_lua_guard<mouse_button<2>>,"mouse_released(name) -> boolean","Mouse release edge."},
    {"wheel",sc_lua_guard<wheel>,"wheel() -> x,y","Accumulated fixed-tick wheel deltas."},
    {nullptr,nullptr,nullptr,nullptr}
};
#ifdef SC_HAS_STREAMING
int stream_open(lua_State* L) {
    if(script(L)->phase!=0) return luaL_error(L,"stream.open requires initialization");
    const char* path=luaL_checkstring(L,1);
    if(!sc_script_validate_path(path)) return luaL_error(L,"stream index must be project-relative");
    auto* s=script(L);
    s->stream=std::make_unique<ScStream>(std::string(s->root)+"/"+path);
    return 0;
}
template<int Operation> int stream_chunk(lua_State* L) {
    mutable_phase(L); auto* s=script(L);
    if(!s->stream) return luaL_error(L,"stream is not open");
    auto x=luaL_checkinteger(L,1),y=luaL_checkinteger(L,2);
    if(x<-31250||x>31250||y<-31250||y>31250) return luaL_error(L,"chunk coordinates outside range");
    if constexpr(Operation==0) { s->stream->request(static_cast<int>(x),static_cast<int>(y)); return 0; }
    else if constexpr(Operation==1) {
        { auto value=s->stream->get(static_cast<int>(x),static_cast<int>(y)); if(!value) throw std::runtime_error(value.error()); s->scratch=std::move(*value); }
        sc_lua_push(L,s->scratch); return 1;
    } else { s->stream->release(static_cast<int>(x),static_cast<int>(y)); return 0; }
}
int stream_stats(lua_State* L) {
    auto* s=script(L); if(!s->stream) return luaL_error(L,"stream is not open");
    s->scratch=s->stream->statistics(); sc_lua_push(L,s->scratch); return 1;
}
const ScLuaApi stream_api[]={
    {"open",sc_lua_guard<stream_open>,"open(index_path)","Open a built map index with a single worker and 128 MiB bounded cache; init only."},
    {"request",sc_lua_guard<stream_chunk<0>>,"request(x,y)","Pin and prefetch a 32x32 chunk."},
    {"get",sc_lua_guard<stream_chunk<1>>,"get(x,y) -> chunk","Wait for a requested chunk without advancing simulation; absent sparse chunks are empty."},
    {"release",sc_lua_guard<stream_chunk<2>>,"release(x,y)","Release one chunk reference; unpinned chunks may be evicted."},
    {"stats",sc_lua_guard<stream_stats>,"stats() -> counters","Read cache budget, charged bytes, pinned and queued chunks."},
    {nullptr,nullptr,nullptr,nullptr}
};
#endif
const ScLuaApi projectile_api[]={
    {"configure",sc_lua_guard<configure>,"configure(capacity)","Allocate 1..65536 bullets once during init; no Box2D bodies."},
    {"spawn",sc_lua_guard<spawn>,"spawn(specs) -> ids","Validate a complete batch before spawning; numeric RGBA and collision mask."},
    {"hits",sc_lua_guard<hits>,"hits() -> hits","Previous tick hits ordered by projectile, fraction and target."},
    {"count",sc_lua_guard<count>,"count() -> integer","Current live projectile count."},
    {"clear",sc_lua_guard<clear>,"clear()","Remove all bullets without recycling their IDs."},
    {nullptr,nullptr,nullptr,nullptr}
};
const ScLuaApi nav_api[]={
    {"flow",sc_lua_guard<flow>,"flow(gx,gy,budget?,slot?) -> id,status,visited","Build one shared target field in slot 1..16; rebuild after terrain changes."},
    {"direction",sc_lua_guard<flow_direction>,"direction(flow,x,y) -> dx,dy","Read a normalized shared-field direction at a world position."},
    {"steer",sc_lua_guard<steer>,"steer(flow,entities,speed) -> count","Atomically validate and steer an entity batch without per-unit Lua callbacks."},
    {"path",sc_lua_guard<path>,"path(sx,sy,gx,gy,budget?) -> result","Deterministic A-star in tile coordinates; ok, unreachable or budget_exhausted."},
    {nullptr,nullptr,nullptr,nullptr}
};
const ScLuaApi app_api[]={
    {"pause",sc_lua_guard<pause>,"pause(boolean)","Pause physics and particles while Lua UI and input keep updating."},
    {"paused",sc_lua_guard<paused>,"paused() -> boolean","Read simulation pause state."},
    {"quit",sc_lua_guard<quit>,"quit()","Request orderly application shutdown."},
    {nullptr,nullptr,nullptr,nullptr}
};
int settings_get(lua_State* L) {
    auto* s=script(L);
    if(!s->settings) return luaL_error(L,"settings service unavailable in this host");
    s->scratch=sc_settings_value(s->settings->current);
    sc_lua_push(L,s->scratch); return 1;
}
int settings_apply(lua_State* L) {
    auto* s=script(L);
    if(s->phase!=1||s->checking) return luaL_error(L,"settings.apply requires update outside check mode");
    if(!s->settings) return luaL_error(L,"settings service unavailable in this host");
    bool persist=true;
    if(!lua_isnoneornil(L,2)) { luaL_checktype(L,2,LUA_TBOOLEAN); persist=lua_toboolean(L,2)!=0; }
    read(L,1); bool ok=false;
    { auto result=s->settings->apply(s->scratch,persist); ok=result.has_value(); }
    if(!ok) { lua_pushnil(L); lua_pushstring(L,s->settings->last_error.c_str()); return 2; }
    s->world->audio_gains=s->settings->current.volume;
    lua_pushboolean(L,true); return 1;
}
int settings_error(lua_State* L) {
    auto* s=script(L);
    if(!s->settings) return luaL_error(L,"settings service unavailable in this host");
    lua_pushstring(L,s->settings->last_error.c_str()); return 1;
}
const ScLuaApi settings_api[]={
    {"get",sc_lua_guard<settings_get>,"get() -> settings","Read application display, volume and action binding preferences."},
    {"apply",sc_lua_guard<settings_apply>,"apply(patch,persist?) -> true|nil,error","Validate, apply and atomically persist settings; restore previous preferences on failure. Update only; persist defaults true."},
    {"error",sc_lua_guard<settings_error>,"error() -> string","Read the last settings load/application error; empty when none."},
    {nullptr,nullptr,nullptr,nullptr}
};
const ScLuaApi debug_api[]={
    {"watch",sc_lua_guard<watch>,"watch(name,value)","Expose explicit bounded Lua data for diagnostic snapshots."},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_systems_register(lua_State* L) {
    lua_newtable(L);
#ifdef SC_HAS_STREAMING
    sc_api_register(L,stream_api); lua_pushboolean(L,true);
#else
    lua_pushboolean(L,false);
#endif
    lua_setfield(L,-2,"available"); lua_setfield(L,-2,"stream");
    lua_newtable(L); sc_api_register(L,input_api);
    lua_setfield(L,-2,"input");
    lua_newtable(L); sc_api_register(L,projectile_api); lua_setfield(L,-2,"projectiles");
    lua_newtable(L); sc_api_register(L,nav_api); lua_setfield(L,-2,"navigation");
    lua_newtable(L); sc_api_register(L,app_api); lua_setfield(L,-2,"app");
    lua_newtable(L); sc_api_register(L,settings_api); lua_setfield(L,-2,"settings");
    lua_newtable(L); sc_api_register(L,debug_api); lua_setfield(L,-2,"debug");
}
void sc_script_systems_describe() {
#ifdef SC_HAS_STREAMING
    sc_api_describe(stream_api,"sc.stream.");
#endif
    sc_api_describe(input_api,"sc.input.");
    sc_api_describe(projectile_api,"sc.projectiles."); sc_api_describe(nav_api,"sc.navigation.");
    sc_api_describe(app_api,"sc.app."); sc_api_describe(debug_api,"sc.debug.");
    sc_api_describe(settings_api,"sc.settings.");
}
