#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/script_audio.h"
#include <lua.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"audio mutation is forbidden in draw() or migration"); }
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
    ScAudioVoice candidate; candidate.alive=true; candidate.music=resource->type=="music"; candidate.duration=resource->duration;
    std::snprintf(candidate.path,sizeof candidate.path,"%s",resource->path.c_str());
    if(!patch(L,2,&candidate)) return luaL_error(L,"%s",script(L)->error);
    if(candidate.fade) candidate.volume=0;
    for(size_t i=candidate.music?32:0;i<(candidate.music?34:32);++i) if(!w->audio[i].alive) {
        auto generation=(w->audio_generations[i]+1)&0x03ffffffu; if(!generation) generation=1;
        w->audio_generations[i]=generation;
        candidate.id=(generation<<6)|static_cast<uint32_t>(i); w->audio[i]=candidate; lua_pushinteger(L,candidate.id); return 1;
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
const ScLuaApi api[]={
    {"play",sc_lua_guard<play>,"play(resource[,options]) -> handle|nil,error","WAV sound: 32 voices; Ogg Vorbis music: 2 streams. Options: volume=1 (0..1), pitch=1 (.25..4), fade=0 (0..60 seconds), loop/paused/persistent=false. Mutation forbidden in draw/migration."},
    {"set",sc_lua_guard<set>,"set(handle,options)","Atomically update voice options; stale handles error. Persistent applies to music across rooms."},
    {"stop",sc_lua_guard<stop>,"stop(handle[,fade])","Stop immediately or fade out over 0..60 seconds (default 0); stale handles error."},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_audio_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"audio");
}
void sc_script_audio_describe() { sc_api_describe(api,"sc.audio."); }
