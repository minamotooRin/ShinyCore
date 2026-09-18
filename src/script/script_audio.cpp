#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/script_audio.h"
#include <lua.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <tuple>
namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"audio mutation is forbidden in draw() or migration"); }
int bus_index(std::string_view name) {
    constexpr const char* names[]={"master","music","sfx","ui"};
    for(int i=0;i<4;++i) if(name==names[i]) return i;
    throw std::runtime_error("unknown audio bus");
}
ScAudioVoice* voice(lua_State* L) {
    int valid=0; auto id=lua_tointegerx(L,1,&valid);
    if(!valid||id<=0||id>UINT32_MAX||(id&63)>=34) luaL_error(L,"invalid audio handle");
    auto* v=&script(L)->world->audio[static_cast<size_t>(id&63)];
    if(!v->alive||v->id!=id) luaL_error(L,"stale audio handle");
    return v;
}
bool patch(lua_State* L,int index,ScAudioVoice* v) {
    if(lua_isnoneornil(L,index)) return true;
    try {
        auto value=sc_lua_read(L,index); if(!value) throw std::runtime_error(value.error());
        const auto* object=std::get_if<ScValue::Object>(&value->data); if(!object) throw std::runtime_error("audio options must be a table");
        for(const auto& [key,item]:*object) {
            auto n=[&](double lo,double hi) { auto p=std::get_if<double>(&item.data); if(!p||*p<lo||*p>hi) throw std::runtime_error("invalid audio."+key); return static_cast<float>(*p); };
            auto b=[&]() { auto p=std::get_if<bool>(&item.data); if(!p) throw std::runtime_error("invalid audio."+key); return *p; };
            if(key=="volume") v->target_volume=n(0,1);
            else if(key=="bus") v->bus=bus_index(item.text());
            else if(key=="pan") v->pan=n(-1,1);
            else if(key=="priority") { auto priority=n(-128,127); if(std::floor(priority)!=priority) throw std::runtime_error("priority requires integer"); v->priority=static_cast<int>(priority); }
            else if(key=="pitch") v->pitch=n(.25,4);
            else if(key=="fade") v->fade=n(0,60);
            else if(key=="loop") v->loop=b();
            else if(key=="paused") v->paused=b();
            else if(key=="persistent") v->persistent=b();
            else throw std::runtime_error("unknown audio field: "+key);
        }
        if(!v->fade) v->volume=v->target_volume;
        return true;
    } catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); return false; }
}
int play(lua_State* L) {
    mutable_phase(L); if(lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"audio resource name required");
    const char* name=lua_tostring(L,1); auto* w=script(L)->world; const ScResource* resource=nullptr;
    for(const auto& r:w->resources) if(r.name==name) resource=&r;
    if(!resource||(resource->type!="sound"&&resource->type!="music")) return luaL_error(L,"unknown sound/music resource");
    ScAudioVoice candidate; candidate.alive=true; candidate.music=resource->type=="music"; candidate.bus=candidate.music?1:2; candidate.duration=resource->duration;
    std::snprintf(candidate.path,sizeof candidate.path,"%s",resource->path.c_str());
    if(!patch(L,2,&candidate)) return luaL_error(L,"%s",script(L)->error);
    if(candidate.fade) candidate.volume=0;
    const size_t begin=candidate.music?32:0,end=candidate.music?34:w->sound_voice_limit;
    size_t selected=end;
    for(size_t i=begin;i<end;++i) if(!w->audio[i].alive) { selected=i; break; }
    if(selected==end) for(size_t i=begin;i<end;++i) {
        const auto& current=w->audio[i];
        if(current.priority>candidate.priority) continue;
        if(selected==end||std::tie(current.priority,current.age,current.id)<std::tie(w->audio[selected].priority,w->audio[selected].age,w->audio[selected].id)) selected=i;
    }
    if(selected!=end) {
        auto generation=(w->audio_generations[selected]+1)&0x03ffffffu;
        if(!generation) return luaL_error(L,"audio handle generation exhausted");
        w->audio_generations[selected]=generation;
        candidate.age=++w->audio_clock;
        candidate.id=(generation<<6)|static_cast<uint32_t>(selected); w->audio[selected]=candidate;
        lua_pushinteger(L,candidate.id); return 1;
    }
    lua_pushnil(L); lua_pushliteral(L,"audio voice capacity exhausted"); return 2;
}
int set(lua_State* L) {
    mutable_phase(L); auto* v=voice(L); auto candidate=*v;
    if(!patch(L,2,&candidate)) return luaL_error(L,"%s",script(L)->error);
    *v=candidate; return 0;
}
int stop(lua_State* L) {
    mutable_phase(L); auto* v=voice(L); float fade=0;
    if(!lua_isnoneornil(L,2)) {
        if(lua_type(L,2)!=LUA_TNUMBER) return luaL_error(L,"fade must be a number");
        double n=lua_tonumber(L,2); if(!std::isfinite(n)||n<0||n>60) return luaL_error(L,"fade must be 0..60"); fade=static_cast<float>(n);
    }
    if(!fade) v->alive=false; else { v->fade=fade; v->target_volume=0; v->stopping=true; } return 0;
}
int bus(lua_State* L) {
    auto* s=script(L); const char* name=luaL_checkstring(L,1);
    int index=bus_index(name); auto candidate=s->world->audio_buses[static_cast<size_t>(index)];
    if(!lua_isnoneornil(L,2)) {
        mutable_phase(L);
        {
            auto value=sc_lua_read(L,2); if(!value) throw std::runtime_error(value.error());
            auto* fields=std::get_if<ScValue::Object>(&value->data);
            if(!fields) throw std::runtime_error("audio bus options must be a table");
            for(const auto& [key,item]:*fields) {
                if(key=="paused") { auto* paused=std::get_if<bool>(&item.data); if(!paused) throw std::runtime_error("bus paused requires boolean"); candidate.paused=*paused; }
                else if(key=="volume"||key=="fade") {
                    auto* n=std::get_if<double>(&item.data);
                    if(!n||!std::isfinite(*n)||*n<0||*n>(key=="volume"?1:60)) throw std::runtime_error("audio bus option outside range");
                    if(key=="volume") candidate.target=static_cast<float>(*n); else candidate.fade=static_cast<float>(*n);
                } else throw std::runtime_error("unknown audio bus option: "+key);
            }
        }
        if(candidate.fade==0) candidate.volume=candidate.target;
        s->world->audio_buses[static_cast<size_t>(index)]=candidate;
    }
    lua_createtable(L,0,4);
    lua_pushnumber(L,candidate.volume); lua_setfield(L,-2,"volume");
    lua_pushnumber(L,candidate.target); lua_setfield(L,-2,"target");
    lua_pushnumber(L,candidate.fade); lua_setfield(L,-2,"fade");
    lua_pushboolean(L,candidate.paused); lua_setfield(L,-2,"paused"); return 1;
}
const ScLuaApi api[]={
    {"bus",sc_lua_guard<bus>,"bus(name,options?) -> state","Read or patch master/music/sfx/ui volume, fade and pause; buses persist across rooms."},
    {"play",sc_lua_guard<play>,"play(resource[,options]) -> handle|nil,error","WAV sound: 32 voices; Ogg Vorbis music: 2 streams. Options: volume=1 (0..1), pitch=1 (.25..4), fade=0 (0..60 seconds), loop/paused/persistent=false, pan=0 (-1..1), priority=0 (-128..127), bus=music or sfx. Lower priority and older voices are stolen first. Mutation forbidden in draw/migration."},
    {"set",sc_lua_guard<set>,"set(handle,options)","Atomically update voice options; stale handles error. Persistent applies to music across rooms."},
    {"stop",sc_lua_guard<stop>,"stop(handle[,fade])","Stop immediately or fade out over 0..60 seconds (default 0); stale handles error."},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_audio_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"audio");
}
void sc_script_audio_describe() { sc_api_describe(api,"sc.audio."); }
