#include "script_material.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include <stdexcept>
#include <cstdlib>

namespace {
ScScript& script(lua_State* L) { return **static_cast<ScScript**>(lua_getextraspace(L)); }
void mutate(lua_State* L) { if(script(L).phase>=2) luaL_error(L,"material mutation requires load/init/update"); }
ScMaterialId handle(lua_State* L) {
    if(lua_type(L,1)!=LUA_TNUMBER) luaL_error(L,"material handle requires an integer number");
    const auto id=luaL_checkinteger(L,1);
    if(id<1||id>static_cast<lua_Integer>(SC_ID_MAX)) luaL_error(L,"invalid material handle");
    return static_cast<ScMaterialId>(id);
}
ScMaterials& pool(lua_State* L) {
    if(!script(L).materials) throw std::runtime_error("no materials in this room");
    return *script(L).materials;
}
void read(lua_State* L,int index) {
    auto value=sc_lua_read(L,index); if(!value) throw std::runtime_error(value.error()); script(L).scratch=std::move(*value);
}
template<class T> T require(ScResult<T> result) {
    if(!result) throw std::runtime_error(result.error());
    if constexpr(!std::is_void_v<T>) return std::move(*result);
}
int create(lua_State* L) {
    mutate(L); read(L,1); auto& s=script(L);
    if(!s.materials) s.materials=std::make_unique<ScMaterials>(s.world->epoch,s.world->entities.size());
    const auto id=require(s.materials->create(*s.world,s.root,s.scratch));
    lua_pushinteger(L,static_cast<lua_Integer>(id)); return 1;
}
int set(lua_State* L) {
    mutate(L); const auto id=handle(L); read(L,2);
    require(pool(L).set(*script(L).world,id,script(L).scratch)); return 0;
}
int reload(lua_State* L) {
    mutate(L); const auto id=handle(L); require(pool(L).reload(id,script(L).root)); return 0;
}
int destroy(lua_State* L) {
    mutate(L); const auto id=handle(L);
    if(!pool(L).destroy(*script(L).world,id)) return luaL_error(L,"stale material handle or still referenced by a binding or postprocess chain");
    return 0;
}
const ScEntity& entity_at(lua_State* L) {
    if(!lua_isinteger(L,1)) luaL_error(L,"material binding requires an integer entity handle");
    const auto id=lua_tointeger(L,1);
    const auto* entity=id>0&&id<=static_cast<lua_Integer>(SC_ID_MAX)?sc_entity(script(L).world,static_cast<ScEntityId>(id)):nullptr;
    if(!entity) luaL_error(L,"material binding requires a live entity handle");
    return *entity;
}
const ScResource& image_at(lua_State* L) {
    if(lua_type(L,1)!=LUA_TSTRING) luaL_error(L,"material binding requires a declared image name");
    std::size_t size=0; const char* name=lua_tolstring(L,1,&size);
    for(const auto& image:script(L).world->resources)
        if(image.name==std::string_view(name,size)&&image.type=="image") return image;
    luaL_error(L,"material binding requires a declared image resource");
    std::abort(); // Lua never returns from the error above.
}
ScMaterialId binding_id(lua_State* L) {
    if(!lua_isinteger(L,2)||lua_tointeger(L,2)<0||lua_tointeger(L,2)>static_cast<lua_Integer>(SC_ID_MAX))
        luaL_error(L,"binding material requires a live handle or 0 to clear");
    return static_cast<ScMaterialId>(lua_tointeger(L,2));
}
int bind_entity(lua_State* L) {
    if(lua_gettop(L)!=2) return luaL_error(L,"bind_entity expects entity and material");
    mutate(L); const auto id=entity_at(L).id;
    std::optional<ScMaterialId> material;
    if(lua_isboolean(L,2)&&!lua_toboolean(L,2)) material=0; // Explicit builtin shader override.
    else if(const auto value=binding_id(L)) material=value;
    require(pool(L).bind_entity(*script(L).world,id,material)); return 0;
}
int bind_image(lua_State* L) {
    if(lua_gettop(L)!=2) return luaL_error(L,"bind_image expects image and material");
    mutate(L); const auto& image=image_at(L); const auto id=binding_id(L);
    require(pool(L).bind_image(*script(L).world,image.name,id)); return 0;
}
int entity_material(lua_State* L) {
    if(lua_gettop(L)!=1) return luaL_error(L,"entity_material expects an entity handle");
    const auto& entity=entity_at(L); const auto* materials=script(L).materials.get();
    lua_pushinteger(L,static_cast<lua_Integer>(materials?materials->entity_material(entity):0)); return 1;
}
int image_material(lua_State* L) {
    if(lua_gettop(L)!=1) return luaL_error(L,"image_material expects an image name");
    const auto& image=image_at(L); const auto* materials=script(L).materials.get();
    lua_pushinteger(L,static_cast<lua_Integer>(materials?materials->image_material(image.path):0)); return 1;
}
void information(ScScript& s,const ScMaterial& material) {
    using V=ScValue;
    V::Object values;
    for(size_t i=0;i<material.uniform_count;++i) {
        const auto& u=material.uniforms[i];
        if(u.type==ScUniformType::texture) values.emplace(u.name,V{std::string(u.texture)});
        else if(u.type==ScUniformType::integer) values.emplace(u.name,V{double(u.integer)});
        else if(u.type==ScUniformType::boolean) values.emplace(u.name,V{u.integer!=0});
        else if(u.type==ScUniformType::scalar) values.emplace(u.name,V{double(u.values[0])});
        else {
            V::Array components; for(size_t j=0;j<static_cast<size_t>(u.type)+1;++j) components.emplace_back(double(u.values[j]));
            values.emplace(u.name,V{std::move(components)});
        }
    }
    s.scratch=V{V::Object{{"shader",V{material.shader}},{"revision",V{double(material.revision)}},
        {"postprocess",V{material.postprocess}},
        {"compiled_revision",V{double(material.compiled_revision)}},{"error",V{material.error}},
        {"status",V{std::string(material.attempted_revision<material.revision?"pending":material.error.empty()?"ready":"failed")}},
        {"uniforms",V{std::move(values)}}}};
}
int info(lua_State* L) {
    const auto id=handle(L); const auto* material=pool(L).find(id);
    if(!material) return luaL_error(L,"stale material handle");
    information(script(L),*material); sc_lua_push(L,script(L).scratch); return 1;
}
int capacity(lua_State* L) {
    script(L).scratch=ScValue{ScValue::Object{{"used",ScValue{double(script(L).materials?script(L).materials->size():0)}},
        {"capacity",ScValue{64.}},{"uniforms_per_material",ScValue{32.}},{"textures_per_material",ScValue{4.}},
        {"postprocess_textures_per_material",ScValue{3.}},{"postprocess_passes",ScValue{4.}},
        {"image_bindings",ScValue{double(script(L).materials?script(L).materials->image_bindings():0)}},{"image_binding_capacity",ScValue{64.}},
        {"entity_bindings",ScValue{double(script(L).materials?script(L).materials->entity_bindings(*script(L).world):0)}},
        {"entity_binding_capacity",ScValue{double(script(L).materials?script(L).materials->entity_capacity():0)}}}};
    sc_lua_push(L,script(L).scratch); return 1;
}
int postprocess(lua_State* L) {
    mutate(L);
    if(!lua_isnoneornil(L,2)&&lua_type(L,2)!=LUA_TNUMBER) return luaL_error(L,"postprocess budget requires an integer number");
    const auto budget=luaL_optinteger(L,2,static_cast<lua_Integer>(script(L).materials?script(L).materials->post.budget_bytes:64*1024*1024));
    if(budget<0||budget>1024*1024*1024) return luaL_error(L,"postprocess budget must be 0..1073741824 bytes");
    read(L,1); auto& s=script(L);
    if(!s.materials) s.materials=std::make_unique<ScMaterials>(s.world->epoch,s.world->entities.size());
    require(s.materials->configure_post(*s.world,s.scratch,static_cast<std::uint64_t>(budget))); return 0;
}
void pipeline_info(ScScript& s) {
    using V=ScValue;
    const ScPostChain empty;
    const auto& post=s.materials?s.materials->post:empty;
    V::Array passes; for(size_t i=0;i<post.count;++i) passes.emplace_back(double(post.passes[i]));
    s.scratch=V{V::Object{{"passes",V{std::move(passes)}},{"limit",V{4.}},
        {"revision",V{double(post.revision)}},{"budget_bytes",V{double(post.budget_bytes)}},
        {"required_color_bytes",V{double(post.required_bytes)}},{"allocated_color_bytes",V{double(post.allocated_bytes)}},
        {"target_count",V{double(post.target_count)}},{"error",V{post.error}},
        {"status",V{std::string(!post.count?"disabled":!post.error.empty()?"failed":post.presented?"ready":"pending")}}}};
}
int pipeline(lua_State* L) { pipeline_info(script(L)); sc_lua_push(L,script(L).scratch); return 1; }
constexpr ScLuaParameter create_parameters[]={{"spec","table"}},handle_parameters[]={{"id","integer"}},
    set_parameters[]={{"id","integer"},{"values","table"}};
constexpr ScLuaParameter post_parameters[]={{"passes","integer[]"},{"budget_bytes","integer",false}};
const ScLuaContract post_contract{post_parameters,nullptr,ScLuaPhases::mutate,"0..4 passes; default 64 MiB color-target budget; at most 1 GiB",nullptr,"materials"};
const ScLuaContract create_contract{create_parameters,"integer",ScLuaPhases::mutate,"64 materials; 32 uniforms; auxiliary textures: image 4, postprocess 3",nullptr,"materials"};
const ScLuaContract handle_contract{handle_parameters,nullptr,ScLuaPhases::mutate,nullptr,nullptr,"materials"};
const ScLuaContract set_contract{set_parameters,nullptr,ScLuaPhases::mutate,nullptr,nullptr,"materials"};
const ScLuaContract info_contract{handle_parameters,"table",ScLuaPhases::read,nullptr,nullptr,"materials"};
const ScLuaContract capacity_contract{{},"table",ScLuaPhases::read,nullptr,nullptr,"materials"};
const ScLuaParameter entity_binding_args[]={{"entity","ScEntityId"},{"material","integer|false"}},
    image_binding_args[]={{"image","string"},{"material","integer"}},entity_query_args[]={{"entity","ScEntityId"}},image_query_args[]={{"image","string"}};
const ScLuaContract entity_binding_contract{entity_binding_args,nullptr,ScLuaPhases::mutate,"One generation-checked override per configured entity slot",nullptr,"materials"},
    image_binding_contract{image_binding_args,nullptr,ScLuaPhases::mutate,"64 resolved image paths",nullptr,"materials"},
    entity_query_contract{entity_query_args,"integer",ScLuaPhases::read,nullptr,nullptr,"materials"},
    image_query_contract{image_query_args,"integer",ScLuaPhases::read,nullptr,nullptr,"materials"};
const ScLuaApi api[]={
    {"bind_entity",sc_lua_guard<bind_entity>,"bind_entity(entity,material)","Bind a live surface material to a live entity. 0 clears the override and inherits its image default; false forces the builtin shader. Create a material first. Old entity generations never inherit overrides.",&entity_binding_contract},
    {"bind_image",sc_lua_guard<bind_image>,"bind_image(image,material)","Bind a default surface material to a declared image path, shared by images, sprites, tiles, atlas projectiles and textured particles. 0 clears. Aliases of one path share the binding. Create a material first.",&image_binding_contract},
    {"entity_material",sc_lua_guard<entity_material>,"entity_material(entity) -> id","Read the effective entity material: override, image default, then builtin (0). The entity must be alive.",&entity_query_contract},
    {"image_material",sc_lua_guard<image_material>,"image_material(image) -> id","Read a declared image's default material, or 0 for builtin rendering.",&image_query_contract},
    {"create",sc_lua_guard<create>,"create{shader,uniforms?,postprocess?} -> id","Create a room material from a declared fragment shader; uniforms map names to {type,value}. postprocess defaults false; true reserves sc_scene/sc_resolution and permits 3 auxiliary textures.",&create_contract},
    {"postprocess",sc_lua_guard<postprocess>,"postprocess(passes,budget_bytes?)","Atomically replace the bounded postprocess chain; empty clears it. Live postprocess=true handles required; referenced materials cannot be destroyed.",&post_contract},
    {"pipeline",sc_lua_guard<pipeline>,"pipeline() -> table","Read requested passes, target color-byte budget/allocation and presentation status. Headless chains stay pending.",&capacity_contract},
    {"set",sc_lua_guard<set>,"set(id,values)","Atomically patch values of existing typed uniforms; types and names remain fixed.",&set_contract},
    {"reload",sc_lua_guard<reload>,"reload(id)","Read replacement shader source; GPU compilation occurs before presentation and keeps the old program on failure.",&handle_contract},
    {"destroy",sc_lua_guard<destroy>,"destroy(id)","Release a room material; generation-checked handles become invalid. Clear image, live entity and postprocess references first; dead entity bindings do not retain materials.",&handle_contract},
    {"info",sc_lua_guard<info>,"info(id) -> table","Read source revision, GPU compilation state/error and current uniform values; headless materials stay pending.",&info_contract},
    {"capacity",sc_lua_guard<capacity>,"capacity() -> table","Read material, uniform and texture limits without allocating the pool.",&capacity_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_material_register(lua_State* L) { lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"material"); }
void sc_script_material_describe() { sc_api_describe(api,"sc.material."); }
