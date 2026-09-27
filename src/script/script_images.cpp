#include "script_images.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/projectiles.h"
#include <cstdio>
#include <set>
#include <stdexcept>

namespace {
#ifdef SC_HAS_STREAMING
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
std::uint64_t request(lua_State* L) {
    if(lua_gettop(L)!=1||!lua_isinteger(L,1)) luaL_error(L,"images operation expects one integer request ID");
    const auto id=lua_tointeger(L,1);
    if(id<1||id>4503599627370495LL) luaL_error(L,"image request outside 1..2^52-1");
    if(!script(L)->images) luaL_error(L,"no room image transaction");
    return static_cast<std::uint64_t>(id);
}
std::vector<ScNamedImage> resolve_images(ScScript& s,const ScValue& value) {
    auto* names=std::get_if<ScValue::Array>(&value.data);
    auto* empty=std::get_if<ScValue::Object>(&value.data);
    if(!names&&(!empty||!empty->empty())) throw std::runtime_error("image preload requires a dense name array");
    if(names&&names->size()>128) throw std::runtime_error("image set exceeds 128 names");
    std::vector<ScNamedImage> images;
    std::vector<const ScResource*> dependencies;
    std::set<std::string> requested,canonical;
    if(names) for(const auto& name:*names) {
        const auto* text=std::get_if<std::string>(&name.data);
        if(!text||text->empty()||text->size()>127||text->find('\0')!=text->npos) throw std::runtime_error("image name requires 1..127 bytes without NUL");
        if(!requested.insert(*text).second) throw std::runtime_error("image names must be distinct");
        const ScResource* found=nullptr;
        for(const auto& resource:s.world->resources) if(resource.name==*text&&resource.type=="image") { found=&resource; break; }
        if(!found) for(const auto& resource:s.world->resources) if(resource.path==*text&&resource.type=="image") { found=&resource; break; }
        if(!found) throw std::runtime_error("image must be a declared resource name or path: "+*text);
        if(canonical.insert(found->name).second) dependencies.push_back(found);
    }
    for(std::size_t i=0;i<dependencies.size();++i) {
        const auto* resource=dependencies[i];
        if(resource->streamed) images.push_back({resource->name,{std::string(s.root)+"/"+resource->path,resource->image_width,resource->image_height}});
#ifdef SC_HAS_ADVANCED_RENDER
        if(s.lighting.normal_maps) if(const char* normal=s.lighting.normal_maps->find(resource->path)) {
            for(const auto& candidate:s.world->resources) if(candidate.type=="image"&&candidate.path==normal) {
                if(canonical.insert(candidate.name).second) dependencies.push_back(&candidate);
                break;
            }
        }
#endif
    }
    return images;
}
int prepare(lua_State* L) {
    auto* s=script(L);
    if(lua_gettop(L)!=1||s->phase!=1||s->checking||s->candidate||!s->image_cache||s->pending_scene[0])
        return luaL_error(L,"images.prepare requires one name array in active update, a host and no pending room change");
    std::uint64_t id{};
    {
        auto value=sc_lua_read(L,1); if(!value) throw std::runtime_error(value.error());
        auto images=resolve_images(*s,*value);
        if(!s->images) s->images=std::make_unique<ScRoomImages>(*s->image_cache,s->world->epoch);
        auto prepared=s->images->prepare(std::move(images));
        if(!prepared) throw std::runtime_error(prepared.error());
        id=*prepared;
    }
    lua_pushinteger(L,static_cast<lua_Integer>(id)); return 1;
}
int reload(lua_State* L) {
    auto* s=script(L);
    if(lua_gettop(L)||s->phase!=1||s->checking||s->candidate||!s->images||s->pending_scene[0])
        return luaL_error(L,"images.reload requires active update, committed images and no pending room change");
    std::uint64_t id{};
    { auto result=s->images->reload(); if(!result) throw std::runtime_error(result.error()); id=*result; }
    lua_pushinteger(L,static_cast<lua_Integer>(id)); return 1;
}
int status(lua_State* L) {
    const auto id=request(L); auto* s=script(L);
    { auto value=s->images->status(id); if(!value) throw std::runtime_error(value.error()); s->scratch=std::move(*value); }
    sc_lua_push(L,s->scratch); return 1;
}
template<int Operation> int finish(lua_State* L) {
    auto* s=script(L);
    if(s->phase!=1&&(Operation==0||s->phase!=4)) return luaL_error(L,"image commit requires update; retry/cancel require update or ui_update");
    const auto id=request(L);
    {
        auto result=Operation==0?s->images->commit(id):Operation==1?s->images->cancel(id):s->images->retry(id);
        if(!result) throw std::runtime_error(result.error());
    }
    lua_pushboolean(L,true); return 1;
}
int stats(lua_State* L) {
    auto* s=script(L);
    if(lua_gettop(L)||!s->image_cache) return luaL_error(L,"images.stats expects no arguments and requires a host");
    { const auto value=s->image_cache->statistics();
      s->scratch=ScValue{ScValue::Object{{"capacity",ScValue{double(value.capacity)}},{"budget_bytes",ScValue{double(value.budget_bytes)}},
        {"resident_bytes",ScValue{double(value.resident_bytes)}},{"resident",ScValue{double(value.resident)}},
        {"pinned",ScValue{double(value.pinned)}},{"pending",ScValue{double(value.pending)}}}}; }
    sc_lua_push(L,s->scratch); return 1;
}
constexpr ScLuaParameter names[]={{"names","string[]",true,"Distinct declared image names or declared paths (names take precedence). Includes bound normal maps. Eager images are validated but not cached; aliases normalize by resource name. Replaces the complete streamed-image set; empty unloads it."}};
constexpr ScLuaParameter ticket[]={{"request","integer",true,"Room-local image transaction ID; invalid after commit, cancel or room exit.",nullptr,1,4503599627370495.0}};
const ScLuaContract prepare_contract{names,"integer",ScLuaPhases::update,"128 names; one room request; shared cache 128 images / 128 MiB",nullptr,"streaming"};
const ScLuaContract reload_contract{{},"integer",ScLuaPhases::update,"Current and replacement revisions share the 128-image / 128 MiB cache budget",nullptr,"streaming"};
const ScLuaContract status_contract{ticket,"ScImageRequestStatus",ScLuaPhases::read,nullptr,nullptr,"streaming"};
const ScLuaContract commit_contract{ticket,"boolean",ScLuaPhases::update,nullptr,nullptr,"streaming"};
const ScLuaContract recover_contract{ticket,"boolean",ScLuaPhases::update_ui,nullptr,nullptr,"streaming"};
const ScLuaContract stats_contract{{},"ScImageCacheStats",ScLuaPhases::read,nullptr,nullptr,"streaming"};
const ScLuaApi api[]={
    {"prepare",sc_lua_guard<prepare>,"prepare(names) -> request","Stage a replacement image set. The next fixed boundary waits for CPU decode and native upload, preserving current images.",&prepare_contract},
    {"reload",sc_lua_guard<reload>,"reload() -> request","Reread the complete committed streamed-image set into private revisions of the same dimensions. Commit publishes; cancel/failure preserves old pixels and future cache lookups.",&reload_contract},
    {"status",sc_lua_guard<status>,"status(request) -> status","Read host-observed pending/ready/failed state; no worker timing is published to gameplay.",&status_contract},
    {"commit",sc_lua_guard<finish<0>>,"commit(request) -> true","Publish a ready set and release the previous room references.",&commit_contract},
    {"cancel",sc_lua_guard<finish<1>>,"cancel(request) -> true","Discard candidate images, preserving the current set; cancels the host wait.",&recover_contract},
    {"retry",sc_lua_guard<finish<2>>,"retry(request) -> true","Retry a failed decode/upload without changing request ID or current images.",&recover_contract},
    {"stats",sc_lua_guard<stats>,"stats() -> stats","Read application CPU cache reservations and references; excludes GPU/workspace overhead.",&stats_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
#endif
}
void sc_script_images_register(lua_State* L) {
    lua_newtable(L);
#ifdef SC_HAS_STREAMING
    sc_api_register(L,api); lua_pushboolean(L,true);
#else
    lua_pushboolean(L,false);
#endif
    lua_setfield(L,-2,"available"); lua_setfield(L,-2,"images");
}
void sc_script_images_describe() {
#ifdef SC_HAS_STREAMING
    sc_api_describe(api,"sc.images.");
#endif
}
ScValue sc_script_images_contracts() {
    ScValue::Object types;
    auto field=[](const char* name,const char* type,const char* description,bool required=true) {
        return ScValue{ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"description",ScValue{std::string(description)}},{"required",ScValue{required}},{"readonly",ScValue{true}}}};
    };
    auto option=[&](const char* name,const char* type,const char* description) {
        auto result=field(name,type,description,false);
        std::get<ScValue::Object>(result.data)["readonly"]=ScValue{false}; return result;
    };
    auto numeric=[&](const char* name,double minimum,double maximum,bool zero=true) {
        auto result=option(name,"number","Source pixels; finite.");
        auto& object=std::get<ScValue::Object>(result.data);
        object.emplace("minimum",ScValue{minimum}); object.emplace("maximum",ScValue{maximum});
        if(zero) object.emplace("default",ScValue{0.0});
        return result;
    };
    ScValue::Array slices,draw;
    for(const auto* side:{"left","right","top","bottom"}) slices.push_back(numeric(side,0,8192));
    for(const auto* name:{"source_x","source_y","source_w","source_h"}) draw.push_back(numeric(name,0,8192));
    auto angle=numeric("angle",-1000000,1000000);
    std::get<ScValue::Object>(angle.data)["description"]=ScValue{std::string("Radians clockwise around the destination center; zero preserves pixel-aligned drawing.")};
    draw.push_back(std::move(angle));
    for(const auto* name:{"flip_x","flip_y","diagonal","screen"}) {
        auto item=option(name,"boolean","Diagonal exchanges source axes after flips; screen selects logical viewport coordinates.");
        std::get<ScValue::Object>(item.data).emplace("default",ScValue{false}); draw.push_back(std::move(item));
    }
    auto color=option("color","string","#RRGGBB or #RRGGBBAA tint, including opacity.");
    std::get<ScValue::Object>(color.data).emplace("default",ScValue{std::string("#FFFFFFFF")}); draw.push_back(std::move(color));
    auto layer=numeric("layer",-32768,32767,false);
    std::get<ScValue::Object>(layer.data)["type"]=ScValue{std::string("integer")};
    std::get<ScValue::Object>(layer.data)["description"]=ScValue{std::string("Omitted preserves submission order; present joins stable scene sorting and forbids nesting in UI clips.")};
    draw.push_back(std::move(layer));
    draw.push_back(option("material","integer|false","Requires materials capability. Live surface handle or false for builtin shader; omitted inherits image binding."));
    draw.push_back(option("slice","ScImageSlice","Nine-slice source insets. Omitted draws one stretched image; consumes one draw command either way."));
    types.emplace("ScImageSlice",ScValue{ScValue::Object{{"fields",ScValue{std::move(slices)}},
        {"constraints",ScValue{ScValue::Array{ScValue{std::string("Opposing insets must leave a positive center inside the source region (or complete PNG). Corners retain source-pixel size; undersized destinations proportionally shrink opposing borders. Flips and diagonal transform both insets and UVs.")}}}}}});
    types.emplace("ScImageOptions",ScValue{ScValue::Object{{"fields",ScValue{std::move(draw)}},
        {"constraints",ScValue{ScValue::Array{ScValue{std::string("Plain table without unknown keys. Source dimensions both zero mean the full image with zero source origin; otherwise both positive and within declared PNG bounds.")}}}}}});
    ScValue::Array status_fields;
    status_fields.push_back(field("request","integer","Room transaction ID."));
    status_fields.push_back(field("status","'pending'|'ready'|'failed'","Host-observed preparation state."));
    status_fields.push_back(field("count","integer","Candidate resource names; 0..128."));
    status_fields.push_back(field("error","string","Present only when failed.",false));
    types.emplace("ScImageRequestStatus",ScValue{ScValue::Object{{"fields",ScValue{std::move(status_fields)}},
        {"constraints",ScValue{ScValue::Array{ScValue{std::string("Read-only host-observed state; request expires on commit, cancel or room exit.")}}}}}});
    ScValue::Array fields;
    for(const char* name:{"capacity","budget_bytes","resident_bytes","resident","pinned","pending"})
        fields.push_back(field(name,"integer","Application CPU cache counter; pinned counts distinct referenced images, pending includes reserved jobs not yet queued."));
    types.emplace("ScImageCacheStats",ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},
        {"constraints",ScValue{ScValue::Array{ScValue{std::string("CPU cache reservations only; excludes GPU and cancelled in-flight decoder storage.")}}}}}});
    return ScValue{std::move(types)};
}
bool sc_script_images_validate(ScScript& s) {
#ifdef SC_HAS_STREAMING
    const auto& world=*s.world;
    std::bitset<128> missing;
    for(std::size_t i=0;i<world.resources.size();++i) if(world.resources[i].streamed) {
        char path[SC_PATH_MAX*2]; std::snprintf(path,sizeof path,"%s/%s",s.root,world.resources[i].path.c_str());
        if(!s.images||!s.images->path(path)) missing.set(i);
    }
    if(missing.none()) return true;
    const auto unavailable=[&](std::size_t index) {
        if(index>=world.resources.size()||!missing.test(index)) return false;
        std::snprintf(s.error,sizeof s.error,"streamed image is not committed: %s",world.resources[index].name.c_str()); return true;
    };
    const auto path_missing=[&](std::string_view path) {
        for(std::size_t i=0;i<world.resources.size();++i)
            if(world.resources[i].path==path&&unavailable(i)) return true;
        return false;
    };
    const auto surface_missing=[&](std::string_view path) {
        if(path_missing(path)) return true;
#ifdef SC_HAS_ADVANCED_RENDER
        if(s.lighting.normal_maps) if(const auto* normal=s.lighting.normal_maps->find(path))
            return path_missing(normal);
#endif
        return false;
    };
    for(const auto& entity:world.entities) if(entity.alive&&entity.sprite[0]&&surface_missing(entity.sprite)) return false;
    for(const auto& tile:world.tile_graphics) if(surface_missing(tile.image)) return false;
    for(int i=0;i<world.draw_count;++i) {
        const auto& draw=world.draws[static_cast<std::size_t>(i)];
        if(draw.kind==SC_DRAW_IMAGE&&surface_missing(draw.text)) return false;
    }
    if(const auto* p=world.projectiles.get()) for(std::size_t i=0;i<p->count;++i)
        if(p->sprite[i]&&unavailable(p->sprites[p->sprite[i]-1].resource)) return false;
    const auto& p=world.particles;
    for(std::size_t i=0;i<p.count;++i) if(p.emitter[i]&&unavailable(p.definitions()[p.emitter[i]-1].image)) return false;
#else
    (void)s;
#endif
    return true;
}

bool sc_script_images_preload(ScScript& s) {
    if(std::holds_alternative<std::monostate>(s.preload_images.data)) return true;
    try {
#ifdef SC_HAS_STREAMING
        if(!s.image_cache) throw std::runtime_error("preload_images requires an application image cache");
        auto images=resolve_images(s,s.preload_images);
        s.images=std::make_unique<ScRoomImages>(*s.image_cache,s.world->epoch);
        auto request=s.images->prepare(std::move(images));
        if(!request) throw std::runtime_error(request.error());
        s.initial_images=*request;
#else
        throw std::runtime_error("preload_images requires SHINY_STREAMING=ON");
#endif
        s.preload_images=ScValue{};
        return true;
    } catch(const std::exception& error) {
        std::snprintf(s.error,sizeof s.error,"scene.preload_images: %s",error.what()); return false;
    }
}
