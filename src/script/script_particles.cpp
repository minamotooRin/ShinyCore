#include "script_particles.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include <cmath>
#include <stdexcept>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
double number(const ScValue& v,const std::string& path,double lo,double hi) {
    const auto* value=std::get_if<double>(&v.data);
    if(!value||!std::isfinite(*value)||*value<lo||*value>hi)
        throw std::invalid_argument(path+": number outside allowed range");
    return *value;
}
const ScValue::Object& object(const ScValue& v,const char* path) {
    const auto* value=std::get_if<ScValue::Object>(&v.data);
    if(!value) throw std::invalid_argument(std::string(path)+": expected object");
    return *value;
}
int define(lua_State* L) {
    auto* s=script(L);
    if(s->phase!=0) return luaL_error(L,"particles.define requires initialization");
    std::uint8_t id{};
    {
        auto value=sc_lua_read(L,1); if(!value) throw std::invalid_argument(value.error());
        ScParticleEmitter spec;
        for(const auto& [key,item]:object(*value,"emitter")) {
            if(key=="blend") {
                const auto* mode=std::get_if<std::string>(&item.data);
                if(!mode||(*mode!="alpha"&&*mode!="additive")) throw std::invalid_argument("emitter.blend: expected alpha or additive");
                spec.additive=*mode=="additive";
            } else if(key=="texture") {
                const auto& region=object(item,"emitter.texture");
                if(region.size()!=5||!region.contains("resource")||!region.contains("x")||!region.contains("y")||!region.contains("w")||!region.contains("h"))
                    throw std::invalid_argument("emitter.texture requires only resource, x, y, w and h");
                auto crop=[&](const char* field,int minimum) {
                    const auto n=number(region.at(field),std::string("emitter.texture.")+field,minimum,8192);
                    if(n!=std::floor(n)) throw std::invalid_argument("emitter.texture: crop requires integers");
                    return static_cast<int>(n);
                };
                spec.x=crop("x",0); spec.y=crop("y",0); spec.w=crop("w",1); spec.h=crop("h",1);
                const auto* resource=std::get_if<std::string>(&region.at("resource").data);
                if(!resource) throw std::invalid_argument("emitter.texture.resource: expected declared PNG resource name");
                for(std::size_t i=0;i<s->world->resources.size();++i) {
                    const auto& image=s->world->resources[i];
                    if(image.name!=*resource||image.type!="image") continue;
                    if(!image.image_width||!image.image_height) throw std::invalid_argument("emitter.texture: resource must be PNG");
                    if(spec.x+spec.w>image.image_width||spec.y+spec.h>image.image_height)
                        throw std::invalid_argument("emitter.texture: region exceeds PNG dimensions");
                    spec.image=static_cast<std::uint16_t>(i); break;
                }
                if(spec.image==128) throw std::invalid_argument("emitter.texture: unknown image resource "+*resource);
            } else if(key=="curve") {
                const auto* curve=std::get_if<ScValue::Array>(&item.data);
                if(!curve||curve->size()<2||curve->size()>8) throw std::invalid_argument("emitter.curve: expected 2..8 keys");
                spec.keys=curve->size();
                for(std::size_t i=0;i<curve->size();++i) {
                    const auto& fields=object((*curve)[i],"emitter.curve key");
                    if(fields.size()!=3||!fields.contains("time")||!fields.contains("size")||!fields.contains("color"))
                        throw std::invalid_argument("emitter.curve key requires only time, size and numeric RGBA color");
                    const std::string path="emitter.curve["+std::to_string(i+1)+"].";
                    const double color=number(fields.at("color"),path+"color",0,UINT32_MAX);
                    if(color!=std::floor(color)) throw std::invalid_argument(path+"color: expected integer");
                    spec.curve[i]={static_cast<float>(number(fields.at("time"),path+"time",0,1)),
                        static_cast<float>(number(fields.at("size"),path+"size",0,4096)),static_cast<std::uint32_t>(color)};
                }
            } else {
                const auto n=static_cast<float>(number(item,"emitter."+key,-1e6,1e6));
                if(key=="speed_min") spec.speed_min=n;
                else if(key=="speed_max") spec.speed_max=n;
                else if(key=="life_min") spec.life_min=n;
                else if(key=="life_max") spec.life_max=n;
                else if(key=="angle_min") spec.angle_min=n;
                else if(key=="angle_max") spec.angle_max=n;
                else if(key=="gravity") spec.gravity=n;
                else throw std::invalid_argument("unknown emitter field: "+key);
            }
        }
        auto result=s->world->particles.define(spec);
        if(!result) throw std::invalid_argument(result.error());
        id=*result;
    }
    lua_pushinteger(L,(static_cast<lua_Integer>(s->world->epoch)<<8)|id); return 1;
}
int burst(lua_State* L) {
    auto* s=script(L); auto* w=s->world;
    if(s->phase>=2) return luaL_error(L,"particles.burst requires init or update");
    const auto id=luaL_checkinteger(L,1),amount=luaL_checkinteger(L,4);
    const double x=luaL_checknumber(L,2),y=luaL_checknumber(L,3);
    if(id<0||(id>>8)!=w->epoch||(id&255)<1||(id&255)>64) return luaL_error(L,"invalid particle emitter handle");
    if(amount<0||amount>65536||!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1e6||std::fabs(y)>1e6)
        return luaL_error(L,"invalid particle burst position or count");
    const char* error=nullptr;
    { auto result=w->particles.burst(w->visual_rng,static_cast<std::uint8_t>(id&255),static_cast<float>(x),static_cast<float>(y),static_cast<std::size_t>(amount));
      if(!result) error=result.error(); }
    if(error) return luaL_error(L,"%s (used=%I, requested=%I, capacity=%I)",error,
        static_cast<lua_Integer>(w->particles.count),amount,static_cast<lua_Integer>(w->particles.capacity()));
    lua_pushinteger(L,amount); return 1;
}
int stats(lua_State* L) {
    const auto& pool=script(L)->world->particles;
    lua_createtable(L,0,4);
    lua_pushinteger(L,static_cast<lua_Integer>(pool.capacity())); lua_setfield(L,-2,"capacity");
    lua_pushinteger(L,static_cast<lua_Integer>(pool.count)); lua_setfield(L,-2,"used");
    lua_pushinteger(L,static_cast<lua_Integer>(pool.capacity()-pool.count)); lua_setfield(L,-2,"available");
    lua_pushinteger(L,static_cast<lua_Integer>(pool.emitter_count())); lua_setfield(L,-2,"emitters");
    return 1;
}
constexpr ScLuaParameter definition[]={{"spec","ScParticleEmitterSpec"}};
constexpr ScLuaParameter burst_parameters[]={{"emitter","integer"},{"x","number"},{"y","number"},{"count","integer"}};
constexpr ScLuaContract define_contract{definition,"integer",ScLuaPhases::initialize,"64 immutable templates per room; 2..8 curve keys"};
constexpr ScLuaContract burst_contract{burst_parameters,"integer",ScLuaPhases::mutate,"project.limits.particles"};
constexpr ScLuaContract stats_contract{{},"{capacity:integer,used:integer,available:integer,emitters:integer}",ScLuaPhases::read};
const ScLuaApi api[]={
    {"define",sc_lua_guard<define>,"define(spec) -> emitter","Register speed/lifetime ranges, angle sector, gravity factor and linear size/RGBA lifetime curve; room-qualified handle.",&define_contract},
    {"burst",sc_lua_guard<burst>,"burst(emitter,x,y,count) -> count","Atomically emit 0..65536 particles; no gameplay RNG consumption. Stale room handles fail.",&burst_contract},
    {"stats",sc_lua_guard<stats>,"stats() -> usage","Read allocated capacity, active count, available slots and emitter templates.",&stats_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_particles_register(lua_State* L) { lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"particles"); }
void sc_script_particles_describe() { sc_api_describe(api,"sc.particles."); }
ScValue sc_script_particles_contracts() {
    auto field=[](const char* name,const char* type,bool required,const char* description) {
        return ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{required}},{"description",ScValue{std::string(description)}}};
    };
    auto numeric=[&](const char* name,double value,double lo,double hi,const char* description) {
        auto result=field(name,"number",false,description);
        result.emplace("default",ScValue{value}); result.emplace("minimum",ScValue{lo}); result.emplace("maximum",ScValue{hi});
        return ScValue{std::move(result)};
    };
    const ScParticleEmitter defaults;
    ScValue::Array fields{
        numeric("speed_min",defaults.speed_min,0,1e6,"Minimum speed, pixels/second; must not exceed speed_max."),
        numeric("speed_max",defaults.speed_max,0,1e6,"Maximum speed, pixels/second."),
        numeric("life_min",defaults.life_min,.001,60,"Minimum lifetime in seconds; must not exceed life_max."),
        numeric("life_max",defaults.life_max,.001,60,"Maximum lifetime in seconds."),
        numeric("angle_min",defaults.angle_min,-1e6,1e6,"Minimum radians; 0 points right, pi/2 down; must not exceed angle_max."),
        numeric("angle_max",defaults.angle_max,-1e6,1e6,"Maximum radians."),
        numeric("gravity",defaults.gravity,-10,10,"World gravity multiplier."),
        ScValue{field("curve","ScParticleCurveKey[]",false,"2..8 keys from time 0 to 1, strictly increasing. Default white size 2 at 0 to transparent white size 0 at 1.")},
        ScValue{field("texture","ScParticleTexture",false,"Declared PNG region; omitted means a white square. Curve size sets longest side, preserving source aspect ratio. Position anchors top-left.")},
        ScValue{field("blend","'alpha'|'additive'",false,"Default alpha. Additive uses source alpha for color intensity; draw order is never regrouped by blend or texture.")}
    };
    ScValue::Array keys{
        ScValue{field("time","number",true,"Normalized lifetime 0..1; endpoint keys must be 0 and 1.")},
        ScValue{field("size","number",true,"Square side length in pixels, 0..4096.")},
        ScValue{field("color","integer",true,"Numeric RGBA 0..4294967295; each channel interpolates linearly with byte rounding.")}
    };
    ScValue::Array region{
        ScValue{field("resource","string",true,"Declared PNG resource name, not a file path.")},
        ScValue{field("x","integer",true,"Source left in pixels, 0..8192.")},
        ScValue{field("y","integer",true,"Source top in pixels, 0..8192.")},
        ScValue{field("w","integer",true,"Source width in pixels, 1..8192; entire region must fit image.")},
        ScValue{field("h","integer",true,"Source height in pixels, 1..8192; entire region must fit image.")}
    };
    auto type=[](ScValue::Array entries,const char* constraint) {
        return ScValue{ScValue::Object{{"fields",ScValue{std::move(entries)}},
            {"constraints",ScValue{ScValue::Array{ScValue{std::string(constraint)}}}}}};
    };
    return ScValue{ScValue::Object{
        {"ScParticleEmitterSpec",type(std::move(fields),"Immutable copied template, defined during initialization. Unknown fields and nonfinite numbers fail; at most 64 per room.")},
        {"ScParticleCurveKey",type(std::move(keys),"All three fields required. Sizes and RGBA are interpolated at fixed update; no additional lifetime alpha factor.")},
        {"ScParticleTexture",type(std::move(region),"No extra fields. Bounded PNG dimensions are available headlessly; graphical preflight additionally decodes the image.")}
    }};
}
