#include "script_projectiles.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/projectiles.h"
#include <cmath>
#include <cstring>
#include <string_view>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"operation requires load/init/update"); }
void arguments(lua_State* L,int minimum,int maximum) {
    const int n=lua_gettop(L);
    if(n<minimum||n>maximum) luaL_error(L,"invalid projectile argument count");
}
lua_Integer integer(lua_State* L,int index,lua_Integer minimum,lua_Integer maximum) {
    int valid=0; const auto n=lua_tointegerx(L,index,&valid);
    if(lua_type(L,index)!=LUA_TNUMBER||!valid||n<minimum||n>maximum)
        luaL_error(L,"projectile argument %d requires an integer in [%I,%I]",index,minimum,maximum);
    return n;
}
struct NumberField { const char* name; float ScProjectileSpec::*member; double minimum,maximum; const char* description; };
constexpr NumberField numbers[]={
    {"x",&ScProjectileSpec::x,-1e6,1e6,"Initial center X in world pixels."},
    {"y",&ScProjectileSpec::y,-1e6,1e6,"Initial center Y in world pixels."},
    {"vx",&ScProjectileSpec::vx,-1e6,1e6,"Horizontal velocity in pixels/second."},
    {"vy",&ScProjectileSpec::vy,-1e6,1e6,"Vertical velocity in pixels/second."},
    {"ax",&ScProjectileSpec::ax,-1e6,1e6,"Horizontal acceleration in pixels/second squared."},
    {"ay",&ScProjectileSpec::ay,-1e6,1e6,"Vertical acceleration in pixels/second squared."},
    {"radius",&ScProjectileSpec::radius,.001,256,"Circular sweep radius, independent of display size."},
    {"life",&ScProjectileSpec::life,.001,3600,"Lifetime in seconds."},
};
int configure(lua_State* L) {
    arguments(L,0,1);
    if(script(L)->phase!=0) return luaL_error(L,"projectiles.configure requires initialization");
    const auto capacity=lua_isnoneornil(L,1)?static_cast<lua_Integer>(script(L)->projectile_limit):integer(L,1,1,65536);
    if(script(L)->projectile_limit==0) return luaL_error(L,"projectiles disabled by project.limits.projectiles=0");
    if(capacity<1||static_cast<std::uint64_t>(capacity)>script(L)->projectile_limit)
        return luaL_error(L,"projectile capacity outside project limit (requested=%I, limit=%I)",capacity,static_cast<lua_Integer>(script(L)->projectile_limit));
    auto* w=script(L)->world;
    if(w->projectiles) return luaL_error(L,"projectiles already configured");
    auto pool=std::make_unique<ScProjectiles>(static_cast<std::size_t>(capacity));
    if(w->presentation) pool->previous_display.resize(static_cast<std::size_t>(capacity));
    std::vector<ScProjectileSpec> batch(static_cast<std::size_t>(capacity));
    script(L)->projectile_batch=std::move(batch);
    w->projectiles.reset(pool.release());
    return 0;
}
void projectile_table(lua_State* L,int index) {
    luaL_checktype(L,index,LUA_TTABLE);
    if(lua_getmetatable(L,index)) luaL_error(L,"projectile data tables cannot have metatables");
}
double projectile_number(lua_State* L,double minimum,double maximum) {
    if(lua_type(L,-1)!=LUA_TNUMBER) luaL_error(L,"projectile field requires a number");
    const double n=lua_tonumber(L,-1);
    if(!std::isfinite(n)||n<minimum||n>maximum) luaL_error(L,"projectile number outside allowed range");
    return n;
}
int spawn(lua_State* L) {
    arguments(L,1,1);
    mutable_phase(L); auto* s=script(L); auto* pool=s->world->projectiles.get();
    if(!pool) return luaL_error(L,"configure projectiles during init first");
    projectile_table(L,1);
    const auto length=lua_rawlen(L,1);
    if(length>pool->x.size()-pool->count) return luaL_error(L,
        "projectile capacity exhausted (used=%I, requested=%I, capacity=%I)",
        static_cast<lua_Integer>(pool->count),static_cast<lua_Integer>(length),static_cast<lua_Integer>(pool->x.size()));
    std::size_t keys=0;
    lua_pushnil(L);
    while(lua_next(L,1)) {
        if(!lua_isinteger(L,-2)||lua_tointeger(L,-2)<1||static_cast<lua_Unsigned>(lua_tointeger(L,-2))>length)
            return luaL_error(L,"projectiles.spawn expects a dense array");
        ++keys; lua_pop(L,1);
    }
    if(keys!=length) return luaL_error(L,"projectiles.spawn expects a dense array");
    for(std::size_t i=0;i<length;++i) {
        lua_rawgeti(L,1,static_cast<lua_Integer>(i+1)); projectile_table(L,-1);
        auto& p=s->projectile_batch[i]; p=ScProjectileSpec{};
        lua_pushnil(L);
        while(lua_next(L,-2)) {
            if(lua_type(L,-2)!=LUA_TSTRING) return luaL_error(L,"projectile fields require string keys");
            std::size_t size{}; const char* text=lua_tolstring(L,-2,&size);
            if(size==0||size>128||std::memchr(text,0,size)) return luaL_error(L,"invalid projectile field name");
            const std::string_view key(text,size);
            bool numeric=false;
            for(const auto& field:numbers) if(key==field.name) {
                p.*field.member=static_cast<float>(projectile_number(L,field.minimum,field.maximum));numeric=true;break;
            }
            if(numeric) { lua_pop(L,1); continue; }
            if(key=="mask"||key=="color"||key=="sprite") {
                const double n=projectile_number(L,0,key=="sprite"?64:UINT32_MAX);
                if(std::floor(n)!=n) return luaL_error(L,"projectile mask/color/sprite require integers");
                if(key=="mask") p.mask=static_cast<std::uint32_t>(n);
                else if(key=="color") p.color=static_cast<std::uint32_t>(n);
                else p.sprite=static_cast<std::uint8_t>(n);
            } else if(key=="terrain"||key=="piercing") {
                luaL_checktype(L,-1,LUA_TBOOLEAN);
                if(key=="terrain") p.terrain=lua_toboolean(L,-1)!=0;
                else p.piercing=lua_toboolean(L,-1)!=0;
            } else return luaL_error(L,"unknown projectile field '%s' at batch item %I",text,static_cast<lua_Integer>(i+1));
            lua_pop(L,1);
        }
        lua_pop(L,1);
    }
    // Allocate every Lua result before committing. Only borrowed/POD locals span
    // these calls, so allocation errors leave live bullets and IDs untouched.
    lua_createtable(L,static_cast<int>(length),0);
    for(std::size_t i=0;i<length;++i) {
        lua_pushinteger(L,static_cast<lua_Integer>(pool->next_id+i));
        lua_rawseti(L,-2,static_cast<lua_Integer>(i+1));
    }
    pool->spawn_batch(std::span<const ScProjectileSpec>{s->projectile_batch.data(),length});
    return 1;
}
int hits(lua_State* L) {
    arguments(L,0,0);
    const auto* pool=script(L)->world->projectiles.get();
    lua_createtable(L,pool?static_cast<int>(pool->hits.size()):0,0);
    if(pool) for(std::size_t i=0;i<pool->hits.size();++i) {
        // Borrowed POD only across allocating Lua calls: longjmp cannot skip owners.
        const auto& hit=pool->hits[i];
        lua_createtable(L,0,5);
        lua_pushinteger(L,static_cast<lua_Integer>(hit.projectile)); lua_setfield(L,-2,"projectile");
        lua_pushinteger(L,static_cast<lua_Integer>(hit.target)); lua_setfield(L,-2,"target");
        lua_pushnumber(L,hit.fraction); lua_setfield(L,-2,"fraction");
        lua_pushnumber(L,hit.x); lua_setfield(L,-2,"x");
        lua_pushnumber(L,hit.y); lua_setfield(L,-2,"y");
        lua_rawseti(L,-2,static_cast<lua_Integer>(i+1));
    }
    return 1;
}
int count(lua_State* L) { arguments(L,0,0); auto* p=script(L)->world->projectiles.get(); lua_pushinteger(L,p?static_cast<lua_Integer>(p->count):0); return 1; }
int projectile_stats(lua_State* L) {
    arguments(L,0,0);
    auto* s=script(L); auto* p=s->world->projectiles.get();
    lua_createtable(L,0,5);
    lua_pushinteger(L,static_cast<lua_Integer>(s->projectile_limit)); lua_setfield(L,-2,"limit");
    lua_pushinteger(L,p?static_cast<lua_Integer>(p->x.size()):0); lua_setfield(L,-2,"capacity");
    lua_pushinteger(L,p?static_cast<lua_Integer>(p->count):0); lua_setfield(L,-2,"used");
    lua_pushinteger(L,p?static_cast<lua_Integer>(p->x.size()-p->count):0); lua_setfield(L,-2,"available");
    lua_pushinteger(L,p?static_cast<lua_Integer>(p->sprite_count):0); lua_setfield(L,-2,"sprites");
    return 1;
}
int clear(lua_State* L) { arguments(L,0,0); mutable_phase(L); if(auto* p=script(L)->world->projectiles.get()) p->clear(); return 0; }
int projectile_sprite(lua_State* L) {
    arguments(L,5,7);
    auto* s=script(L); auto* pool=s->world->projectiles.get();
    if(s->phase!=0||!pool) return luaL_error(L,"projectile sprites require configuration during initialization");
    if(lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"projectile resource requires a name string");
    std::size_t size{}; const char* name=lua_tolstring(L,1,&size);
    if(size==0||size>127||std::memchr(name,0,size)) return luaL_error(L,"projectile resource requires 1..127 bytes without NUL");
    const auto x=integer(L,2,0,8191),y=integer(L,3,0,8191),w=integer(L,4,1,8192),h=integer(L,5,1,8192);
    for(int index=6;index<=7;++index) if(!lua_isnoneornil(L,index)&&lua_type(L,index)!=LUA_TNUMBER)
        return luaL_error(L,"projectile display size requires a number");
    const auto width=luaL_optnumber(L,6,static_cast<lua_Number>(w)),height=luaL_optnumber(L,7,static_cast<lua_Number>(h));
    if(x<0||y<0||w<1||h<1||w>8192||h>8192||x>8192-w||y>8192-h||
       !std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||width>4096||height>4096)
        return luaL_error(L,"invalid projectile sprite bounds");
    for(std::size_t i=0;i<s->world->resources.size();++i) {
        const auto& resource=s->world->resources[i];
        if(resource.name!=name||resource.type!="image") continue;
        if(resource.image_width==0||resource.image_height==0) return luaL_error(L,"projectile atlas requires a declared PNG image");
        if(x+w>resource.image_width||y+h>resource.image_height) return luaL_error(L,"projectile sprite region exceeds PNG dimensions");
        auto id=pool->add_sprite({static_cast<std::uint16_t>(i),static_cast<int>(x),static_cast<int>(y),static_cast<int>(w),static_cast<int>(h),
                                 static_cast<float>(width),static_cast<float>(height)});
        lua_pushinteger(L,id); return 1;
    }
    return luaL_error(L,"unknown projectile image resource");
}
constexpr ScLuaParameter projectile_configure_parameters[]={{"capacity","integer|nil",false,"Omitted/nil uses project.limits.projectiles; must not exceed that limit.",nullptr,1,65536}};
constexpr ScLuaParameter spawn_parameters[]={{"specs","ScProjectileSpec[]",true,"Dense plain array; whole batch is preflighted before commit."}};
constexpr ScLuaParameter sprite_parameters[]={
    {"resource","string",true,"Declared PNG resource name, 1..127 bytes without NUL."},
    {"x","integer",true,"Source left pixel.",nullptr,0,8191},
    {"y","integer",true,"Source top pixel.",nullptr,0,8191},
    {"w","integer",true,"Source width; x+w must fit the PNG and 8192.",nullptr,1,8192},
    {"h","integer",true,"Source height; y+h must fit the PNG and 8192.",nullptr,1,8192},
    {"width","number|nil",false,"Finite and strictly positive, at most 4096; omitted/nil defaults to w.",nullptr,0,4096},
    {"height","number|nil",false,"Finite and strictly positive, at most 4096; omitted/nil defaults to h.",nullptr,0,4096},
};
constexpr ScLuaContract projectile_configure_contract{projectile_configure_parameters,nullptr,ScLuaPhases::initialize,"project.limits.projectiles: default 32768; range 0..65536"};
constexpr ScLuaContract projectile_stats_contract{{},"ScProjectileStats",ScLuaPhases::read};
constexpr ScLuaContract spawn_contract{spawn_parameters,"integer[]",ScLuaPhases::mutate,"Configured projectile capacity; monotonically increasing room-local IDs below 2^52, not entity handles."};
constexpr ScLuaContract sprite_contract{sprite_parameters,"integer",ScLuaPhases::initialize,"64 registered sprites after configuration; return ID 1..64."};
constexpr ScLuaContract hits_contract{{},"ScProjectileHit[]",ScLuaPhases::read};
constexpr ScLuaContract count_contract{{},"integer",ScLuaPhases::read};
constexpr ScLuaContract clear_contract{{},nullptr,ScLuaPhases::mutate};
const ScLuaApi projectile_api[]={
    {"sprite",sc_lua_guard<projectile_sprite>,"sprite(resource,x,y,w,h,width?,height?) -> id","Register a declared PNG atlas region during init, after configure. Up to 64; display size defaults to source size, max 4096. Spawn sprite=0 uses the radius-sized color quad. Draws retain projectile ID order.",&sprite_contract},
    {"configure",sc_lua_guard<configure>,"configure(capacity?)","Allocate once during initialization, up to project.limits.projectiles (default 32768, 0 disables). Omitted capacity uses that limit; no storage before configuration.",&projectile_configure_contract},
    {"stats",sc_lua_guard<projectile_stats>,"stats() -> {limit,capacity,used,available,sprites}","Read configured project budget, allocated pool capacity, live count, free slots and registered sprite count; safe in draw.",&projectile_stats_contract},
    {"spawn",sc_lua_guard<spawn>,"spawn(specs) -> ids","Validate a dense plain-table batch within allocated capacity, using preallocated staging. Empty batch returns an empty array. Numeric RGBA/mask, strict field types; Lua result allocation completes before native commit.",&spawn_contract},
    {"hits",sc_lua_guard<hits>,"hits() -> hits","Copy last simulated-step hits in projectile/fraction/target order. Target zero is terrain; clear removes hits too. Empty when unconfigured.",&hits_contract},
    {"count",sc_lua_guard<count>,"count() -> integer","Current live projectile count, or zero when unconfigured.",&count_contract},
    {"clear",sc_lua_guard<clear>,"clear()","Remove bullets and hits without recycling IDs or sprite registrations; no-op before configuration.",&clear_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_projectiles_register(lua_State* L) { lua_newtable(L);sc_api_register(L,projectile_api);lua_setfield(L,-2,"projectiles"); }
void sc_script_projectiles_describe() { sc_api_describe(projectile_api,"sc.projectiles."); }
ScValue sc_script_projectiles_contracts() {
    using V=ScValue; V::Object types; V::Array fields;
    constexpr ScProjectileSpec initial{};
    auto number=[&](const char* name,const char* type,double value,double minimum,double maximum,const char* description) {
        fields.emplace_back(V::Object{{"name",V{std::string(name)}},{"type",V{std::string(type)}},
            {"required",V{false}},{"default",V{value}},{"minimum",V{minimum}},{"maximum",V{maximum}},
            {"finite",V{true}},{"description",V{std::string(description)}}});
    };
    for(const auto& f:numbers) number(f.name,"number",initial.*f.member,f.minimum,f.maximum,f.description);
    number("mask","integer",initial.mask,0,UINT32_MAX,"Entity category mask; zero skips targets. Terrain uses the independent terrain flag.");
    number("color","integer",initial.color,0,UINT32_MAX,"Packed numeric 0xRRGGBBAA, not a hex string.");
    number("sprite","integer",initial.sprite,0,64,"Registered room-local projectile sprite ID; zero is a radius-sized colored quad.");
    auto boolean=[&](const char* name,bool value,const char* description) {
        fields.emplace_back(V::Object{{"name",V{std::string(name)}},{"type",V{std::string("boolean")}},
            {"required",V{false}},{"default",V{value}},{"description",V{std::string(description)}}});
    };
    boolean("terrain",initial.terrain,"Enable terrain sweep independently of mask.");
    boolean("piercing",initial.piercing,"Continue past entity hits; blocking terrain still stops the projectile.");
    V::Array constraints;
    for(const char* text:{"Plain tables only; fields use strict types, no unknown fields or metatables.",
        "Batch must be a dense array within remaining capacity. Failure preserves live projectiles and next ID.",
        "Sprite must already be registered; world motion uses collision radius, not display width/height.",
        "IDs are room-local projectile sequence numbers, not generation-checked entity handles or save references."})
        constraints.emplace_back(std::string(text));
    types.emplace("ScProjectileSpec",V::Object{{"fields",V{std::move(fields)}},{"constraints",V{std::move(constraints)}},
        {"unknown_fields",V{std::string("reject")}}});
    auto readonly=[&](const char* name,const char* type,const char* description,double minimum,double maximum) {
        fields.emplace_back(V::Object{{"name",V{std::string(name)}},{"type",V{std::string(type)}},
            {"required",V{true}},{"readonly",V{true}},{"minimum",V{minimum}},{"maximum",V{maximum}},
            {"description",V{std::string(description)}}});
    };
    fields.clear();
    readonly("projectile","integer","Room-local projectile sequence number.",1,static_cast<double>(SC_ID_MAX));
    readonly("target","ScEntityId","Target entity handle; zero means terrain.",0,static_cast<double>(SC_ID_MAX));
    readonly("fraction","number","Sweep fraction within the completed simulation step.",0,1);
    readonly("x","number","Projectile center X at impact, in world pixels.",-1e6,1e6);
    readonly("y","number","Projectile center Y at impact, in world pixels.",-1e6,1e6);
    types.emplace("ScProjectileHit",V::Object{{"fields",V{std::move(fields)}},{"constraints",V{V::Array{
        V{std::string("Independent snapshots, ordered by projectile, fraction and target. Paused simulation retains the last step; clear removes results.")}}}}});
    fields.clear();
    readonly("limit","integer","Project capacity ceiling; zero disables allocation.",0,65536);
    readonly("capacity","integer","Allocated capacity, zero before configure.",0,65536);
    readonly("used","integer","Active projectiles.",0,65536);
    readonly("available","integer","Allocated capacity minus active count.",0,65536);
    readonly("sprites","integer","Registered sprite regions, retained by clear.",0,64);
    types.emplace("ScProjectileStats",V::Object{{"fields",V{std::move(fields)}},{"constraints",V{V::Array{}}}});
    return V{std::move(types)};
}
