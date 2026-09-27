#include "script_api.h"
#include "audio_contract.h"
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
    for(int i=0;i<4;++i) if(name==SC_AUDIO_BUSES[i]) return i;
    throw std::runtime_error("unknown audio bus");
}
ScAudioVoice* voice(lua_State* L) {
    int valid=0; auto id=lua_tointegerx(L,1,&valid);
    if(lua_type(L,1)!=LUA_TNUMBER||!valid||id<=0||id>UINT32_MAX||(id&63)>=34) luaL_error(L,"invalid audio handle");
    auto* v=&script(L)->audio->audio[static_cast<size_t>(id&63)];
    if(!v->alive||v->id!=id) luaL_error(L,"stale audio handle");
    return v;
}
bool patch(lua_State* L,int index,ScAudioVoice* v,bool persistent_music=false) {
    if(lua_isnoneornil(L,index)) return true;
    try {
        auto value=sc_lua_read(L,index); if(!value) throw std::runtime_error(value.error());
        const auto* object=std::get_if<ScValue::Object>(&value->data); if(!object) throw std::runtime_error("audio options must be a table");
        for(const auto& [key,item]:*object) {
            if(persistent_music&&key=="persistent") throw std::runtime_error("music persistence is implicit; omit persistent");
            bool found=false;
            for(const auto& field:SC_AUDIO_NUMBERS) if(key==field.name) {
                auto* n=std::get_if<double>(&item.data);
                if(!n||!std::isfinite(*n)||*n<field.minimum||*n>field.maximum)
                    throw std::runtime_error("invalid audio."+key);
                v->*field.member=static_cast<float>(*n); found=true; break;
            }
            if(found) continue;
            for(const auto& field:SC_AUDIO_BOOLEANS) if(key==field.name) {
                auto* value1=std::get_if<bool>(&item.data);
                if(!value1) throw std::runtime_error("invalid audio."+key);
                v->*field.member=*value1; found=true; break;
            }
            if(found) continue;
            if(key=="bus") v->bus=bus_index(item.text());
            else if(key=="priority") {
                auto* n=std::get_if<double>(&item.data);
                if(!n||!std::isfinite(*n)||*n<SC_AUDIO_PRIORITY_MIN||*n>SC_AUDIO_PRIORITY_MAX||std::floor(*n)!=*n)
                    throw std::runtime_error("audio.priority requires an integer in -128..127");
                v->priority=static_cast<int>(*n);
            } else throw std::runtime_error("unknown audio field: "+key);
        }
        if(!v->fade) v->volume=v->target_volume;
        return true;
    } catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); return false; }
}
int start(lua_State* L,bool persistent_music) {
    if(lua_gettop(L)<1||lua_gettop(L)>2) return luaL_error(L,"audio playback expects resource and optional options");
    mutable_phase(L); if(lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"audio resource name required");
    size_t name_size=0; const char* name=lua_tolstring(L,1,&name_size); auto* w=script(L)->world; auto* mixer=script(L)->audio; const ScResource* resource=nullptr;
    for(const auto& r:w->resources) if(r.name==std::string_view(name,name_size)) resource=&r;
    if(!resource||(resource->type!="sound"&&resource->type!="music")) return luaL_error(L,"unknown sound/music resource");
    if(persistent_music&&resource->type!="music") return luaL_error(L,"audio.music requires a music resource");
    ScAudioVoice candidate; candidate.alive=true; candidate.music=resource->type=="music"; candidate.bus=candidate.music?1:2; candidate.duration=resource->duration;
    std::snprintf(candidate.path,sizeof candidate.path,"%s",resource->path.c_str());
    if(persistent_music) {
        for(size_t i=32;i<34;++i) {
            auto& current=mixer->audio[i];
            if(!current.alive||!current.persistent||current.stopping||std::strcmp(current.path,candidate.path)!=0) continue;
            candidate=current;
            if(!patch(L,2,&candidate,true)) return luaL_error(L,"%s",script(L)->error);
            current=candidate; lua_pushinteger(L,current.id); return 1;
        }
        candidate.persistent=true;
    }
    if(!patch(L,2,&candidate,persistent_music)) return luaL_error(L,"%s",script(L)->error);
    if(candidate.fade) candidate.volume=0;
    const size_t begin=candidate.music?32:0,end=candidate.music?34:mixer->sound_voice_limit;
    size_t selected=end;
    for(size_t i=begin;i<end;++i) if(!mixer->audio[i].alive) { selected=i; break; }
    if(selected==end) for(size_t i=begin;i<end;++i) {
        const auto& current=mixer->audio[i];
        if(current.priority>candidate.priority) continue;
        if(selected==end||std::tie(current.priority,current.age,current.id)<std::tie(mixer->audio[selected].priority,mixer->audio[selected].age,mixer->audio[selected].id)) selected=i;
    }
    if(selected!=end) {
        auto generation=(mixer->audio_generations[selected]+1)&0x03ffffffu;
        if(!generation) return luaL_error(L,"audio handle generation exhausted");
        mixer->audio_generations[selected]=generation;
        candidate.age=++mixer->audio_clock;
        candidate.id=(generation<<6)|static_cast<uint32_t>(selected); mixer->audio[selected]=candidate;
        lua_pushinteger(L,candidate.id); return 1;
    }
    lua_pushnil(L); lua_pushliteral(L,"audio voice capacity exhausted"); return 2;
}
int play(lua_State* L) { return start(L,false); }
int music(lua_State* L) { return start(L,true); }
int set(lua_State* L) {
    mutable_phase(L); auto* v=voice(L); auto candidate=*v;
    if(!patch(L,2,&candidate)) return luaL_error(L,"%s",script(L)->error);
    *v=candidate; return 0;
}
int stop(lua_State* L) {
    mutable_phase(L); auto* v=voice(L); float fade=0;
    if(!lua_isnoneornil(L,2)) {
        if(lua_type(L,2)!=LUA_TNUMBER) return luaL_error(L,"fade must be a number");
        double n=lua_tonumber(L,2); if(!std::isfinite(n)||n<0||n>SC_AUDIO_FADE_MAX) return luaL_error(L,"fade must be 0..60"); fade=static_cast<float>(n);
    }
    if(!fade) v->alive=false; else { v->fade=fade; v->target_volume=0; v->stopping=true; } return 0;
}
int bus(lua_State* L) {
    auto* s=script(L);
    if(lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"audio bus name requires a string");
    size_t name_size=0; const char* name=lua_tolstring(L,1,&name_size);
    int index=bus_index(std::string_view(name,name_size)); auto candidate=s->audio->audio_buses[static_cast<size_t>(index)];
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
                    if(!n||!std::isfinite(*n)||*n<0||*n>(key=="volume"?1:SC_AUDIO_FADE_MAX)) throw std::runtime_error("audio bus option outside range");
                    if(key=="volume") candidate.target=static_cast<float>(*n); else candidate.fade=static_cast<float>(*n);
                } else throw std::runtime_error("unknown audio bus option: "+key);
            }
        }
        if(candidate.fade==0) candidate.volume=candidate.target;
        s->audio->audio_buses[static_cast<size_t>(index)]=candidate;
    }
    lua_createtable(L,0,4);
    lua_pushnumber(L,candidate.volume); lua_setfield(L,-2,"volume");
    lua_pushnumber(L,candidate.target); lua_setfield(L,-2,"target");
    lua_pushnumber(L,candidate.fade); lua_setfield(L,-2,"fade");
    lua_pushboolean(L,candidate.paused); lua_setfield(L,-2,"paused"); return 1;
}
const ScValue zero{0.0};
constexpr ScLuaParameter play_parameters[]={
    {"resource","string",true,"Declared sound or music resource name; embedded NUL is rejected."},
    {"options","ScAudioOptions",false,"Defaults apply only to newly allocated voices."}};
constexpr ScLuaParameter music_parameters[]={
    {"resource","string",true,"Declared music name. Reuses the first live persistent, non-stopping voice with the same resource path."},
    {"options","ScMusicOptions",false,"New voices use defaults; reused voices retain omitted fields and playback position."}};
constexpr ScLuaParameter set_parameters[]={
    {"handle","integer",true,"Live generation-checked voice handle; 1..4294967295."},
    {"options","ScAudioOptions",false,"Atomic patch; nil leaves the voice unchanged."}};
constexpr ScLuaParameter stop_parameters[]={
    {"handle","integer",true,"Live generation-checked voice handle."},
    {"fade","number",false,"Finite seconds; zero stops immediately.",&zero,0,SC_AUDIO_FADE_MAX}};
constexpr ScLuaParameter bus_parameters[]={
    {"name","'master'|'music'|'sfx'|'ui'"},
    {"options","ScAudioBusOptions",false,"Omit or pass nil to read in any phase. A table, including an empty one, is a mutation."}};
constexpr ScLuaContract play_contract{play_parameters,"integer|nil",ScLuaPhases::mutate,"project.limits.sound_voices (0..32); 2 music streams","string|nil"};
constexpr ScLuaContract music_contract{music_parameters,"integer|nil",ScLuaPhases::mutate,"2 music streams; existing matching music reuses its slot","string|nil"};
constexpr ScLuaContract set_contract{set_parameters,nullptr,ScLuaPhases::mutate};
constexpr ScLuaContract stop_contract{stop_parameters,nullptr,ScLuaPhases::mutate};
constexpr ScLuaContract bus_contract{bus_parameters,"ScAudioBusState",ScLuaPhases::read,nullptr,nullptr,"core","options"};
const ScLuaApi api[]={
    {"bus",sc_lua_guard<bus>,"bus(name,options?) -> state","Read or atomically patch a persistent application bus; patching is forbidden in draw/ui_update.",&bus_contract},
    {"play",sc_lua_guard<play>,"play(resource[,options]) -> handle|nil,error","Allocate WAV sound or Ogg Vorbis music. Steal equal/lower priority voices by priority, age and handle; return nil,error when no voice is eligible. Invalid arguments raise an error.",&play_contract},
    {"music",sc_lua_guard<music>,"music(resource[,options]) -> handle|nil,error","Acquire persistent music by declared resource path, patching an existing voice without restarting it; candidate changes remain isolated until commit.",&music_contract},
    {"set",sc_lua_guard<set>,"set(handle,options?)","Atomically patch a live voice; invalid options or stale handles raise an error without changing the voice.",&set_contract},
    {"stop",sc_lua_guard<stop>,"stop(handle[,fade])","Stop immediately or fade to zero, then expire the voice; stale handles raise an error.",&stop_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_audio_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"audio");
}
void sc_script_audio_describe() { sc_api_describe(api,"sc.audio."); }
