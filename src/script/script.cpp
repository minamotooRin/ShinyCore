#include "script_api.h"
#include "core_contract.h"
#include "entity_contract.h"
#include "audio_contract.h"
#include "script_navigation.h"
#include "script_input.h"
#include "script_images.h"
#include "script_save.h"
#include "script_identity.h"
#include "script_camera.h"
#include "script_presentation.h"
#include "script_projectiles.h"
#include "script_application.h"
#include "shiny/save.h"
#include "shiny/save_io.h"
#include "shiny/identity.h"
#include "shiny/attachment.h"
#include "script_systems.h"
#include "script_particles.h"
#ifdef SC_HAS_ADVANCED_RENDER
#include "script_material.h"
#include "script_lighting.h"
#endif
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/script_physics.h"
#include "shiny/project.h"
#include "shiny/script_audio.h"
#include "shiny/text.h"
#include "shiny/physics.h"
#include "shiny/capabilities.h"
#ifdef SC_HAS_NETWORK
#include "shiny/net_lua.h"
#include "shiny/net.h"
#endif

#include <lua.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

static constexpr std::size_t SC_LUA_MEMORY_LIMIT = 16u * 1024u * 1024u;
static constexpr int SC_LUA_INSTRUCTIONS = 1000000;
static constexpr int SC_HOOK_INTERVAL = 1000;

// Lua is compiled as C: errors use longjmp. C callbacks below deliberately keep
// only trivially destructible locals; all owning C++ scopes surround lua_pcall.
static_assert(std::is_trivially_destructible_v<ScEntity>);
static_assert(std::is_trivially_destructible_v<ScDraw>);
static_assert(std::is_trivially_destructible_v<ScTone>);

static const char draw_phase_key = 0;

static ScScript *script_of(lua_State *L) {
    return *static_cast<ScScript **>(lua_getextraspace(L));
}

static void *script_alloc(void *ud, void *ptr, size_t old_size, size_t new_size) {
    auto *script = static_cast<ScScript *>(ud);
    if (!ptr) old_size = 0;
    if (!new_size) {
        std::free(ptr);
        script->memory_used -= old_size;
        return nullptr;
    }
    if (new_size > old_size &&
        new_size - old_size > SC_LUA_MEMORY_LIMIT - script->memory_used) return nullptr;
    void *result = std::realloc(ptr, new_size);
    if (result) script->memory_used = script->memory_used - old_size + new_size;
    return result;
}

static void instruction_hook(lua_State *L, lua_Debug *ar) {
    ScScript *script = script_of(L);
    if(ar->event==LUA_HOOKCOUNT) {
        script->instruction_budget -= SC_HOOK_INTERVAL;
        if (script->instruction_budget <= 0)
            luaL_error(L, "instruction budget exceeded (1000000 per callback)");
    }
#ifdef SC_HAS_DEVTOOLS
    if(script->debug_hook&&!script->debug_hook(script->debug_context,*script,L,ar))
        luaL_error(L,"%s",script->error);
#endif
}

static int traceback(lua_State *L) {
    const char *message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "non-string Lua error", 1);
    return 1;
}

static int draw_phase(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &draw_phase_key);
    int phase = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return phase;
}

static void require_mutable(lua_State *L) {
    if (draw_phase(L)||script_of(L)->phase>=2) luaL_error(L, "simulation mutation is forbidden in draw(), ui_update() or migration; use init() or update()");
}

#ifdef SC_HAS_NETWORK
static int network_mutation_guard(lua_State *L) {
    require_mutable(L);
    if(script_of(L)->checking) return luaL_error(L,"network mutation forbidden during check");
    if(script_of(L)->candidate&&script_of(L)->phase==0) return luaL_error(L,"network mutation forbidden during candidate initialization");
    return 0;
}
#endif

static void arg_count(lua_State *L, int minimum, int maximum) {
    int count = lua_gettop(L);
    if (count < minimum || count > maximum)
        luaL_error(L, "expected %d to %d arguments, got %d", minimum, maximum, count);
}

static float number_at(lua_State *L, int index, double minimum, double maximum,const char* context="argument/field") {
    if (lua_type(L, index) != LUA_TNUMBER) luaL_error(L, "%s must be a number",context);
    lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number) || number < minimum || number > maximum)
        luaL_error(L, "%s: number outside allowed range [%f, %f]",context,minimum,maximum);
    return static_cast<float>(number);
}

static lua_Integer integer_at(lua_State *L, int index, lua_Integer minimum, lua_Integer maximum,const char* context="argument/field") {
    int valid = 0;
    lua_Integer number = lua_tointegerx(L, index, &valid);
    if (lua_type(L, index) != LUA_TNUMBER || !valid || number < minimum || number > maximum)
        luaL_error(L, "%s: expected integer in [%I, %I]",context,minimum,maximum);
    return number;
}

static const char *string_at(lua_State *L, int index, size_t maximum, bool nonempty,const char* context="argument/field") {
    if (lua_type(L, index) != LUA_TSTRING) luaL_error(L, "%s must be a string",context);
    size_t length;
    const char *value = lua_tolstring(L, index, &length);
    if (length > maximum || (nonempty && !length) || std::memchr(value, 0, length))
        luaL_error(L, "%s: string is empty, too long, or contains a NUL byte",context);
    return value;
}

bool sc_script_validate_path(const char *path) {
    if (!path || !*path || std::strlen(path) >= SC_PATH_MAX || path[0] == '/') return false;
    const char *segment = path;
    for (const char *p = path;; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c == '\\' || c == ':' || (c && c < 32) || c == 127) return false;
        if (!c || c == '/') {
            size_t length = static_cast<std::size_t>(p - segment);
            if (!length || (length == 1 && segment[0] == '.') ||
                (length == 2 && segment[0] == '.' && segment[1] == '.')) return false;
            if (!c) return true;
            segment = p + 1;
        }
    }
}

static uint32_t color_at(lua_State *L, int index) {
    const char *color = string_at(L, index, 9, true);
    size_t length = std::strlen(color);
    if ((length != 7 && length != 9) || color[0] != '#')
        luaL_error(L, "color must be '#RRGGBB' or '#RRGGBBAA'");
    uint32_t value = 0;
    for (size_t i = 1; i < length; ++i) {
        unsigned char c = static_cast<unsigned char>(color[i]);
        unsigned int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else { luaL_error(L, "invalid hexadecimal color"); return 0; }
        value = (value << 4) | digit;
    }
    return length == 7 ? (value << 8) | 255u : value;
}

static void strict_keys(lua_State *L, int index, const char *const *allowed, const char *context) {
    luaL_checktype(L, index, LUA_TTABLE);
    index = lua_absindex(L, index);
    if (lua_getmetatable(L, index))
        luaL_error(L, "%s must be a plain data table without a metatable", context);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "%s keys must be strings", context);
        const char *key = string_at(L, -2, 128, true);
        bool found = false;
        for (size_t i = 0; allowed[i]; ++i) if (!std::strcmp(key, allowed[i])) { found = true; break; }
        if (!found) luaL_error(L, "unknown %s field '%s'", context, key);
        lua_pop(L, 1);
    }
}

static size_t array_length(lua_State *L, int index, size_t maximum, const char *context) {
    luaL_checktype(L, index, LUA_TTABLE);
    index = lua_absindex(L, index);
    size_t length = lua_rawlen(L, index), count = 0;
    if (length > maximum) luaL_error(L, "%s exceeds capacity", context);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (!lua_isinteger(L, -2)) luaL_error(L, "%s must be a dense array", context);
        lua_Integer key = lua_tointeger(L, -2);
        if (key < 1 || static_cast<lua_Unsigned>(key) > length) luaL_error(L, "%s must be a dense array", context);
        ++count;
        lua_pop(L, 1);
    }
    if (count != length) luaL_error(L, "%s must be a dense array", context);
    return length;
}

static void float_field(lua_State *L, int index, const char *name, float *out, double min, double max) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) *out = number_at(L, -1, min, max,name);
    lua_pop(L, 1);
}

static void int_field(lua_State *L, int index, const char *name, int *out, int min, int max) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) *out = static_cast<int>(integer_at(L, -1, min, max,name));
    lua_pop(L, 1);
}

static void bool_field(lua_State *L, int index, const char *name, bool *out) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) {
        if(lua_type(L,-1)!=LUA_TBOOLEAN) luaL_error(L,"%s must be a boolean",name);
        *out = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
}

static void string_field(lua_State *L, int index, const char *name, char *out, size_t size) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) {
        const char *value = string_at(L, -1, size - 1, false,name);
        std::memcpy(out, value, std::strlen(value) + 1);
    }
    lua_pop(L, 1);
}

static void color_field(lua_State *L, int index, const char *name, uint32_t *out) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) *out = color_at(L, -1);
    lua_pop(L, 1);
}

static void entity_patch(lua_State *L, int index, ScEntity *entity) {
    const ScEntity before=*entity;
    strict_keys(L, index, SC_ENTITY_KEYS.data(), "entity");
    for(const auto& field:SC_ENTITY_NUMBERS)
        float_field(L,index,field.name,&(entity->*field.member),field.minimum,field.maximum);
    for(const auto& field:SC_ENTITY_INTEGERS)
        int_field(L,index,field.name,&(entity->*field.member),static_cast<int>(field.minimum),static_cast<int>(field.maximum));
    for(const auto& field:SC_ENTITY_BOOLEANS) bool_field(L,index,field.name,&(entity->*field.member));
    color_field(L,index,"color",&entity->color);
    string_field(L,index,SC_ENTITY_TAG.name,entity->tag,SC_ENTITY_TAG.maximum_bytes+1);
    string_field(L,index,SC_ENTITY_SPRITE.name,entity->sprite,SC_ENTITY_SPRITE.maximum_bytes+1);
    char persistent_id[128]{};
    std::memcpy(persistent_id,entity->persistent_id,sizeof persistent_id);
    string_field(L,index,SC_ENTITY_IDENTITY.name,persistent_id,sizeof persistent_id);
    if(persistent_id[0]&&!sc_identity_name_valid(persistent_id)) luaL_error(L,"invalid persistent_id");
    if(entity->alive&&std::strcmp(persistent_id,entity->persistent_id)) luaL_error(L,"persistent_id is immutable after spawn");
    std::memcpy(entity->persistent_id,persistent_id,sizeof persistent_id);
    for(const auto& resource:script_of(L)->world->resources) if(resource.type=="image"&&resource.name==entity->sprite) {
        std::snprintf(entity->sprite,sizeof entity->sprite,"%s",resource.path.c_str()); break;
    }
    sc_script_body_patch(L,index,entity);
    if(!sc_attachment_patch_valid(before,*entity))
        luaL_error(L,"attached entity: use sc.presentation.attach for local pose; body and velocity must remain absent");
    if(!sc_physics_body_valid(*entity)) luaL_error(L,"invalid body: polygons require 3..8 distinct convex vertices");
    if (*entity->sprite && !sc_script_validate_path(entity->sprite))
        luaL_error(L, "sprite must be a project-relative path without '.' or '..' segments");
    if ((entity->frame_w == 0) != (entity->frame_h == 0))
        luaL_error(L, "frame_w and frame_h must both be positive or both zero");
}

static ScEntity entity_defaults(void) {
    ScEntity entity{};
    for(const auto& field:SC_ENTITY_NUMBERS) entity.*field.member=field.initial;
    for(const auto& field:SC_ENTITY_INTEGERS) entity.*field.member=field.initial;
    for(const auto& field:SC_ENTITY_BOOLEANS) entity.*field.member=field.initial;
    entity.color=SC_ENTITY_COLOR;
    return entity;
}

static ScEntityId id_at(lua_State *L, int index) {
    return static_cast<ScEntityId>(integer_at(L, index, 1, SC_ID_MAX));
}

static ScEntity *entity_at(lua_State *L, int index) {
    ScEntityId id = id_at(L, index);
    ScEntity *entity = sc_entity(script_of(L)->world, id);
    if (!entity) luaL_error(L, "entity ID %I is stale or unknown", static_cast<lua_Integer>(id));
    return entity;
}

static int api_spawn(lua_State *L) {
    arg_count(L, 1, 1); require_mutable(L);
    ScEntity entity = entity_defaults();
    entity_patch(L, 1, &entity);
    const char* error=nullptr;
    ScEntityId id = sc_spawn(script_of(L)->world, &entity,&error);
    if (!id) return luaL_error(L, "spawn: %s (persistent_id='%s')",error?error:"entity creation failed",entity.persistent_id);
    lua_pushinteger(L, id);
    return 1;
}

void sc_script_prepare_spawn_batch(lua_State* L,int index) {
    auto* s=script_of(L); index=lua_absindex(L,index);
    if(lua_getmetatable(L,index)) luaL_error(L,"entities must be a plain array");
    const auto count=array_length(L,index,s->world->entities.size(),"entities");
    lua_createtable(L,static_cast<int>(count),0);
    s->batch_entities.resize(count);
    for(std::size_t i=0;i<count;++i) {
        s->batch_entities[i]=entity_defaults();
        lua_rawgeti(L,index,static_cast<lua_Integer>(i+1));
        entity_patch(L,-1,&s->batch_entities[i]); lua_pop(L,1);
    }
}
void sc_script_push_spawn_ids(lua_State* L) {
    auto* s=script_of(L);
    for(std::size_t i=0;i<s->batch_entities.size();++i) {
        lua_pushinteger(L,static_cast<lua_Integer>(s->batch_entities[i].id));
        lua_rawseti(L,-2,static_cast<lua_Integer>(i+1));
    }
}
static int api_spawn_many(lua_State* L) {
    arg_count(L,1,2); require_mutable(L); auto* s=script_of(L);
    const bool parents=lua_gettop(L)==2&&!lua_isnil(L,2);
    sc_script_prepare_spawn_batch(L,1);
    s->batch_parents.clear();
    if(parents) {
        if(!lua_istable(L,2)||lua_getmetatable(L,2)) return luaL_error(L,"parents must be a plain array");
        const auto count=array_length(L,2,s->world->entities.size(),"parents");
        if(count!=s->batch_entities.size()) return luaL_error(L,"parents must match entity batch length");
        s->batch_parents.resize(count);
        for(std::size_t i=0;i<count;++i) {
            lua_rawgeti(L,2,static_cast<lua_Integer>(i+1));
            s->batch_parents[i]=static_cast<std::size_t>(integer_at(L,-1,0,count)); lua_pop(L,1);
        }
    }
    const char* error=nullptr;
    { auto result=sc_spawn_many(*s->world,s->batch_entities,s->batch_parents); if(!result) error=result.error(); }
    if(error) return luaL_error(L,"spawn_many: %s",error);
    sc_script_push_spawn_ids(L); return 1;
}

static void push_number_field(lua_State *L, const char *name, lua_Number value) {
    lua_pushnumber(L, value); lua_setfield(L, -2, name);
}

static void push_integer_field(lua_State *L, const char *name, lua_Integer value) {
    lua_pushinteger(L, value); lua_setfield(L, -2, name);
}

static void push_bool_field(lua_State *L, const char *name, bool value) {
    lua_pushboolean(L, value); lua_setfield(L, -2, name);
}

static int push_entity(lua_State *L, const ScEntity* entity) {
    lua_createtable(L, 0, 21);
    push_integer_field(L, "id", entity->id);
    push_bool_field(L, "grounded", entity->grounded);
    for(const auto& field:SC_ENTITY_NUMBERS) push_number_field(L,field.name,entity->*field.member);
    for(const auto& field:SC_ENTITY_INTEGERS) push_integer_field(L,field.name,entity->*field.member);
    for(const auto& field:SC_ENTITY_BOOLEANS) push_bool_field(L,field.name,entity->*field.member);
    lua_pushstring(L, entity->tag); lua_setfield(L, -2, "tag");
    lua_pushstring(L, entity->sprite); lua_setfield(L, -2, "sprite");
    lua_pushstring(L, entity->persistent_id); lua_setfield(L, -2, "persistent_id");
    char color[10];
    std::snprintf(color, sizeof color, "#%08x", entity->color);
    lua_pushstring(L, color); lua_setfield(L, -2, "color");
    sc_script_body_push(L,entity);
    return 1;
}

static int api_get(lua_State* L) {
    arg_count(L,1,1); return push_entity(L,entity_at(L,1));
}
static int api_get_many(lua_State* L) {
    arg_count(L,1,1); auto count=array_length(L,1,65536,"ids");
    lua_createtable(L,static_cast<int>(count),0);
    for(size_t i=0;i<count;++i) {
        lua_rawgeti(L,1,static_cast<lua_Integer>(i+1)); auto* e=entity_at(L,-1); lua_pop(L,1);
        push_entity(L,e); lua_rawseti(L,-2,static_cast<lua_Integer>(i+1));
    }
    return 1;
}
static int api_set_many(lua_State* L) {
    arg_count(L,1,1); require_mutable(L); auto* s=script_of(L);
    auto count=array_length(L,1,s->world->entities.size(),"patches"); s->batch_entities.resize(count);
    s->batch_seen.reset();
    // The scratch owner is outside callback stack frames that Lua can longjmp.
    for(size_t i=0;i<count;++i) {
        lua_rawgeti(L,1,static_cast<lua_Integer>(i+1));
        static const char* keys[]={"id","patch",nullptr}; strict_keys(L,-1,keys,"batch item");
        lua_getfield(L,-1,"id"); s->batch_entities[i]=*entity_at(L,-1); lua_pop(L,1);
        const auto slot=sc_entity_slot(s->batch_entities[i].id);
        if(s->batch_seen.test(slot)) return luaL_error(L,"duplicate entity in batch");
        s->batch_seen.set(slot);
        lua_getfield(L,-1,"patch"); entity_patch(L,-1,&s->batch_entities[i]); lua_pop(L,2);
    }
    for(const auto& entity:s->batch_entities) *sc_entity(s->world,entity.id)=entity;
    sc_attachments_sync(*s->world);
    return 0;
}
static int api_find_all(lua_State* L) {
    arg_count(L,1,1); const char* tag=string_at(L,1,47,true); lua_newtable(L); lua_Integer n=0;
    for(const auto& e:script_of(L)->world->entities) if(e.alive&&!std::strcmp(e.tag,tag)) { lua_pushinteger(L,e.id); lua_rawseti(L,-2,++n); }
    return 1;
}

static int api_set(lua_State *L) {
    arg_count(L, 2, 2); require_mutable(L);
    ScEntity *entity = entity_at(L, 1), patched = *entity;
    entity_patch(L, 2, &patched);
    *entity = patched;
    sc_attachments_sync(*script_of(L)->world);
    return 0;
}

static int api_destroy(lua_State *L) {
    arg_count(L, 1, 1); require_mutable(L);
    ScEntityId id = entity_at(L, 1)->id;
    lua_pushboolean(L, sc_destroy(script_of(L)->world, id));
    return 1;
}

static int api_find(lua_State *L) {
    arg_count(L, 1, 1);
    ScEntityId id = sc_find(script_of(L)->world, string_at(L, 1, 47, true));
    if (id) lua_pushinteger(L, id); else lua_pushnil(L);
    return 1;
}

static int api_objects(lua_State* L) { arg_count(L,0,0); sc_lua_push(L,script_of(L)->objects); return 1; }
static int api_map(lua_State* L) {
    arg_count(L,3,4); auto* s=script_of(L); auto* w=s->world;
    const char* name=string_at(L,1,128,true); ScLayer* layer=nullptr;
    for(auto& item:w->layers) if(item.name==name) layer=&item;
    if(!layer) return luaL_error(L,"unknown map layer: %s",name);
    int x=static_cast<int>(integer_at(L,2,0,w->map.width-1)),y=static_cast<int>(integer_at(L,3,0,w->map.height-1));
    auto& cell=layer->cells[static_cast<size_t>(y*w->map.width+x)];
    if(lua_gettop(L)==4) {
        require_mutable(L); auto gid=static_cast<uint32_t>(integer_at(L,4,0,UINT32_MAX));
        if(gid&&!sc_tile_graphic(w,gid)) return luaL_error(L,"unknown map GID");
        auto old=cell; cell=gid; bool ok=false;
        {
            auto result=sc_project_tiles(w,s->navigation_region.get(),true); ok=result.has_value();
            if(!ok) std::snprintf(s->error,sizeof s->error,"%s",result.error().c_str());
        }
        if(!ok) { cell=old; return luaL_error(L,"map: %s",s->error); }
    }
    lua_pushinteger(L,cell); return 1;
}

static int api_overlap(lua_State *L) {
    arg_count(L, 2, 2);
    ScEntity *a = entity_at(L, 1), *b = entity_at(L, 2);
    lua_pushboolean(L, sc_overlap(a, b));
    return 1;
}

static uint32_t action_at(lua_State *L) {
    arg_count(L, 1, 1);
    const char *action = string_at(L, 1, 16, true);
    static const char *const names[] = {"left", "right", "up", "down", "jump", "action"};
    for (unsigned int i = 0; i < 6; ++i) if (!std::strcmp(action, names[i])) return 1u << i;
    luaL_error(L, "unknown action '%s'; expected left/right/up/down/jump/action", action);
    return 0;
}

template<std::size_t N> static int device_name(lua_State* L,const ScInputName (&names)[N]) {
    const char* name=string_at(L,1,32,true);
    int id=sc_input_id(names,name);
    if(id<0) return luaL_error(L,"unknown input name '%s'",name);
    return id;
}
template<int Edge> static int api_key(lua_State* L) {
    arg_count(L,1,1);
    int id=device_name(L,SC_KEYS); const auto& in=script_of(L)->world->input;
    const auto& bits=Edge==0?in.keys:Edge==1?in.key_pressed:in.key_released;
    lua_pushboolean(L,bits[static_cast<std::size_t>(id)]); return 1;
}
template<int Edge> static int api_gamepad(lua_State* L) {
    arg_count(L,1,1);
    int id=device_name(L,SC_BUTTONS); const auto& in=script_of(L)->world->input;
    auto bits=Edge==0?in.buttons:Edge==1?in.button_pressed:in.button_released;
    lua_pushboolean(L,(bits&(1u<<id))!=0); return 1;
}
static int api_gamepad_connected(lua_State* L) {
    arg_count(L,0,0);
    lua_pushboolean(L,script_of(L)->world->input.connected); return 1;
}
static int api_gamepad_axis(lua_State* L) {
    arg_count(L,1,2);
    int axis=device_name(L,SC_AXES);
    float deadzone=0.2f;
    if(!lua_isnoneornil(L,2)) {
        if(lua_type(L,2)!=LUA_TNUMBER) return luaL_error(L,"deadzone must be a number in [0,1)");
        double value=lua_tonumber(L,2);
        if(!std::isfinite(value)||value<0||value>=1) return luaL_error(L,"deadzone must be in [0,1)");
        deadzone=static_cast<float>(value);
        if(deadzone>=1) return luaL_error(L,"deadzone must be representable below 1");
    }
    lua_pushnumber(L,sc_gamepad_axis(script_of(L)->world->input,axis,deadzone)); return 1;
}

static int api_down(lua_State *L) {
    uint32_t action = action_at(L);
    lua_pushboolean(L, (script_of(L)->world->held & action) != 0); return 1;
}

static int api_pressed(lua_State *L) {
    uint32_t action = action_at(L);
    lua_pushboolean(L, (script_of(L)->world->pressed & action) != 0); return 1;
}

static int api_released(lua_State *L) {
    uint32_t action = action_at(L);
    lua_pushboolean(L, (script_of(L)->world->released & action) != 0); return 1;
}

static int api_tile(lua_State *L) {
    arg_count(L, 2, 3);
    int x = static_cast<int>(integer_at(L, 1, -1000000, 1000000));
    int y = static_cast<int>(integer_at(L, 2, -1000000, 1000000));
    ScWorld *world = script_of(L)->world;
    if (lua_gettop(L) == 3) {
        require_mutable(L);
        const char *tile = string_at(L, 3, 1, true);
        if (*tile != '.' && *tile != '#' && *tile != '=') return luaL_error(L, "tile must be '.', '#', or '='");
        if (x < 0 || y < 0 || x >= world->map.width || y >= world->map.height)
            return luaL_error(L, "tile write outside map bounds (coordinates are zero-based)");
        auto& cell=world->map.tiles[static_cast<std::size_t>(y * world->map.width + x)];
        if((cell=='.')!=(*tile=='.')) {
            if(world->map.navigation_revision==UINT64_MAX) return luaL_error(L,"navigation revision exhausted");
            ++world->map.navigation_revision;
        }
        cell = *tile;
    }
    char tile = sc_tile(world, x, y);
    lua_pushlstring(L, &tile, 1); return 1;
}

static int api_random(lua_State *L) {
    require_mutable(L);
    ScWorld *world = script_of(L)->world;
    if (!lua_gettop(L)) { lua_pushnumber(L, sc_random(world)); return 1; }
    arg_count(L, 2, 2);
    float minimum = number_at(L, 1, -1000000, 1000000);
    float maximum = number_at(L, 2, -1000000, 1000000);
    if (minimum > maximum) return luaL_error(L, "random minimum must be <= maximum");
    if (lua_isinteger(L, 1) && lua_isinteger(L, 2)) {
        lua_Integer lo = lua_tointeger(L, 1), hi = lua_tointeger(L, 2);
        uint32_t range = static_cast<uint32_t>(hi - lo + 1);
        uint32_t threshold = static_cast<uint32_t>(-range) % range, sample;
        do { sample = sc_random_u32(world); } while (sample < threshold);
        lua_pushinteger(L, lo + sample % range);
    } else lua_pushnumber(L, minimum + (maximum - minimum) * static_cast<double>(sc_random(world)));
    return 1;
}

static int api_emit(lua_State *L) {
    arg_count(L, 4, 6); require_mutable(L);
    float x = number_at(L, 1, -1000000, 1000000), y = number_at(L, 2, -1000000, 1000000);
    int count = static_cast<int>(integer_at(L, 3, 0, SC_MAX_PARTICLES));
    uint32_t color = color_at(L, 4);
    float speed = lua_gettop(L) >= 5 ? number_at(L, 5, 0, 1000000) : 30;
    float life = lua_gettop(L) >= 6 ? number_at(L, 6, 0.001, 60) : 0.5f;
    auto* world=script_of(L)->world;
    const char* error=nullptr;
    { auto result=sc_emit(world,x,y,count,color,speed,life); if(!result) error=result.error(); }
    if(error) return luaL_error(L,"%s (used=%I, requested=%I, capacity=%I)",error,
        static_cast<lua_Integer>(world->particles.count),static_cast<lua_Integer>(count),static_cast<lua_Integer>(world->particles.capacity()));
    return 0;
}

static int api_tone(lua_State *L) {
    arg_count(L, 1, 3); require_mutable(L);
    ScTone tone{};
    tone.frequency = number_at(L, 1, 20, 20000);
    tone.duration = lua_gettop(L) >= 2 ? number_at(L, 2, 0.001, 10) : 0.1f;
    tone.volume = lua_gettop(L) >= 3 ? number_at(L, 3, 0, 1) : 0.2f;
    ScWorld *world = script_of(L)->world;
    if (world->tone_count >= SC_MAX_TONES) return luaL_error(L, "tone queue capacity exhausted (32 per tick)");
    world->tones[static_cast<std::size_t>(world->tone_count++)] = tone; return 0;
}

static int api_message(lua_State *L) {
    arg_count(L, 1, 1); require_mutable(L);
    const char *message = string_at(L, 1, sizeof(script_of(L)->world->message) - 1, false);
    std::memcpy(script_of(L)->world->message, message, std::strlen(message) + 1); return 0;
}

static int api_scene(lua_State *L) {
    arg_count(L, 1, 2); require_mutable(L);
    const char *path = string_at(L, 1, SC_PATH_MAX - 1, true);
    size_t length = std::strlen(path);
    if (!sc_script_validate_path(path) || length < 5 || std::strcmp(path + length - 4, ".lua"))
        return luaL_error(L, "scene path must be a project-relative .lua file without '.' or '..' segments");
    auto* s=script_of(L);
    if(s->save_io&&s->save_io->active()) return luaL_error(L,"release the asynchronous save request before changing rooms");
#ifdef SC_HAS_STREAMING
    if(s->images&&s->images->busy()) return luaL_error(L,"commit or cancel the image request before changing rooms");
#endif
    if (lua_gettop(L)==2) {
        bool ok=false;
        {
            auto value=sc_lua_read(L,2);
            if(value && std::holds_alternative<ScValue::Object>(value->data)) {
                auto checked=sc_state_validate(*value);
                if(checked) { s->pending_state=std::move(*checked); s->has_pending_state=true; ok=true; }
            }
        }
        if(!ok) return luaL_error(L,"scene state must be a bounded plain object");
    } else s->has_pending_state=false;
    std::memcpy(s->pending_scene, path, length + 1); return 0;
}

static ScDraw *push_draw(lua_State *L, ScDraw draw, int screen_index) {
    if (!draw_phase(L)) luaL_error(L, "drawing APIs are available only inside draw(alpha)");
    if (lua_gettop(L) >= screen_index) {
        luaL_checktype(L, screen_index, LUA_TBOOLEAN);
        draw.screen = lua_toboolean(L, screen_index) != 0;
    }
    ScWorld *world = script_of(L)->world;
    if (world->draw_count >= static_cast<int>(world->draws.size())) luaL_error(L, "draw queue capacity exhausted");
    world->draws[static_cast<std::size_t>(world->draw_count)] = draw;
    return &world->draws[static_cast<std::size_t>(world->draw_count++)];
}

static int api_rect(lua_State *L) {
    arg_count(L, 5, 6);
    ScDraw draw{}; draw.kind = SC_DRAW_RECT;
    draw.x = number_at(L, 1, -1000000, 1000000); draw.y = number_at(L, 2, -1000000, 1000000);
    draw.w = number_at(L, 3, 0, 1000000); draw.h = number_at(L, 4, 0, 1000000);
    draw.color = color_at(L, 5); push_draw(L, draw, 6); return 0;
}

static int api_clip(lua_State* L) {
    int n=lua_gettop(L); if(n!=0&&n!=4) return luaL_error(L,"clip expects zero or four arguments");
    ScDraw draw{}; draw.screen=true; draw.kind=n?SC_DRAW_CLIP:SC_DRAW_UNCLIP;
    if(n) { draw.x=number_at(L,1,-1e6,1e6); draw.y=number_at(L,2,-1e6,1e6); draw.w=number_at(L,3,0,1e6); draw.h=number_at(L,4,0,1e6); }
    push_draw(L,draw,5); return 0;
}
static int api_image(lua_State* L) {
    arg_count(L,5,6); ScDraw draw{}; draw.kind=SC_DRAW_IMAGE; draw.color=0xffffffff;
    const char* name=string_at(L,1,127,true); const ScResource* resource=nullptr;
    for(const auto& r:script_of(L)->world->resources) if(r.name==name&&r.type=="image") resource=&r;
    if(!resource) return luaL_error(L,"image requires a declared image resource");
#ifdef SC_HAS_STREAMING
    if(resource->streamed) {
        char path[SC_PATH_MAX*2]; std::snprintf(path,sizeof path,"%s/%s",script_of(L)->root,resource->path.c_str());
        if(!script_of(L)->images||!script_of(L)->images->path(path))
            return luaL_error(L,"streamed image is not committed: %s",name);
    }
#endif
    std::snprintf(draw.text,sizeof draw.text,"%s",resource->path.c_str());
    draw.x=number_at(L,2,-1e6,1e6); draw.y=number_at(L,3,-1e6,1e6); draw.w=number_at(L,4,0,4096); draw.h=number_at(L,5,0,4096);
    if(lua_istable(L,6)) {
        static const char* keys[]={"source_x","source_y","source_w","source_h","flip_x","flip_y","diagonal","color","screen","layer","material","slice",nullptr};
        strict_keys(L,6,keys,"image options");
        lua_getfield(L,6,"material");
        if(!lua_isnil(L,-1)) {
#ifdef SC_HAS_ADVANCED_RENDER
            if(lua_isboolean(L,-1)&&!lua_toboolean(L,-1)) draw.default_material=false;
            else {
                draw.material=static_cast<ScMaterialId>(integer_at(L,-1,1,static_cast<lua_Integer>(SC_ID_MAX)));
                auto* materials=script_of(L)->materials.get();
                if(!materials||!materials->find(draw.material)) return luaL_error(L,"stale material handle in image options");
                if(materials->find(draw.material)->postprocess) return luaL_error(L,"postprocess materials cannot draw individual images");
            }
#else
            return luaL_error(L,"image material requires SHINY_ADVANCED_RENDER=ON");
#endif
        }
        lua_pop(L,1);
        float_field(L,6,"source_x",&draw.source_x,0,8192);
        float_field(L,6,"source_y",&draw.source_y,0,8192);
        float_field(L,6,"source_w",&draw.source_w,0,8192);
        float_field(L,6,"source_h",&draw.source_h,0,8192);
        if((draw.source_w==0)!=(draw.source_h==0)||
            (!draw.source_w&&(draw.source_x||draw.source_y))) return luaL_error(L,"image source requires positive width and height");
        if(draw.source_w&&(draw.source_x+draw.source_w>resource->image_width||draw.source_y+draw.source_h>resource->image_height))
            return luaL_error(L,"image source rectangle exceeds declared PNG dimensions");
        lua_getfield(L,6,"slice");
        if(!lua_isnil(L,-1)) {
            static const char* slice_keys[]={"left","right","top","bottom",nullptr};
            strict_keys(L,lua_gettop(L),slice_keys,"image slice");
            float_field(L,lua_gettop(L),"left",&draw.slice_left,0,8192);
            float_field(L,lua_gettop(L),"right",&draw.slice_right,0,8192);
            float_field(L,lua_gettop(L),"top",&draw.slice_top,0,8192);
            float_field(L,lua_gettop(L),"bottom",&draw.slice_bottom,0,8192);
            const float width=draw.source_w?draw.source_w:static_cast<float>(resource->image_width);
            const float height=draw.source_h?draw.source_h:static_cast<float>(resource->image_height);
            if(draw.slice_left+draw.slice_right>=width||draw.slice_top+draw.slice_bottom>=height)
                return luaL_error(L,"image slice requires a positive center inside a bounded PNG source");
        }
        lua_pop(L,1);
        bool_field(L,6,"flip_x",&draw.flip_x); bool_field(L,6,"flip_y",&draw.flip_y);
        bool_field(L,6,"diagonal",&draw.diagonal); bool_field(L,6,"screen",&draw.screen);
        color_field(L,6,"color",&draw.color);
        lua_getfield(L,6,"layer");
        if(!lua_isnil(L,-1)) { draw.layered=true; draw.layer=static_cast<int>(integer_at(L,-1,-32768,32767)); }
        lua_pop(L,1);
        push_draw(L,draw,7);
    } else push_draw(L,draw,6);
    return 0;
}

static int api_circle(lua_State *L) {
    arg_count(L, 4, 5);
    ScDraw draw{}; draw.kind = SC_DRAW_CIRCLE;
    draw.x = number_at(L, 1, -1000000, 1000000); draw.y = number_at(L, 2, -1000000, 1000000);
    draw.w = draw.h = number_at(L, 3, 0, 4096);
    draw.color = color_at(L, 4); push_draw(L, draw, 5); return 0;
}

static void validate_text(lua_State* L,const char* text,const char* font) {
    auto* script=script_of(L); bool valid=false,found=!*font,missing=false;
    {
        valid=sc_utf8(text).has_value();
        for(const auto& resource:script->world->resources) if(resource.name==font&&resource.type=="font") found=true;
        if(valid&&found) missing=sc_text_layout(script->world,text,16,font).missing;
    }
    if(!valid) luaL_error(L,"text must be valid UTF-8");
    if(!found) luaL_error(L,"unknown font resource: %s",font);
    if(missing&&!script->warned_missing_glyph) {
        std::fputs("[shiny] missing text glyph; using '?'. Add a font resource containing this codepoint.\n",stderr);
        script->warned_missing_glyph=true;
    }
}
static int api_text(lua_State *L) {
    arg_count(L, 5, 7);
    ScDraw draw{}; draw.kind = SC_DRAW_TEXT;
    const char *text = string_at(L, 1, sizeof draw.text - 1, false);
    std::memcpy(draw.text, text, std::strlen(text) + 1);
    draw.x = number_at(L, 2, -1000000, 1000000); draw.y = number_at(L, 3, -1000000, 1000000);
    draw.h = number_at(L, 4, 1, 512); draw.color = color_at(L, 5);
    if(!lua_isnoneornil(L,7)) {
        static const char* const keys[]={"font","wrap","align",nullptr}; strict_keys(L,7,keys,"text options");
        string_field(L,7,"font",draw.font,sizeof draw.font); float_field(L,7,"wrap",&draw.wrap,0,4096);
        int_field(L,7,"align",&draw.align,0,2);
    }
    validate_text(L,draw.text,draw.font);
    push_draw(L, draw, 6); return 0;
}
static int api_measure(lua_State* L) {
    arg_count(L,2,4); const char* text=string_at(L,1,4096,false); float size=number_at(L,2,1,512);
    const char* font=!lua_isnoneornil(L,3)?string_at(L,3,127,false):"";
    float wrap=!lua_isnoneornil(L,4)?number_at(L,4,0,4096):0,width=0,height=0;
    validate_text(L,text,font);
    { auto layout=sc_text_layout(script_of(L)->world,text,size,font,wrap); width=layout.width; height=layout.height; }
    lua_pushnumber(L,width); lua_pushnumber(L,height); return 2;
}

static int api_tick(lua_State *L) {
    arg_count(L, 0, 0); lua_pushinteger(L, static_cast<lua_Integer>(script_of(L)->world->tick)); return 1;
}

static int api_time(lua_State *L) {
    arg_count(L, 0, 0); lua_pushnumber(L, static_cast<lua_Number>(script_of(L)->world->tick) / 60.0); return 1;
}

static int api_log(lua_State *L) {
    arg_count(L, 1, 1);
    std::fprintf(stderr, "[lua] %s\n", string_at(L, 1, 4096, false)); return 0;
}

/* A caught instruction-limit error must still unwind out of every pcall/xpcall.
 * Without this guard, a script could accidentally retry its failed infinite loop. */
static int guarded_protected_call(lua_State *L) {
    int arguments = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, arguments, LUA_MULTRET);
    if (script_of(L)->instruction_budget <= 0)
        return luaL_error(L, "instruction budget exceeded (1000000 per callback)");
    return lua_gettop(L);
}

static int guarded_setmetatable(lua_State *L) {
    if (lua_istable(L, 2)) {
        lua_pushliteral(L, "__gc"); lua_rawget(L, 2);
        if (!lua_isnil(L, -1))
            return luaL_error(L, "__gc finalizers are disabled to preserve instruction limits");
        lua_pop(L, 1);
    }
    int arguments = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1)); lua_insert(L, 1);
    lua_call(L, arguments, LUA_MULTRET);
    return lua_gettop(L);
}

static constexpr ScLuaParameter entity_argument[]={{"entity","ScEntityPatch"}};
static constexpr ScLuaParameter id_argument[]={{"id","ScEntityId"}};
static constexpr ScLuaParameter ids_argument[]={{"ids","ScEntityId[]"}};
static constexpr ScLuaParameter edit_arguments[]={{"id","ScEntityId"},{"patch","ScEntityPatch"}};
static constexpr ScLuaParameter edits_argument[]={{"items","ScEntityEdit[]"}};
static constexpr ScLuaParameter tag_argument[]={{"tag","string"}};
static constexpr ScLuaParameter overlap_arguments[]={{"a","ScEntityId"},{"b","ScEntityId"}};
static constexpr ScLuaContract spawn_contract{entity_argument,"ScEntityId",ScLuaPhases::mutate,"project.limits.entities"};
    static constexpr ScLuaParameter entities_argument[]={{"entities","ScEntityPatch[]"},
        {"parents","integer[]|nil",false,"Dense array matching entities: 0 for a root, otherwise a one-based index in this batch (forward references allowed). Child x/y/angle become local offsets. Omitted/nil creates independent roots."}};
static constexpr ScLuaContract spawn_many_contract{entities_argument,"ScEntityId[]",ScLuaPhases::mutate,"project.limits.entities"};
static constexpr ScLuaContract get_contract{id_argument,"ScEntity",ScLuaPhases::read};
static constexpr ScLuaContract get_many_contract{ids_argument,"ScEntity[]",ScLuaPhases::read,"65536 input IDs"};
static constexpr ScLuaContract set_contract{edit_arguments,nullptr,ScLuaPhases::mutate};
static constexpr ScLuaContract set_many_contract{edits_argument,nullptr,ScLuaPhases::mutate,"project.limits.entities"};
static constexpr ScLuaContract destroy_contract{id_argument,"boolean",ScLuaPhases::mutate};
static constexpr ScLuaContract find_contract{tag_argument,"ScEntityId|nil",ScLuaPhases::read};
static constexpr ScLuaContract find_all_contract{tag_argument,"ScEntityId[]",ScLuaPhases::read};
static constexpr ScLuaContract overlap_contract{overlap_arguments,"boolean",ScLuaPhases::read};

static const ScValue image_options_default{false};
static const ScLuaParameter image_parameters[]={
    {"resource","string",true,"Declared image resource name; streamed images must be committed."},
    {"x","number",true,"Destination coordinate.",nullptr,-1000000,1000000},
    {"y","number",true,"Destination coordinate.",nullptr,-1000000,1000000},
    {"w","number",true,"Destination width.",nullptr,0,4096},
    {"h","number",true,"Destination height.",nullptr,0,4096},
    {"options","ScImageOptions|boolean",false,"Image options, or screen boolean.",&image_options_default}};
static const ScLuaContract image_contract{image_parameters,nullptr,ScLuaPhases::draw,"One command; at most nine quads with slice"};
static const ScLuaApi api[] = {
    {"clip",sc_lua_guard<api_clip>,"clip(x,y,w,h) / clip()","Push or pop a screen-space clipping rectangle; nesting depth 32.",&sc_core_api::clip},
    {"image",sc_lua_guard<api_image>,"image(resource,x,y,w,h,options?)","Draw an image region or nine-slice panel with stable order, clipping, tint and optional material.",&image_contract},
    {"get_many",sc_lua_guard<api_get_many>,"get_many(ids) -> entities","Read entity copies in requested order.",&get_many_contract},
    {"set_many",sc_lua_guard<api_set_many>,"set_many(items)","Validate all {id,patch} records before commit; duplicate IDs rejected.",&set_many_contract},
    {"find_all",sc_lua_guard<api_find_all>,"find_all(tag) -> ids","Find every entity with a matching tag in stable slot order.",&find_all_contract},
    {"measure",sc_lua_guard<api_measure>,"measure(text,size[,font,wrap]) -> width,height","Measure UTF-8 text using the same lazy font metrics as native rendering.",&sc_core_api::measure},
    {"map",sc_lua_guard<api_map>,"map(layer,x,y[,gid]) -> gid","Read or replace a Tiled layer cell.",&sc_core_api::map},
    {"objects",sc_lua_guard<api_objects>,"objects() -> array","Copy Tiled objects with layer offsets applied; explicit collision objects already belong to static terrain.",&sc_core_api::objects},
    {"spawn", sc_lua_guard<api_spawn>, "spawn(entity) -> id", "Create an entity; unspecified fields use defaults.",&spawn_contract},
    {"spawn_many",sc_lua_guard<api_spawn_many>,"spawn_many(entities,parents?) -> ids","Atomically create a dense batch and optional visual hierarchy in input order. Preflight fields, persistent IDs, capacities, child body/velocity, cycles, depth (32) and composed world poses before any creation. Empty batches return an empty array.",&spawn_many_contract},
    {"get", sc_lua_guard<api_get>, "get(id) -> entity", "Return an independent entity snapshot; stale or unknown handles raise errors.",&get_contract},
    {"set", sc_lua_guard<api_set>, "set(id, patch)", "Atomically apply writable fields; invalid IDs or fields raise errors.",&set_contract},
    {"destroy", sc_lua_guard<api_destroy>, "destroy(id) -> true", "Destroy a live generation-checked entity; returns true on success.",&destroy_contract},
    {"find", sc_lua_guard<api_find>, "find(tag) -> id|nil", "Find the first living entity with an exact nonempty tag, at most 47 bytes.",&find_contract},
    {"overlap", sc_lua_guard<api_overlap>, "overlap(a, b) -> boolean", "Test axis-aligned entity bounds.",&overlap_contract},
    {"key_down", sc_lua_guard<api_key<0>>, "key_down(name) -> boolean", "Read the fixed-tick device input snapshot.",&sc_core_api::key},
    {"key_pressed", sc_lua_guard<api_key<1>>, "key_pressed(name) -> boolean", "Read the fixed-tick device input snapshot.",&sc_core_api::key},
    {"key_released", sc_lua_guard<api_key<2>>, "key_released(name) -> boolean", "Read the fixed-tick device input snapshot.",&sc_core_api::key},
    {"gamepad_down", sc_lua_guard<api_gamepad<0>>, "gamepad_down(name) -> boolean", "Read the fixed-tick device input snapshot.",&sc_core_api::pad},
    {"gamepad_pressed", sc_lua_guard<api_gamepad<1>>, "gamepad_pressed(name) -> boolean", "Read the fixed-tick device input snapshot.",&sc_core_api::pad},
    {"gamepad_released", sc_lua_guard<api_gamepad<2>>, "gamepad_released(name) -> boolean", "Read the fixed-tick device input snapshot.",&sc_core_api::pad},
    {"gamepad_connected", sc_lua_guard<api_gamepad_connected>, "gamepad_connected() -> boolean", "Read the fixed-tick device input snapshot.",&sc_core_api::connected},
    {"gamepad_axis", sc_lua_guard<api_gamepad_axis>, "gamepad_axis(axis, deadzone?) -> number", "Read the fixed-tick device input snapshot.",&sc_core_api::axis},
    {"down", sc_lua_guard<api_down>, "down(action) -> boolean", "Read held input.",&sc_core_api::action},
    {"pressed", sc_lua_guard<api_pressed>, "pressed(action) -> boolean", "Read this tick's press edge.",&sc_core_api::action},
    {"released", sc_lua_guard<api_released>, "released(action) -> boolean", "Read this tick's release edge.",&sc_core_api::action},
    {"tile", sc_lua_guard<api_tile>, "tile(x, y [, char]) -> char", "Read or write a zero-based ASCII map cell; outside reads return # in bounded rooms and . in unbounded worlds.",&sc_core_api::tile},
    {"random", sc_lua_guard<api_random>, "random([min, max]) -> number", "Seeded randomness: [0,1), inclusive integer bounds, or float range.",&sc_core_api::random},
    {"emit", sc_lua_guard<api_emit>, "emit(x, y, count, color [, speed, life])", "Atomically emit 0..32768 particles within project.limits.particles. Exhaustion reports used/requested/capacity without advancing visual RNG. Defaults speed30 life0.5.",&sc_core_api::emit},
    {"tone", sc_lua_guard<api_tone>, "tone(frequency [, duration, volume])", "Queue a synthesized tone; defaults duration0.1 volume0.2.",&sc_core_api::tone},
    {"message", sc_lua_guard<api_message>, "message(text)", "Set the persistent HUD message (191 UTF-8 bytes maximum).",&sc_core_api::message},
    {"scene", sc_lua_guard<api_scene>, "scene(relative_lua_path[,state])", "Request a transactional scene switch after this callback; release any asynchronous save request first.",&sc_core_api::scene},
    {"rect", sc_lua_guard<api_rect>, "rect(x, y, w, h, color [, screen])", "Queue a filled rectangle during draw; screen defaults false.",&sc_core_api::rect},
    {"circle", sc_lua_guard<api_circle>, "circle(x, y, radius, color [, screen])", "Queue a filled circle during draw.",&sc_core_api::circle},
    {"text", sc_lua_guard<api_text>, "text(text, x, y, size, color [, screen, options])", "Queue UTF-8 text (511 bytes) during draw; options font, wrap=0 (0..4096), align=0 (0 left/1 center/2 right).",&sc_core_api::text},
    {"tick", sc_lua_guard<api_tick>, "tick() -> integer", "Return completed fixed simulation steps.",&sc_core_api::tick},
    {"time", sc_lua_guard<api_time>, "time() -> number", "Return fixed simulation time in seconds.",&sc_core_api::time},
    {"log", sc_lua_guard<api_log>, "log(text)", "Write diagnostics to stderr, preserving headless JSON on stdout.",&sc_core_api::log},
    {nullptr, nullptr, nullptr, nullptr}
};

static void load_map(lua_State *L, int index, ScMap *map) {
    static const char *const keys[] = {"tile_size", "rows", "color", "accent", "background", nullptr};
    strict_keys(L, index, keys, "map");
    int_field(L, index, "tile_size", &map->tile_size, 1, 256);
    color_field(L, index, "color", &map->color);
    color_field(L, index, "accent", &map->accent);
    color_field(L, index, "background", &map->background);
    lua_getfield(L, index, "rows");
    size_t height = array_length(L, -1, SC_MAX_TILES, "map.rows");
    if (!height) luaL_error(L, "map.rows must contain at least one row");
    int rows_index = lua_gettop(L);
    size_t width = 0;
    for (size_t row = 0; row < height; ++row) {
        lua_rawgeti(L, rows_index, static_cast<lua_Integer>(row) + 1);
        const char *tiles = string_at(L, -1, SC_MAX_TILES, true);
        size_t length = std::strlen(tiles);
        if (!row) {
            width = length;
            if (width > SC_MAX_TILES / height) luaL_error(L, "map exceeds 16384 cells");
        }
        if (length != width) luaL_error(L, "map.rows must all have equal byte length");
        for (size_t column = 0; column < width; ++column) {
            if (tiles[column] != '.' && tiles[column] != '#' && tiles[column] != '=')
                luaL_error(L, "map.rows permits only '.', '#', and '='");
            map->tiles[row * width + column] = tiles[column];
        }
        lua_pop(L, 1);
    }
    map->width = static_cast<int>(width); map->height = static_cast<int>(height);
    lua_pop(L, 1);
}

static void load_scene(lua_State *L, int index) {
    static const char *const keys[] = {
        "title", "width", "height", "gravity", "ambient", "map", "entities", "preload_images", "init", "update", "draw", "ui_update", nullptr
    };
    strict_keys(L, index, keys, "scene");
    ScWorld *world = script_of(L)->world;
    string_field(L, index, "title", world->title, sizeof world->title);
    int_field(L, index, "width", &world->view_width, 64, 4096);
    int_field(L, index, "height", &world->view_height, 64, 4096);
    float_field(L, index, "gravity", &world->gravity, -1000000, 1000000);
    float_field(L, index, "ambient", &world->ambient, 0, 1);
    lua_getfield(L,index,"preload_images");
    if(!lua_isnil(L,-1)) {
        auto value=sc_lua_read(L,-1);
        if(!value) throw std::runtime_error("scene.preload_images: "+value.error());
        script_of(L)->preload_images=std::move(*value);
    }
    lua_pop(L,1);
    lua_getfield(L, index, "map");
    if(lua_type(L,-1)==LUA_TSTRING) {
        auto* s=script_of(L); const char* path=string_at(L,-1,SC_PATH_MAX-1,true);
        if(!sc_script_validate_path(path)) luaL_error(L,"invalid map path");
        bool ok=false;
        {
            auto result=sc_project_map(world,s->root,path);
            if(result) { s->objects=std::move(*result); ok=true; }
            else std::snprintf(s->error,sizeof s->error,"%s",result.error().c_str());
        }
        if(!ok) luaL_error(L,"map: %s",s->error);
    } else if (!lua_isnil(L, -1)) load_map(L, lua_gettop(L), &world->map);
    lua_pop(L, 1);
    lua_getfield(L, index, "entities");
    if (!lua_isnil(L, -1)) {
        size_t count = array_length(L, -1, world->entities.size(), "entities");
        int entities = lua_gettop(L);
        for (size_t i = 0; i < count; ++i) {
            lua_rawgeti(L, entities, static_cast<lua_Integer>(i) + 1);
            ScEntity entity = entity_defaults();
            entity_patch(L, lua_gettop(L), &entity);
            const char* error=nullptr;
            if (!sc_spawn(world, &entity,&error)) luaL_error(L,"spawn: %s (persistent_id='%s')",error?error:"entity creation failed",entity.persistent_id);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    const char *callbacks[] = {"init", "update", "draw", "ui_update"};
    for (size_t i = 0; i < 4; ++i) {
        lua_getfield(L, index, callbacks[i]);
        if (!lua_isnil(L, -1) && !lua_isfunction(L, -1))
            luaL_error(L, "scene.%s must be a function", callbacks[i]);
        if(i==3) script_of(L)->has_ui_update=lua_isfunction(L,-1);
        lua_pop(L, 1);
    }
}

static int bootstrap(lua_State *L) {
    const luaL_Reg libraries[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}, {nullptr, nullptr}
    };
    for (const luaL_Reg *library = libraries; library->name; ++library) {
        luaL_requiref(L, library->name, library->func, 1); lua_pop(L, 1);
    }
    const char *forbidden[] = {"dofile", "loadfile", "load", nullptr};
    for (size_t i = 0; forbidden[i]; ++i) { lua_pushnil(L); lua_setglobal(L, forbidden[i]); }
    const char *protected_names[] = {"pcall", "xpcall"};
    for (size_t i = 0; i < 2; ++i) {
        lua_getglobal(L, protected_names[i]);
        lua_pushcclosure(L,sc_lua_guard<guarded_protected_call>, 1);
        lua_setglobal(L, protected_names[i]);
    }
    lua_getglobal(L, "setmetatable");
    lua_pushcclosure(L,sc_lua_guard<guarded_setmetatable>, 1); lua_setglobal(L, "setmetatable");
    lua_pushboolean(L, false); lua_rawsetp(L, LUA_REGISTRYINDEX, &draw_phase_key);
    lua_getglobal(L, "math");
    lua_pushnil(L); lua_setfield(L, -2, "random");
    lua_pushnil(L); lua_setfield(L, -2, "randomseed"); lua_pop(L, 1);
    lua_pushcfunction(L,sc_lua_guard<api_log>); lua_setglobal(L, "print");
    lua_newtable(L);
    sc_api_register(L,api);
#ifdef SC_HAS_NETWORK
    sc_net_lua_register(L, network_mutation_guard,script_of(L)->network_sessions);
#else
    lua_newtable(L);
    lua_pushboolean(L, false); lua_setfield(L, -2, "available");
    lua_setfield(L, -2, "net");
#endif
    sc_script_data_register(L);
    sc_script_physics_register(L);
    sc_script_audio_register(L);
    sc_script_systems_register(L);
#ifdef SC_HAS_ADVANCED_RENDER
    sc_script_material_register(L);
    sc_script_lighting_register(L);
#endif
    sc_script_identity_register(L);
    sc_script_camera_register(L);
    sc_script_presentation_register(L);
    lua_setglobal(L, "sc");
    sc_script_project_load(L);
    ScScript *script = script_of(L);
    script->batch_entities.reserve(script->world->entities.size());
    script->batch_parents.reserve(script->world->entities.size());
#ifdef SC_HAS_ADVANCED_RENDER
    script->lighting.occluder_modes.resize(script->world->entities.size());
#endif
    script->batch_seen.reset();
    bool resources_ok=false;
    {
        auto result=sc_project_resources(script->world,script->project,script->root);
        resources_ok=result.has_value();
        if(!result) std::snprintf(script->error,SC_ERROR_MAX,"%s",result.error().c_str());
        if(!std::strcmp(script->entry,"__project_entry__.lua")) {
            auto entry=script->project.get("entry");
            std::snprintf(script->entry,SC_PATH_MAX,"%s",entry?entry->text().c_str():"main.lua");
        }
    }
    if(!resources_ok) return luaL_error(L,"resources: %s",script->error);
    if(!sc_script_validate_path(script->entry)) return luaL_error(L,"invalid project entry");
    char path[SC_PATH_MAX * 2];
    if (std::snprintf(path, sizeof path, "%s/%s", script->root, script->entry) >= static_cast<int>(sizeof path))
        return luaL_error(L, "scene path is too long");
    if (luaL_loadfilex(L, path, "t") != LUA_OK) return lua_error(L);
    lua_call(L, 0, 1);
    if (!lua_istable(L, -1)) return luaL_error(L, "scene must return a table");
    load_scene(L, lua_gettop(L));
    script->module_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static bool protected_call(ScScript *script, int arguments) {
    lua_State *L = script->lua;
    int function_index = lua_gettop(L) - arguments;
    lua_pushcfunction(L,sc_lua_guard<traceback>);
    lua_insert(L, function_index);
    script->instruction_budget = SC_LUA_INSTRUCTIONS;
    int hooks=LUA_MASKCOUNT;
#ifdef SC_HAS_DEVTOOLS
    if(script->debug_hook) hooks|=LUA_MASKLINE|LUA_MASKRET;
#endif
    lua_sethook(L, instruction_hook, hooks, SC_HOOK_INTERVAL);
    int status = lua_pcall(L, arguments, 0, function_index);
    lua_sethook(L, nullptr, 0, 0);
    if (status != LUA_OK) {
        const char *error = lua_tostring(L, -1);
        std::snprintf(script->error, sizeof script->error, "%s: %s", script->entry, error ? error : "unknown Lua failure");
    }
    lua_settop(L, function_index - 1);
    return status == LUA_OK;
}

static int callback_dispatch(lua_State *L) {
    ScScript *script = script_of(L);
    static const char *const names[] = {"init", "update", "draw", "ui_update"};
    const char *name = names[lua_tointeger(L, 1)];
    lua_rawgeti(L, LUA_REGISTRYINDEX, script->module_ref);
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1)) return 0;
    if (!lua_isfunction(L, -1)) return luaL_error(L, "scene.%s must remain a function", name);
    int arguments = lua_gettop(L) >= 4 ? 1 : 0;
    if (arguments) lua_pushvalue(L, 2);
    lua_call(L, arguments, 0);
    return 0;
}

static bool callback(ScScript *script, int callback_id, bool has_argument, float argument, bool drawing) {
    if (!script || !script->lua) return false;
    lua_State *L = script->lua;
    script->error[0] = 0;
    script->phase=callback_id==3?4:callback_id;
    /* This registry key already exists before callbacks; the write does not allocate. */
    lua_pushboolean(L, drawing); lua_rawsetp(L, LUA_REGISTRYINDEX, &draw_phase_key);
    lua_pushcfunction(L,sc_lua_guard<callback_dispatch>);
    lua_pushinteger(L, callback_id);
    if (has_argument) lua_pushnumber(L, argument);
    bool ok = protected_call(script, has_argument ? 2 : 1);
    lua_pushboolean(L, false); lua_rawsetp(L, LUA_REGISTRYINDEX, &draw_phase_key);
    script->phase=0;
    return ok;
}

bool sc_script_open(ScScript *script, ScWorld *world, const char *root, const char *entry) {
    if (!script) return false;
    // Preserve potentially aliased input paths before releasing a previous VM.
    char root_copy[SC_PATH_MAX]{}, entry_copy[SC_PATH_MAX]{};
    const bool valid = world && root && *root && std::strlen(root) < sizeof script->root &&
                       sc_script_validate_path(entry);
    if (valid) {
        std::memcpy(root_copy, root, std::strlen(root) + 1);
        std::memcpy(entry_copy, entry, std::strlen(entry) + 1);
    }
    sc_script_close(script);
    script->module_ref = LUA_NOREF;
    script->world = world;
    script->root[0] = script->entry[0] = script->pending_scene[0] = script->error[0] = '\0';
    script->memory_used = 0;
    script->projectile_limit = 32768;
    script->instruction_budget = 0;
    script->has_ui_update=false;
    script->phase=0;
    if (!valid) {
        std::snprintf(script->error, sizeof script->error, "invalid project root or scene path"); return false;
    }
    std::memcpy(script->root, root_copy, std::strlen(root_copy) + 1);
    std::memcpy(script->entry, entry_copy, std::strlen(entry_copy) + 1);
    script->state_.reset(lua_newstate(script_alloc, script));
    script->lua = script->state_.get();
    if (!script->lua) { std::snprintf(script->error, sizeof script->error, "cannot create Lua VM (16 MiB limit)"); return false; }
    *static_cast<ScScript **>(lua_getextraspace(script->lua)) = script;
    lua_pushcfunction(script->lua, sc_lua_guard<bootstrap>);
    if (!protected_call(script, 0)) { sc_script_close(script); return false; }
    if (!callback(script, 0, false, 0, false) || !sc_script_images_preload(*script)) { sc_script_close(script); return false; }
    return true;
}

void ScScript::LuaCloser::operator()(lua_State *state) const noexcept {
    auto *script = script_of(state);
#ifdef SC_HAS_DEVTOOLS
    script->debug_hook=nullptr; script->debug_context=nullptr;
#endif
    // Keep a quota for any remaining Lua execution during VM teardown.
    script->instruction_budget = SC_LUA_INSTRUCTIONS;
    lua_sethook(state, instruction_hook, LUA_MASKCOUNT, SC_HOOK_INTERVAL);
    lua_close(state);
    script->lua = nullptr;
    script->module_ref = LUA_NOREF;
}

ScScript::~ScScript() noexcept { sc_script_close(this); }

void sc_script_close(ScScript *script) noexcept {
    if (script) {
        script->state_.reset();
#ifdef SC_HAS_ADVANCED_RENDER
        script->materials.reset();
        script->lighting={};
#endif
#ifdef SC_HAS_STREAMING
        script->stream.reset();
        script->images.reset();
        script->initial_images=0;
#endif
        script->preload_images=ScValue{};
        script->navigation_region.reset();
        for(auto& field:script->flow_fields) field.reset();
        script->watches.clear();
        std::vector<ScProjectileSpec>{}.swap(script->projectile_batch);
    }
}

bool sc_script_update(ScScript *script) {
    sc_presentation_capture(*script->world);
#ifdef SC_HAS_NETWORK
    if(script->network_sessions) script->network_sessions->begin_tick();
#endif
    if(!script->has_ui_update) script->world->text_focus=false;
    return callback(script, 1, true, SC_DT, false);
}

bool sc_script_ui_update(ScScript* script,float dt,const ScDeviceInput& input) {
    if(!script||!script->lua) return false;
    if(!script->has_ui_update) return true;
    if(!std::isfinite(dt)||dt<0||dt>.25f) {
        std::snprintf(script->error,sizeof script->error,"UI delta must be finite and within 0..0.25 seconds");
        return false;
    }
    auto& w=*script->world;
    const auto saved=w.input;
    const auto held=w.held,pressed=w.pressed,released=w.released;
    w.input=input; w.held=sc_device_actions(input); w.pressed=w.released=0;
    w.text_focus=false;
    const bool ok=callback(script,3,true,dt,false);
    w.input=saved; w.held=held; w.pressed=pressed; w.released=released;
    return ok;
}

bool sc_script_draw(ScScript *script, float alpha) {
    if (!script || !script->world) return false;
    script->world->draw_count = 0;
#ifdef SC_HAS_ADVANCED_RENDER
    script->lighting.point_count=0;
#endif
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1) {
        std::snprintf(script->error, sizeof script->error, "draw alpha must be within [0, 1]"); return false;
    }
    script->draw_alpha=alpha;
    if(!callback(script,2,true,alpha,true)) return false;
    if(!sc_script_images_validate(*script)) return false;
#ifdef SC_HAS_ADVANCED_RENDER
    ScLightFrame lighting;
    if(auto result=sc_collect_lights(*script->world,script->lighting,lighting,alpha);!result) {
        std::snprintf(script->error,sizeof script->error,"%s",result.error()); return false;
    }
#endif
    int depth=0;
    for(int i=0;i<script->world->draw_count;++i) {
        auto kind=script->world->draws[static_cast<size_t>(i)].kind;
        if(kind==SC_DRAW_CLIP) ++depth;
        if(kind==SC_DRAW_UNCLIP) --depth;
        if(depth&&script->world->draws[static_cast<size_t>(i)].layered) {
            std::snprintf(script->error,sizeof script->error,"scene-layer images cannot be nested inside UI clips"); return false;
        }
        if(depth<0||depth>32) { std::snprintf(script->error,sizeof script->error,"unbalanced clip stack or depth exceeds 32"); return false; }
    }
    if(depth!=0) { std::snprintf(script->error,sizeof script->error,"unbalanced clip stack"); return false; }
    return true;
}

void sc_script_describe(void) {
    std::printf("{\"engine\":\"ShinyCore\",\"version\":\"%s\",\"language\":\"Lua 5.4\",\"fixed_hz\":60,", SC_VERSION);
    std::printf("\"modules\":{");
    bool first_capability=true;
    for(const auto& capability:SC_CAPABILITIES) {
        std::printf("%s\"%.*s\":%s",first_capability?"":",",static_cast<int>(capability.name.size()),capability.name.data(),capability.available?"true":"false");
        first_capability=false;
    }
    std::printf("},");
    std::printf("\"limits\":{\"entities\":4096,\"identities\":4096,\"tiles\":16384,\"particles\":32768,\"projectiles\":32768,\"draws\":4096,\"tones_per_tick\":32,\"sound_voices\":32,\"music_streams\":2,\"state_bytes\":262144,\"state_depth\":16,\"layers\":16,\"compound_shapes\":4,\"joints\":256,\"lua_memory_bytes\":16777216,\"instructions_per_callback\":1000000},");
    auto names=[](const char* label,const auto& list) {
        std::printf("\"%s\":[",label); bool first=true;
        for(const auto& item:list) { std::printf("%s\"%.*s\"",first?"":",",static_cast<int>(item.name.size()),item.name.data()); first=false; }
        std::printf("],");
    };
    names("keys",SC_KEYS); names("gamepad_buttons",SC_BUTTONS); names("gamepad_axes",SC_AXES); names("mouse_buttons",SC_MOUSE_BUTTONS);
    std::printf("\"actions\":[\"left\",\"right\",\"up\",\"down\",\"jump\",\"action\"],");
#ifdef SC_HAS_NETWORK
    std::printf("\"network\":{\"available\":true,\"transport\":\"ENet 1.3.18\",\"max_peers\":32,\"max_payload\":1200,\"max_sessions\":4},");
#else
    std::printf("\"network\":{\"available\":false},");
#endif
    std::printf("\"save\":{\"format\":%d,\"index_bytes\":%zu,\"chunk_bytes\":%zu,\"chunks\":%zu,\"world_bytes\":%zu},",
        SC_SAVE_FORMAT,SC_SAVE_BYTES,SC_STATE_BYTES,SC_SAVE_CHUNKS,SC_SAVE_WORLD_BYTES);
    std::printf("\"scene_fields\":[\"title\",\"width\",\"height\",\"gravity\",\"ambient\",\"map\",\"entities\",\"preload_images\",\"init\",\"update\",\"draw\",\"ui_update\"],");
    std::printf("\"entity_fields\":[");
    for (size_t i=0; SC_ENTITY_KEYS[i]; ++i) std::printf("%s\"%s\"",i?",":"",SC_ENTITY_KEYS[i]);
    std::printf("],\"entity_readonly_fields\":[\"id\",\"grounded\",\"support\",\"normal_x\",\"normal_y\"],\"map_fields\":[\"tile_size\",\"rows\",\"color\",\"accent\",\"background\"],");
    auto types=sc_script_identity_contracts();
    auto& type_fields=std::get<ScValue::Object>(types.data);
#ifdef SC_HAS_NETWORK
    auto network_types=sc_net_lua_contracts();
    type_fields.merge(std::get<ScValue::Object>(network_types.data));
#endif
    auto core_types=sc_core_api::types();
    type_fields.merge(std::get<ScValue::Object>(core_types.data));
    auto presentation_types=sc_script_presentation_contracts();
    type_fields.merge(std::get<ScValue::Object>(presentation_types.data));
    auto projectile_types=sc_script_projectiles_contracts();
    type_fields.merge(std::get<ScValue::Object>(projectile_types.data));
    auto application_types=sc_script_application_contracts();
    type_fields.merge(std::get<ScValue::Object>(application_types.data));
    auto camera_types=sc_script_camera_contracts();
    type_fields.merge(std::get<ScValue::Object>(camera_types.data));
    auto particle_types=sc_script_particles_contracts();
    type_fields.merge(std::get<ScValue::Object>(particle_types.data));
#ifdef SC_HAS_ADVANCED_RENDER
    auto lighting_types=sc_script_lighting_contracts();
    type_fields.merge(std::get<ScValue::Object>(lighting_types.data));
#endif
    auto navigation_types=sc_script_navigation_contracts();
    type_fields.merge(std::get<ScValue::Object>(navigation_types.data));
    auto physics_types=sc_script_physics_contracts();
    type_fields.merge(std::get<ScValue::Object>(physics_types.data));
#ifdef SC_HAS_STREAMING
    auto stream_types=sc_script_stream_contracts();
    type_fields.merge(std::get<ScValue::Object>(stream_types.data));
#endif
    auto save_types=sc_script_save_contracts();
    type_fields.merge(std::get<ScValue::Object>(save_types.data));
    auto image_types=sc_script_images_contracts();
    type_fields.merge(std::get<ScValue::Object>(image_types.data));
    auto input_types=sc_script_input_contracts();
    type_fields.merge(std::get<ScValue::Object>(input_types.data));
    auto audio_types=sc_audio_contracts();
    type_fields.merge(std::get<ScValue::Object>(audio_types.data));
    type_fields.emplace("ScEntityPatch",sc_entity_contract());
    type_fields.emplace("ScEntity",sc_entity_read_contract());
    type_fields.emplace("ScEntityEdit",sc_entity_edit_contract());
    const auto contracts=sc_json_write(types);
    std::printf("\"contract_version\":1,\"types\":%s,",contracts.c_str());
    std::printf("\"colors\":\"#RRGGBB or #RRGGBBAA\",\"coordinates\":\"pixels; map cells are zero-based; positive y points down\",\"functions\":[");
    sc_api_describe(api,"sc.",false);
#ifdef SC_HAS_NETWORK
    sc_net_lua_describe();
#endif
    sc_script_data_describe();
    sc_script_physics_describe();
    sc_script_audio_describe();
    sc_script_systems_describe();
#ifdef SC_HAS_ADVANCED_RENDER
    sc_script_material_describe();
    sc_script_lighting_describe();
#endif
    sc_script_identity_describe();
    sc_script_camera_describe();
    sc_script_presentation_describe();
    std::printf("]}\n");
}
