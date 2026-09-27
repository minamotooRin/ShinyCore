#include "script_lighting.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include <cmath>
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace {
ScScript& script(lua_State* L) { return **static_cast<ScScript**>(lua_getextraspace(L)); }
constexpr const char* occluder_names[]={"body","bounds","shape","none"};
ScEntityId live_entity(lua_State* L) {
    if(lua_type(L,1)!=LUA_TNUMBER) luaL_error(L,"occluder requires an integer entity handle");
    const auto id=luaL_checkinteger(L,1);
    if(id<1||id>static_cast<lua_Integer>(SC_ID_MAX)||!sc_entity(script(L).world,static_cast<ScEntityId>(id)))
        luaL_error(L,"occluder requires a live entity handle");
    return static_cast<ScEntityId>(id);
}
int occluder(lua_State* L) {
    auto& s=script(L);
    if(s.phase>=2) return luaL_error(L,"lighting.occluder requires load/init/update");
    const auto id=live_entity(L);
    if(lua_type(L,2)!=LUA_TSTRING) return luaL_error(L,"occluder mode requires body, bounds, shape or none");
    std::size_t length=0; const char* name=lua_tolstring(L,2,&length);
    std::size_t mode=0;
    while(mode<std::size(occluder_names)&&std::string_view(name,length)!=occluder_names[mode]) ++mode;
    if(mode==std::size(occluder_names)) return luaL_error(L,"occluder mode requires body, bounds, shape or none");
    const auto slot=sc_entity_slot(id);
    if(slot>=s.lighting.occluder_modes.size()) return luaL_error(L,"occluder controls require the configured room entity pool");
    s.lighting.occluder_modes[slot]={mode?id:0,static_cast<ScOccluderMode>(mode)};
    return 0;
}
int occluder_mode(lua_State* L) {
    const auto id=live_entity(L);
    const auto mode=sc_occluder_mode(script(L).lighting.occluder_modes,id);
    lua_pushstring(L,occluder_names[static_cast<std::size_t>(mode)]); return 1;
}
struct NumberField { const char* name; float ScPointLight::*member; double minimum,maximum; bool required; const char* description; };
constexpr NumberField point_numbers[]={
    {"x",&ScPointLight::x,-1e6,1e6,true,"World-space light center X."},
    {"y",&ScPointLight::y,-1e6,1e6,true,"World-space light center Y; positive down."},
    {"radius",&ScPointLight::radius,.001,1024,false,"Attenuation radius, pixels."},
    {"height",&ScPointLight::height,.001,4096,false,"Height above the 2D surface in pixels, used only for normal-map diffuse shading; does not change shadow geometry."},
    {"intensity",&ScPointLight::intensity,0,1,false,"Linear intensity multiplied by color alpha; zero does not consume a visible-light slot."},
    {"softness",&ScPointLight::softness,0,32,false,"Source radius, pixels; omitted inherits lighting.configure at submission. Ignored for non-shadow lights."}
};
int point(lua_State* L) {
    auto& s=script(L);
    if(s.phase!=2) return luaL_error(L,"lighting.point requires draw");
    auto value=sc_lua_read(L,1);
    if(!value) throw std::invalid_argument(value.error());
    const auto* fields=std::get_if<ScValue::Object>(&value->data);
    if(!fields) throw std::invalid_argument("point light requires a plain object");
    ScPointLight light; light.softness=s.lighting.softness; light.samples=s.lighting.samples;
    for(const auto& number:point_numbers) if(number.required&&!fields->contains(number.name))
        throw std::invalid_argument(std::string("point light requires ")+number.name);
    for(const auto& [name,item]:*fields) {
        auto number=std::find_if(std::begin(point_numbers),std::end(point_numbers),[&](const auto& field){return name==field.name;});
        if(number!=std::end(point_numbers)) {
            const auto* n=std::get_if<double>(&item.data);
            if(!n||!std::isfinite(*n)||*n<number->minimum||*n>number->maximum) throw std::invalid_argument("invalid point light "+name);
            light.*(number->member)=static_cast<float>(*n);
        } else if(name=="samples"||name=="ignore") {
            const auto* n=std::get_if<double>(&item.data);
            if(!n||!std::isfinite(*n)||*n!=std::floor(*n)||*n<(name=="samples"?1:0)||*n>(name=="samples"?8:double(SC_ID_MAX)))
                throw std::invalid_argument("invalid point light "+name);
            if(name=="samples") light.samples=static_cast<int>(*n);
            else {
                light.ignore=static_cast<ScEntityId>(*n);
                if(light.ignore&&!sc_entity(s.world,light.ignore)) throw std::invalid_argument("point light ignore requires a live entity handle");
            }
        } else if(name=="shadows") {
            const auto* flag=std::get_if<bool>(&item.data);
            if(!flag) throw std::invalid_argument("point light shadows requires boolean");
            light.shadows=*flag;
        } else if(name=="color") {
            const auto* color=std::get_if<std::string>(&item.data);
            if(!color||(color->size()!=7&&color->size()!=9)||(*color)[0]!='#') throw std::invalid_argument("point light color requires #RRGGBB or #RRGGBBAA");
            std::uint32_t rgba{};
            auto [end,error]=std::from_chars(color->data()+1,color->data()+color->size(),rgba,16);
            if(error!=std::errc{}||end!=color->data()+color->size()) throw std::invalid_argument("invalid point light color");
            light.color=color->size()==7?(rgba<<8)|255:rgba;
        } else throw std::invalid_argument("unknown point light field: "+name);
    }
    if(s.lighting.point_count==s.lighting.points.size()) throw std::invalid_argument("point light command capacity exceeded (used=32, requested=1, capacity=32)");
    s.lighting.points[s.lighting.point_count++]=light; return 0;
}
int configure(lua_State* L) {
    auto& s=script(L);
    if(s.phase>=2) return luaL_error(L,"lighting.configure requires load/init/update");
    auto value=sc_lua_read(L,1);
    if(!value) throw std::invalid_argument(value.error());
    auto* fields=std::get_if<ScValue::Object>(&value->data);
    if(!fields) throw std::invalid_argument("lighting options must be a plain object");
    float softness=s.lighting.softness; int samples=s.lighting.samples;
    for(const auto& [name,item]:*fields) {
        const auto* number=std::get_if<double>(&item.data);
        if(!number||!std::isfinite(*number)) throw std::invalid_argument("lighting."+name+": expected a finite number");
        if(name=="softness"&&*number>=0&&*number<=32) softness=static_cast<float>(*number);
        else if(name=="samples"&&*number>=1&&*number<=8&&*number==std::floor(*number)) samples=static_cast<int>(*number);
        else throw std::invalid_argument("invalid lighting option: "+name+" (softness 0..32, samples integer 1..8)");
    }
    s.lighting.softness=softness; s.lighting.samples=samples; s.lighting.presented=false; return 0;
}
int normal(lua_State* L) {
    auto& s=script(L);
    if(s.phase!=0) return luaL_error(L,"lighting.normal requires load/init");
    if(lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING) return luaL_error(L,"lighting.normal requires image resource names");
    std::size_t image_size=0,normal_size=0;
    const char* image=lua_tolstring(L,1,&image_size);
    const char* map=lua_tolstring(L,2,&normal_size);
    auto bound=sc_normal_bind(*s.world,s.lighting,{image,image_size},{map,normal_size});
    if(!bound) throw std::invalid_argument(bound.error());
    return 0;
}
int stats(lua_State* L) {
    auto& s=script(L); const auto& light=s.lighting;
    std::size_t overrides=0;
    for(const auto& entity:s.world->entities)
        if(entity.alive&&sc_occluder_mode(light.occluder_modes,entity.id)!=ScOccluderMode::body) ++overrides;
    using V=ScValue;
    s.scratch=V{V::Object{{"softness",V{double(light.softness)}},{"samples",V{double(light.samples)}},
        {"effective_samples",V{double(light.softness>0?light.samples:1)}},
        {"occluders",V{double(light.occluders)}},{"required_occluders",V{double(light.required_occluders)}},
        {"occluder_capacity",V{double(ScLighting::occluder_capacity)}},{"lights",V{double(light.lights)}},
        {"light_capacity",V{double(ScLighting::light_capacity)}},{"error",V{light.error}},
        {"shadow_lights",V{double(light.shadow_lights)}},{"shadow_capacity",V{double(ScLighting::shadow_capacity)}},
        {"point_commands",V{double(light.point_count)}},
        {"normal_maps",V{double(light.normal_maps?light.normal_maps->count:0)}},{"normal_capacity",V{64.}},
        {"normal_target_bytes",V{double(light.normal_target_bytes)}},{"normal_budget_bytes",V{double(ScLighting::normal_budget_bytes)}},
        {"occluder_overrides",V{double(overrides)}},{"override_capacity",V{double(light.occluder_modes.size())}},
        {"status",V{std::string(!light.error.empty()?"failed":light.presented?"ready":"pending")}}}};
    sc_lua_push(L,s.scratch); return 1;
}
constexpr ScLuaParameter options[]={{"options","{softness?:number,samples?:integer}"}};
constexpr ScLuaParameter occluder_parameters[]={{"id","ScEntityId"},{"mode","'body'|'bounds'|'shape'|'none'"}};
constexpr ScLuaParameter occluder_id[]={{"id","ScEntityId"}};
const ScLuaContract occluder_contract{occluder_parameters,nullptr,ScLuaPhases::mutate,"project.limits.entities; storage allocated while loading",nullptr,"geometry_shadows"};
const ScLuaContract occluder_mode_contract{occluder_id,"'body'|'bounds'|'shape'|'none'",ScLuaPhases::read,nullptr,nullptr,"geometry_shadows"};
constexpr ScLuaParameter point_parameters[]={{"spec","ScPointLight"}};
constexpr ScLuaParameter normal_parameters[]={{"image","string"},{"normal_map","string"}};
const ScLuaContract normal_contract{normal_parameters,nullptr,ScLuaPhases::initialize,"64 image/atlas bindings; matching dimensions; 64 MiB normal-target budget",nullptr,"normal_maps"};
const ScLuaContract point_contract{point_parameters,nullptr,ScLuaPhases::draw,"32 commands per draw; combined visible budget 32 lights / 16 shadow lights",nullptr,"geometry_shadows"};
const ScLuaContract configure_contract{options,nullptr,ScLuaPhases::mutate,"softness 0..32 pixels (default 0); samples 1..8 (default 1)",nullptr,"geometry_shadows"};
const ScLuaContract stats_contract{{},"{softness:number,samples:integer,effective_samples:integer,occluders:integer,required_occluders:integer,occluder_capacity:integer,lights:integer,light_capacity:integer,shadow_lights:integer,shadow_capacity:integer,point_commands:integer,normal_maps:integer,normal_capacity:integer,normal_target_bytes:integer,normal_budget_bytes:integer,occluder_overrides:integer,override_capacity:integer,status:string,error:string}",ScLuaPhases::read,nullptr,nullptr,"geometry_shadows"};
const ScLuaApi api[]={
    {"occluder",sc_lua_guard<occluder>,"occluder(id,mode)","Choose body (default physical filters), bounds (rotated visual rectangle), shape (stored geometry regardless of physical flags), or none. Does not create/change physics. Generation checks prevent slot reuse from inheriting overrides.",&occluder_contract},
    {"occluder_mode",sc_lua_guard<occluder_mode>,"occluder_mode(id) -> mode","Read the current live entity's presentation-only occlusion mode; defaults to body.",&occluder_mode_contract},
    {"normal",sc_lua_guard<normal>,"normal(image,normal_map)","Bind a same-size normal image/atlas during initialization; shared by world images, sprites and tiles. Rebinding a source path replaces its previous mapping atomically.",&normal_contract},
    {"point",sc_lua_guard<point>,"point(spec)","Submit an explicit world point light during draw. Commands clear before every draw; no entity or persistent handle is created. Per-light color, intensity, shadows and quality override inherited settings.",&point_contract},
    {"configure",sc_lua_guard<configure>,"configure(options)","Atomically patch area-light softness and sample count; glows cast geometry shadows excluding their own body. Settings are presentation-only.",&configure_contract},
    {"stats",sc_lua_guard<stats>,"stats() -> table","Read settings, budgets, current/last draw point_commands and last native submission usage. Headless status stays pending and native usage counts stay zero.",&stats_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_lighting_register(lua_State* L) { lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"lighting"); }
void sc_script_lighting_describe() { sc_api_describe(api,"sc.lighting."); }
ScValue sc_script_lighting_contracts() {
    using V=ScValue;
    auto field=[](const char* name,const char* type,bool required,const char* description) {
        return V::Object{{"name",V{std::string(name)}},{"type",V{std::string(type)}},{"required",V{required}},{"description",V{std::string(description)}}};
    };
    V::Array fields; const ScPointLight defaults;
    for(const auto& n:point_numbers) {
        auto item=field(n.name,"number",n.required,n.description);
        item.emplace("minimum",V{n.minimum}); item.emplace("maximum",V{n.maximum});
        if(!n.required&&std::string_view(n.name)!="softness") item.emplace("default",V{double(defaults.*(n.member))});
        fields.emplace_back(std::move(item));
    }
    fields.emplace_back(field("samples","integer",false,"1..8; omitted inherits lighting.configure. One sample is centered; ignored when shadows=false."));
    auto shadows=field("shadows","boolean",false,"Enable geometry occlusion and area samples; false draws an unoccluded point light.");
    shadows.emplace("default",V{true}); fields.emplace_back(std::move(shadows));
    auto color=field("color","ScColor",false,"RGB light color with alpha intensity. This does not whiten the supplied RGB.");
    color.emplace("default",V{std::string("#FFFFFFFF")}); fields.emplace_back(std::move(color));
    auto ignore=field("ignore","integer",false,"Live entity handle excluded from this light's occlusion; 0 means no exclusion.");
    ignore.emplace("default",V{0.}); fields.emplace_back(std::move(ignore));
    V::Array constraints{V{std::string("Draw-only copied command; unknown fields, stale ignore handles and out-of-range values fail before appending. No persistent light handles.")}};
    V::Object type{{"fields",V{std::move(fields)}},{"constraints",V{std::move(constraints)}}};
    return V{V::Object{{"ScPointLight",V{std::move(type)}}}};
}
