#include "script_systems.h"
#include "script_application.h"
#include "script_particles.h"
#include "script_projectiles.h"
#include "script_navigation.h"
#include "script_input.h"
#include "script_images.h"
#include "script_api.h"
#include <tuple>
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/projectiles.h"
#include "shiny/navigation.h"
#include "shiny/settings.h"
#include "shiny/physics.h"
#ifdef SC_HAS_DEVTOOLS
#include "../dev/inspect.h"
#endif
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"operation requires init or update"); }
void read(lua_State* L,int index) {
    auto value=sc_lua_read(L,index); if(!value) throw std::invalid_argument(value.error()); script(L)->scratch=std::move(*value);
}
int watch(lua_State* L) {
    mutable_phase(L); auto* s=script(L);
    if(lua_gettop(L)!=2||lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"watch expects a name string and value");
    std::size_t size{}; const char* key=lua_tolstring(L,1,&size);
    if(!size||size>128||std::memchr(key,0,size)) return luaL_error(L,"watch name requires 1..128 bytes without NUL");
    if(!s->watches.contains(key)&&s->watches.size()>=64) return luaL_error(L,"watch capacity exhausted (64)");
    read(L,2);
    // Validate serialized UTF-8 and byte bounds before replacing a visible watch.
    { auto valid=sc_json_read(sc_json_write(s->scratch));
      if(!valid) throw std::invalid_argument(valid.error()); }
    { auto valid=sc_json_read(sc_json_write(ScValue{std::string(key)}),1024);
      if(!valid) throw std::invalid_argument(valid.error()); }
    s->watches.insert_or_assign(std::string(key),std::move(s->scratch));
    return 0;
}
#ifdef SC_HAS_STREAMING
int stream_open(lua_State* L) {
    if(script(L)->phase!=0) return luaL_error(L,"stream.open requires initialization");
    if(lua_gettop(L)!=1||lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"stream.open expects one path string");
    std::size_t length=0; const char* path=lua_tolstring(L,1,&length);
    if(length>=SC_PATH_MAX||std::memchr(path,0,length)||!sc_script_validate_path(path)) return luaL_error(L,"stream index must be project-relative");
    auto* s=script(L);
    const auto file=std::string(s->root)+"/"+path;
    s->stream=s->content_loader?std::make_unique<ScStream>(file,*s->content_loader):std::make_unique<ScStream>(file);
    return 0;
}
template<int Operation> int stream_chunk(lua_State* L) {
    mutable_phase(L); auto* s=script(L);
    if(lua_gettop(L)!=(Operation==0?3:2)) return luaL_error(L,"unexpected stream argument count");
    if(!s->stream) return luaL_error(L,"stream is not open");
    if(lua_type(L,1)!=LUA_TNUMBER||lua_type(L,2)!=LUA_TNUMBER) return luaL_error(L,"chunk coordinates must be numbers");
    auto x=luaL_checkinteger(L,1),y=luaL_checkinteger(L,2);
    if(x<-31250||x>31250||y<-31250||y>31250) return luaL_error(L,"chunk coordinates outside range");
    if constexpr(Operation==0) {
        if(lua_type(L,3)!=LUA_TNUMBER) return luaL_error(L,"commit_frame must be a number");
        auto frame=luaL_checkinteger(L,3);
        if(frame<0||static_cast<std::uint64_t>(frame)<s->world->tick+(s->phase==1?1u:0u)||frame>4503599627370495LL)
            return luaL_error(L,"commit_frame must be a future simulation tick (initialization may request tick 0)");
        auto sequence=s->stream->request(static_cast<int>(x),static_cast<int>(y),static_cast<std::uint64_t>(frame));
        lua_pushinteger(L,static_cast<lua_Integer>(sequence)); return 1;
    }
    else if constexpr(Operation==1) {
        bool ready=false;
        { auto value=s->stream->get(static_cast<int>(x),static_cast<int>(y)); if(!value) throw std::runtime_error(value.error());
          ready=value->has_value(); if(ready) s->scratch=std::move(**value); }
        if(ready) sc_lua_push(L,s->scratch); else lua_pushnil(L); return 1;
    } else { s->stream->release(static_cast<int>(x),static_cast<int>(y)); return 0; }
}
int stream_stats(lua_State* L) {
    if(lua_gettop(L)!=0) return luaL_error(L,"stream.stats expects no arguments");
    auto* s=script(L); if(!s->stream) return luaL_error(L,"stream is not open");
    s->scratch=s->stream->statistics(); sc_lua_push(L,s->scratch); return 1;
}
int stream_failure(lua_State* L) {
    if(lua_gettop(L)!=0) return luaL_error(L,"stream.failure expects no arguments");
    auto* s=script(L); if(!s->stream) return luaL_error(L,"stream is not open");
    s->scratch=s->stream->failure(); sc_lua_push(L,s->scratch); return 1;
}
int stream_retry(lua_State* L) {
    auto* s=script(L); if(s->phase!=4) mutable_phase(L);
    if(lua_gettop(L)!=1||lua_type(L,1)!=LUA_TNUMBER) return luaL_error(L,"stream.retry expects one request sequence");
    const auto sequence=luaL_checkinteger(L,1);
    if(sequence<1||sequence>4503599627370495LL) return luaL_error(L,"stream request sequence outside 1..2^52-1");
    if(!s->stream) return luaL_error(L,"stream is not open");
    s->stream->retry(static_cast<std::uint64_t>(sequence));
    lua_pushboolean(L,true); return 1;
}
int stream_metadata(lua_State* L) {
    mutable_phase(L);
    if(lua_gettop(L)!=0) return luaL_error(L,"stream.metadata expects no arguments");
    auto* s=script(L); if(!s->stream) return luaL_error(L,"stream is not open");
    s->scratch=ScValue{ScValue::Object{}};
    {
        auto& out=std::get<ScValue::Object>(s->scratch.data);
        for(const char* name:{"format","chunk_size","tilewidth","tileheight","layers","tilesets","parallaxoriginx","parallaxoriginy"})
            if(const auto* value=s->stream->metadata().get(name)) out.emplace(name,*value);
    }
    sc_lua_push(L,s->scratch); return 1;
}
int stream_terrain(lua_State* L) {
    mutable_phase(L);
    if(lua_gettop(L)<1||lua_gettop(L)>3) return luaL_error(L,"stream.terrain expects shapes, optional navigation and entering entities");
    auto* s=script(L); if(!s->stream) return luaL_error(L,"stream is not open");
    const bool replace_navigation=lua_gettop(L)>=2&&!lua_isnil(L,2);
    const bool entering=lua_gettop(L)==3&&!lua_isnil(L,3);
    lua_createtable(L,2,0);
    lua_pushvalue(L,1); lua_rawseti(L,-2,1);
    if(replace_navigation) { lua_pushvalue(L,2); lua_rawseti(L,-2,2); }
    read(L,-1); lua_pop(L,1);
    if(entering) sc_script_prepare_spawn_batch(L,3);
    {
        std::vector<ScTerrainShape> shapes;
        const auto& args=std::get<ScValue::Array>(s->scratch.data);
        const auto* array=std::get_if<ScValue::Array>(&args[0].data);
        const auto* empty=std::get_if<ScValue::Object>(&args[0].data);
        if(!array&&(!empty||!empty->empty())) throw std::runtime_error("terrain requires a dense shape array");
        if(array) {
            if(array->size()>SC_MAX_TILES) throw std::runtime_error("terrain shape capacity exhausted");
            shapes.reserve(array->size());
            for(const auto& value:*array) {
                const auto* object=std::get_if<ScValue::Object>(&value.data);
                if(!object) throw std::runtime_error("terrain shape must be a plain object");
                ScTerrainShape shape;
                for(const auto& [name,field]:*object) {
                    if(name=="one_way") {
                        const auto* flag=std::get_if<bool>(&field.data);
                        if(!flag) throw std::runtime_error("terrain one_way must be boolean");
                        shape.one_way=*flag;
                    } else if(name=="vertices") {
                        const auto* points=std::get_if<ScValue::Array>(&field.data);
                        if(!points||points->size()<6||points->size()>16||points->size()%2)
                            throw std::runtime_error("terrain vertices require 3..8 x,y pairs");
                        shape.vertex_count=static_cast<int>(points->size()/2);
                        for(std::size_t i=0;i<points->size();++i) shape.vertices[i]=static_cast<float>((*points)[i].number(NAN));
                    } else {
                        const auto n=static_cast<float>(field.number(NAN));
                        if(name=="x") shape.x=n; else if(name=="y") shape.y=n;
                        else if(name=="w") shape.w=n; else if(name=="h") shape.h=n;
                        else throw std::runtime_error("unknown terrain shape field: "+name);
                    }
                }
                shapes.push_back(shape);
            }
        }
        std::bitset<SC_MAX_TILES> blocked;
        std::unique_ptr<ScNavigationRegion> candidate;
        if(replace_navigation) {
            const auto* fields=std::get_if<ScValue::Object>(&args[1].data);
            if(!fields) throw std::runtime_error("navigation region must be a plain object");
            double x=NAN,y=NAN,tile=8; const ScValue* rows=nullptr;
            for(const auto& [name,value]:*fields) {
                if(name=="x") x=value.number(NAN); else if(name=="y") y=value.number(NAN);
                else if(name=="cell_size") tile=value.number(NAN); else if(name=="rows") rows=&value;
                else throw std::runtime_error("unknown navigation region field: "+name);
            }
            if(!rows) throw std::runtime_error("navigation region requires rows");
            candidate=sc_prepare_navigation_region(x,y,*rows,tile,shapes);
        }
        auto* region=replace_navigation?candidate.get():s->navigation_region.get();
        if(region&&!replace_navigation) {
            blocked=sc_navigation_patch(region->map,s->world->terrain_shapes,shapes,region->x,region->y).blocked;
            if(blocked!=region->map.navigation_blocked&&region->map.navigation_revision==UINT64_MAX)
                throw std::runtime_error("navigation revision exhausted");
        }
        auto result=sc_physics_replace_terrain(*s->world,shapes,entering?std::span<ScEntity>(s->batch_entities):std::span<ScEntity>{});
        if(!result) throw std::runtime_error(result.error());
        if(replace_navigation) {
            s->navigation_region=std::move(candidate);
            for(auto& field:s->flow_fields) field.reset();
        } else if(region&&blocked!=region->map.navigation_blocked) {
            region->map.navigation_blocked=blocked; ++region->map.navigation_revision;
        }
    }
    if(entering) { sc_script_push_spawn_ids(L); lua_pushboolean(L,true); lua_insert(L,-2); return 2; }
    lua_pushboolean(L,true); return 1;
}
constexpr ScLuaParameter stream_path[]={{"index_path","string"}},stream_coordinates[]={{"x","integer"},{"y","integer"}};
constexpr ScLuaParameter stream_request_parameters[]={{"x","integer"},{"y","integer"},{"commit_frame","integer"}};
constexpr ScLuaContract stream_open_contract{stream_path,nullptr,ScLuaPhases::initialize,"128 MiB cache; 16 MiB index; 65536 chunks",nullptr,"streaming"};
constexpr ScLuaContract stream_request_contract{stream_request_parameters,"integer",ScLuaPhases::mutate,"1024 pending requests; coordinates -31250..31250",nullptr,"streaming"};
constexpr ScLuaContract stream_get_contract{stream_coordinates,"table|nil",ScLuaPhases::mutate,nullptr,nullptr,"streaming"};
constexpr ScLuaContract stream_release_contract{stream_coordinates,nullptr,ScLuaPhases::mutate,nullptr,nullptr,"streaming"};
constexpr ScLuaContract stream_stats_contract{{},"table",ScLuaPhases::read,nullptr,nullptr,"streaming"};
constexpr ScLuaContract stream_metadata_contract{{},"table",ScLuaPhases::mutate,nullptr,nullptr,"streaming"};
constexpr ScLuaParameter stream_terrain_parameters[]={{"shapes","table[]"},{"navigation","table|nil"},{"entities","ScEntityPatch[]|nil"}};
constexpr ScLuaContract stream_terrain_contract{stream_terrain_parameters,"boolean,ScEntityId[]|nil",ScLuaPhases::mutate,"16384 shapes; Lua data conversion budget 256 KiB",nullptr,"streaming"};
constexpr ScLuaParameter stream_retry_parameters[]={
    {"sequence","integer",true,"Current failure().sequence; stale, pending, successful or unknown requests raise an error.",nullptr,1,4503599627370495.0}};
constexpr ScLuaContract stream_failure_contract{{},"ScStreamFailure|nil",ScLuaPhases::read,nullptr,nullptr,"streaming"};
constexpr ScLuaContract stream_retry_contract{stream_retry_parameters,"boolean",ScLuaPhases::ui_mutate,nullptr,nullptr,"streaming"};
const ScLuaApi stream_api[]={
    {"failure",sc_lua_guard<stream_failure>,"failure() -> failure|nil","Copy the first failure observed at a scheduled boundary; nil while healthy or retrying. Does not expose background completion timing or change pins/cache order.",&stream_failure_contract},
    {"retry",sc_lua_guard<stream_retry>,"retry(sequence) -> true","Queue the current failed read again without waiting. Retains request sequence, deadline, references, cache reservation and old visible world. Clears failure until the next boundary attempt. Allowed in ui_update; draw forbidden.",&stream_retry_contract},
    {"open",sc_lua_guard<stream_open>,"open(index_path)","Open a built map index with a single worker and 128 MiB bounded cache; load/init only.",&stream_open_contract},
    {"request",sc_lua_guard<stream_chunk<0>>,"request(x,y,commit_frame) -> sequence","Pin and prefetch a 32x32 chunk for a planned simulation tick. Deadlines follow request order; sparse empty regions return sequence 0.",&stream_request_contract},
    {"get",sc_lua_guard<stream_chunk<1>>,"get(x,y) -> chunk|nil","Read committed chunk data without waiting; nil until its planned boundary. Absent sparse chunks are empty. Load/init/update only.",&stream_get_contract},
    {"release",sc_lua_guard<stream_chunk<2>>,"release(x,y)","Release one chunk reference; zero references cancel visibility. Pending cancellation drains at its planned boundary.",&stream_release_contract},
    {"stats",sc_lua_guard<stream_stats>,"stats() -> counters","Read reserved cache bytes, visible/pinned chunks and scheduled request counts; worker completion timing is not exposed.",&stream_stats_contract},
    {"metadata",sc_lua_guard<stream_metadata>,"metadata() -> table","Copy format, chunk_size, tilewidth, tileheight, layers, tilesets and parallaxoriginx/y without chunk directory or object payloads. Load/init/update only.",&stream_metadata_contract},
    {"terrain",sc_lua_guard<stream_terrain>,"terrain(shapes,navigation?,entities?) -> true,ids?","Atomically replace imported terrain with {x=0,y=0,w,h,one_way=false,vertices?} shapes in world pixels. Optional vertices are 3..8 local convex x,y pairs. Retain previous terrain on failure; remove finite room borders and camera clamping on success. Empty array clears imported terrain. Optional navigation {x,y,rows,cell_size=8} replaces the local grid in the same transaction and discards previous flow fields. Optional entering entity batch commits with terrain; returns IDs as second result. Invalid entities or capacity failure retain prior terrain/navigation. Load/init/update only.",&stream_terrain_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
#endif
#ifdef SC_HAS_DEVTOOLS
constexpr ScLuaParameter debug_ui_parameters[]={{"name","string"},{"tree","table|nil",false}};
const ScLuaContract debug_ui_contract{debug_ui_parameters,nullptr,ScLuaPhases::ui_mutate,"64 weakly held trees; names 1..128 bytes",nullptr,"devtools"};
#endif
constexpr ScLuaParameter watch_parameters[]={
    {"name","string",true,"1..128 UTF-8 bytes without NUL; exact string type."},
    {"value","boolean|number|string|table|nil",true,"Copied plain UTF-8 data; finite numbers, dense arrays or string-keyed objects. Nil is a retained null observation, not deletion."}};
constexpr ScLuaContract watch_contract{watch_parameters,nullptr,ScLuaPhases::mutate,"64 names; each value at most 256 KiB and depth 16"};
const ScLuaApi debug_api[]={
    {"watch",sc_lua_guard<watch>,"watch(name,value)","Expose a copied, bounded observation. Invalid input preserves existing watches; Lua metatables are rejected. Available without interactive devtools.",&watch_contract},
#ifdef SC_HAS_DEVTOOLS
    {"ui",sc_lua_guard<sc_debug_ui_register>,"ui(name,tree?)","Register a weak shiny.ui tree for inspection, or remove it with nil; devtools only.",&debug_ui_contract},
#endif
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_systems_register(lua_State* L) {
#ifdef SC_HAS_DEVTOOLS
    sc_debug_ui_init(L);
#endif
    sc_script_particles_register(L);
    lua_newtable(L);
#ifdef SC_HAS_STREAMING
    sc_api_register(L,stream_api); lua_pushboolean(L,true);
#else
    lua_pushboolean(L,false);
#endif
    lua_setfield(L,-2,"available"); lua_setfield(L,-2,"stream");
    sc_script_input_register(L);
    sc_script_images_register(L);
    sc_script_projectiles_register(L);
    sc_script_navigation_register(L);
    sc_script_application_register(L);
    lua_newtable(L); sc_api_register(L,debug_api); lua_setfield(L,-2,"debug");
}
void sc_script_systems_describe() {
    sc_script_particles_describe();
#ifdef SC_HAS_STREAMING
    sc_api_describe(stream_api,"sc.stream.");
#endif
    sc_script_input_describe();
    sc_script_images_describe();
    sc_script_projectiles_describe(); sc_script_navigation_describe();
    sc_script_application_describe(); sc_api_describe(debug_api,"sc.debug.");
}
#ifdef SC_HAS_STREAMING
ScValue sc_script_stream_contracts() {
    ScValue::Array fields;
    for(const auto& [name,type,description]:{
        std::tuple{"sequence","integer","Original ordered request sequence, 1..2^52-1; pass to retry."},
        std::tuple{"frame","integer","Original scheduled commit frame, 0..2^52-1; retry does not move it."},
        std::tuple{"x","integer","Chunk column, -31250..31250."},
        std::tuple{"y","integer","Chunk row, -31250..31250."},
        std::tuple{"message","string","Read/decode/validation diagnostic from the failed chunk."}})
        fields.emplace_back(ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{true}},{"readonly",ScValue{true}},{"description",ScValue{std::string(description)}}});
    ScValue record{ScValue::Object{{"fields",ScValue{std::move(fields)}},
        {"constraints",ScValue{ScValue::Array{ScValue{std::string{
            "Only published by owner-thread advance at a due boundary. Retrying or releasing the last failed pin clears this independent snapshot; other due chunks remain unpublished until the entire batch succeeds."}}}}}}};
    return ScValue{ScValue::Object{{"ScStreamFailure",std::move(record)}}};
}
#endif
