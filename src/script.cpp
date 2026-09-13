#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/script_physics.h"
#include "shiny/project.h"
#include "shiny/script_audio.h"
#include "shiny/text.h"
#include "shiny/physics.h"
#ifdef SC_HAS_NETWORK
#include "shiny/net_lua.h"
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
    static_cast<void>(ar);
    ScScript *script = script_of(L);
    script->instruction_budget -= SC_HOOK_INTERVAL;
    if (script->instruction_budget <= 0)
        luaL_error(L, "instruction budget exceeded (1000000 per callback)");
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
    if (draw_phase(L)||script_of(L)->phase>=2) luaL_error(L, "simulation mutation is forbidden in draw() or migration; use init() or update()");
}

#ifdef SC_HAS_NETWORK
static int network_mutation_guard(lua_State *L) {
    require_mutable(L);
    return 0;
}
#endif

static void arg_count(lua_State *L, int minimum, int maximum) {
    int count = lua_gettop(L);
    if (count < minimum || count > maximum)
        luaL_error(L, "expected %d to %d arguments, got %d", minimum, maximum, count);
}

static float number_at(lua_State *L, int index, float minimum, float maximum) {
    if (lua_type(L, index) != LUA_TNUMBER) luaL_error(L, "argument/field must be a number");
    lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number) || number < minimum || number > maximum)
        luaL_error(L, "number outside allowed range [%f, %f]", static_cast<double>(minimum), static_cast<double>(maximum));
    return static_cast<float>(number);
}

static lua_Integer integer_at(lua_State *L, int index, lua_Integer minimum, lua_Integer maximum) {
    int valid = 0;
    lua_Integer number = lua_tointegerx(L, index, &valid);
    if (lua_type(L, index) != LUA_TNUMBER || !valid || number < minimum || number > maximum)
        luaL_error(L, "expected integer in [%I, %I]", minimum, maximum);
    return number;
}

static const char *string_at(lua_State *L, int index, size_t maximum, bool nonempty) {
    if (lua_type(L, index) != LUA_TSTRING) luaL_error(L, "argument/field must be a string");
    size_t length;
    const char *value = lua_tolstring(L, index, &length);
    if (length > maximum || (nonempty && !length) || std::memchr(value, 0, length))
        luaL_error(L, "string is empty, too long, or contains a NUL byte");
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

static void float_field(lua_State *L, int index, const char *name, float *out, float min, float max) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) *out = number_at(L, -1, min, max);
    lua_pop(L, 1);
}

static void int_field(lua_State *L, int index, const char *name, int *out, int min, int max) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) *out = static_cast<int>(integer_at(L, -1, min, max));
    lua_pop(L, 1);
}

static void bool_field(lua_State *L, int index, const char *name, bool *out) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TBOOLEAN);
        *out = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
}

static void string_field(lua_State *L, int index, const char *name, char *out, size_t size) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) {
        const char *value = string_at(L, -1, size - 1, false);
        std::memcpy(out, value, std::strlen(value) + 1);
    }
    lua_pop(L, 1);
}

static void color_field(lua_State *L, int index, const char *name, uint32_t *out) {
    lua_getfield(L, index, name);
    if (!lua_isnil(L, -1)) *out = color_at(L, -1);
    lua_pop(L, 1);
}

static const char *const entity_keys[] = {
    "tag", "x", "y", "w", "h", "vx", "vy", "dynamic", "solid", "gravity", "color", "glow",
    "sprite", "frame", "frame_w", "frame_h", "layer", "body", "angle", "angular_velocity", "flip_x", "flip_y", nullptr
};

static void entity_patch(lua_State *L, int index, ScEntity *entity) {
    strict_keys(L, index, entity_keys, "entity");
    float_field(L, index, "x", &entity->x, -1000000, 1000000);
    float_field(L, index, "y", &entity->y, -1000000, 1000000);
    float_field(L, index, "vx", &entity->vx, -1000000, 1000000);
    float_field(L, index, "vy", &entity->vy, -1000000, 1000000);
    float_field(L, index, "w", &entity->w, 0.001f, 4096);
    float_field(L, index, "h", &entity->h, 0.001f, 4096);
    float_field(L, index, "gravity", &entity->gravity, -100, 100);
    float_field(L, index, "glow", &entity->glow, 0, 1024);
    bool_field(L, index, "dynamic", &entity->dynamic);
    bool_field(L, index, "solid", &entity->solid);
    color_field(L, index, "color", &entity->color);
    string_field(L, index, "tag", entity->tag, sizeof entity->tag);
    string_field(L, index, "sprite", entity->sprite, sizeof entity->sprite);
    for(const auto& resource:script_of(L)->world->resources) if(resource.type=="image"&&resource.name==entity->sprite) {
        std::snprintf(entity->sprite,sizeof entity->sprite,"%s",resource.path.c_str()); break;
    }
    int_field(L, index, "frame", &entity->frame, 0, 65535);
    int_field(L, index, "frame_w", &entity->frame_w, 0, 4096);
    int_field(L, index, "frame_h", &entity->frame_h, 0, 4096);
    int_field(L, index, "layer", &entity->layer, -32768, 32767);
    float_field(L,index,"angle",&entity->angle,-1000000,1000000);
    float_field(L,index,"angular_velocity",&entity->angular_velocity,-1000,1000);
    bool_field(L,index,"flip_x",&entity->flip_x); bool_field(L,index,"flip_y",&entity->flip_y);
    sc_script_body_patch(L,index,entity);
    if(!sc_physics_body_valid(*entity)) luaL_error(L,"invalid body: polygons require 3..8 distinct convex vertices");
    if (*entity->sprite && !sc_script_validate_path(entity->sprite))
        luaL_error(L, "sprite must be a project-relative path without '.' or '..' segments");
    if ((entity->frame_w == 0) != (entity->frame_h == 0))
        luaL_error(L, "frame_w and frame_h must both be positive or both zero");
}

static ScEntity entity_defaults(void) {
    ScEntity entity{};
    entity.w = entity.h = 8;
    entity.gravity = 1;
    entity.solid = true;
    entity.color = 0xffffffffu;
    return entity;
}

static uint32_t id_at(lua_State *L, int index) {
    return static_cast<uint32_t>(integer_at(L, index, 1, UINT32_MAX));
}

static ScEntity *entity_at(lua_State *L, int index) {
    uint32_t id = id_at(L, index);
    ScEntity *entity = sc_entity(script_of(L)->world, id);
    if (!entity) luaL_error(L, "entity ID %I is stale or unknown", static_cast<lua_Integer>(id));
    return entity;
}

static int api_spawn(lua_State *L) {
    arg_count(L, 1, 1); require_mutable(L);
    ScEntity entity = entity_defaults();
    entity_patch(L, 1, &entity);
    uint32_t id = sc_spawn(script_of(L)->world, &entity);
    if (!id) return luaL_error(L, "entity capacity exhausted (256)");
    lua_pushinteger(L, id);
    return 1;
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

static int api_get(lua_State *L) {
    arg_count(L, 1, 1);
    const ScEntity *entity = entity_at(L, 1);
    lua_createtable(L, 0, 21);
    push_integer_field(L, "id", entity->id);
    push_bool_field(L, "grounded", entity->grounded);
    push_bool_field(L, "dynamic", entity->dynamic);
    push_bool_field(L, "solid", entity->solid);
    push_number_field(L, "x", entity->x); push_number_field(L, "y", entity->y);
    push_number_field(L, "w", entity->w); push_number_field(L, "h", entity->h);
    push_number_field(L, "vx", entity->vx); push_number_field(L, "vy", entity->vy);
    push_number_field(L, "gravity", entity->gravity); push_number_field(L, "glow", entity->glow);
    push_integer_field(L, "frame", entity->frame); push_integer_field(L, "frame_w", entity->frame_w);
    push_integer_field(L, "frame_h", entity->frame_h); push_integer_field(L, "layer", entity->layer);
    lua_pushstring(L, entity->tag); lua_setfield(L, -2, "tag");
    lua_pushstring(L, entity->sprite); lua_setfield(L, -2, "sprite");
    char color[10];
    std::snprintf(color, sizeof color, "#%08x", entity->color);
    lua_pushstring(L, color); lua_setfield(L, -2, "color");
    sc_script_body_push(L,entity);
    return 1;
}

static int api_set(lua_State *L) {
    arg_count(L, 2, 2); require_mutable(L);
    ScEntity *entity = entity_at(L, 1), patched = *entity;
    entity_patch(L, 2, &patched);
    *entity = patched;
    return 0;
}

static int api_destroy(lua_State *L) {
    arg_count(L, 1, 1); require_mutable(L);
    uint32_t id = entity_at(L, 1)->id;
    lua_pushboolean(L, sc_destroy(script_of(L)->world, id));
    return 1;
}

static int api_find(lua_State *L) {
    arg_count(L, 1, 1);
    uint32_t id = sc_find(script_of(L)->world, string_at(L, 1, 47, true));
    if (id) lua_pushinteger(L, id); else lua_pushnil(L);
    return 1;
}

static int api_objects(lua_State* L) { sc_lua_push(L,script_of(L)->objects); return 1; }
static int api_map(lua_State* L) {
    arg_count(L,3,4); auto* w=script_of(L)->world;
    const char* name=string_at(L,1,128,true); ScLayer* layer=nullptr;
    for(auto& item:w->layers) if(item.name==name) layer=&item;
    if(!layer) return luaL_error(L,"unknown map layer: %s",name);
    int x=static_cast<int>(integer_at(L,2,0,w->map.width-1)),y=static_cast<int>(integer_at(L,3,0,w->map.height-1));
    auto& cell=layer->cells[static_cast<size_t>(y*w->map.width+x)];
    if(lua_gettop(L)==4) {
        require_mutable(L); auto gid=static_cast<uint32_t>(integer_at(L,4,0,UINT32_MAX));
        if(gid&&!sc_tile_graphic(w,gid)) return luaL_error(L,"unknown map GID");
        auto old=cell; cell=gid; bool ok=false;
        { auto result=sc_project_tiles(w); ok=result.has_value(); }
        if(!ok) { cell=old; return luaL_error(L,"map collider capacity or geometry error"); }
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
        world->map.tiles[static_cast<std::size_t>(y * world->map.width + x)] = *tile;
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
    float life = lua_gettop(L) >= 6 ? number_at(L, 6, 0.001f, 60) : 0.5f;
    sc_emit(script_of(L)->world, x, y, count, color, speed, life); return 0;
}

static int api_tone(lua_State *L) {
    arg_count(L, 1, 3); require_mutable(L);
    ScTone tone{};
    tone.frequency = number_at(L, 1, 20, 20000);
    tone.duration = lua_gettop(L) >= 2 ? number_at(L, 2, 0.001f, 10) : 0.1f;
    tone.volume = lua_gettop(L) >= 3 ? number_at(L, 3, 0, 1) : 0.2f;
    ScWorld *world = script_of(L)->world;
    if (world->tone_count >= SC_MAX_TONES) return luaL_error(L, "tone queue capacity exhausted (32 per tick)");
    world->tones[static_cast<std::size_t>(world->tone_count++)] = tone; return 0;
}

static int api_camera(lua_State *L) {
    arg_count(L, 1, 2); require_mutable(L);
    ScWorld *world = script_of(L)->world;
    if (lua_gettop(L) == 1) {
        const ScEntity *entity = entity_at(L, 1);
        world->camera_target = entity->id;
        world->camera_x = entity->x + entity->w * 0.5f - static_cast<float>(world->view_width) * 0.5f;
        world->camera_y = entity->y + entity->h * 0.5f - static_cast<float>(world->view_height) * 0.5f;
    } else {
        float x = number_at(L, 1, -1000000, 1000000), y = number_at(L, 2, -1000000, 1000000);
        world->camera_target = 0; world->camera_x = x; world->camera_y = y;
    }
    return 0;
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
    if (world->draw_count >= SC_MAX_DRAWS) luaL_error(L, "draw queue capacity exhausted (512)");
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
        std::fputs("[shiny] missing text glyph; using '?'. Declare characters in a project font resource.\n",stderr);
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

static const ScLuaApi api[] = {
    {"measure",sc_lua_guard<api_measure>,"measure(text,size[,font,wrap]) -> width,height","Measure UTF-8 text using declared CPU font metrics."},
    {"map",sc_lua_guard<api_map>,"map(layer,x,y[,gid]) -> gid","Read or replace a Tiled layer cell."},
    {"objects",sc_lua_guard<api_objects>,"objects() -> array","Copy authored Tiled objects for Lua factories."},
    {"spawn", sc_lua_guard<api_spawn>, "spawn(entity) -> id", "Create an entity; unspecified fields use defaults."},
    {"get", sc_lua_guard<api_get>, "get(id) -> entity", "Return a copy with read-only id and grounded fields."},
    {"set", sc_lua_guard<api_set>, "set(id, patch)", "Atomically apply writable fields; invalid IDs or fields raise errors."},
    {"destroy", sc_lua_guard<api_destroy>, "destroy(id) -> true", "Destroy a live generation-checked entity."},
    {"find", sc_lua_guard<api_find>, "find(tag) -> id|nil", "Find the first living entity with an exact tag."},
    {"overlap", sc_lua_guard<api_overlap>, "overlap(a, b) -> boolean", "Test axis-aligned entity bounds."},
    {"key_down", sc_lua_guard<api_key<0>>, "key_down(name) -> boolean", "Read the fixed-tick device input snapshot."},
    {"key_pressed", sc_lua_guard<api_key<1>>, "key_pressed(name) -> boolean", "Read the fixed-tick device input snapshot."},
    {"key_released", sc_lua_guard<api_key<2>>, "key_released(name) -> boolean", "Read the fixed-tick device input snapshot."},
    {"gamepad_down", sc_lua_guard<api_gamepad<0>>, "gamepad_down(name) -> boolean", "Read the fixed-tick device input snapshot."},
    {"gamepad_pressed", sc_lua_guard<api_gamepad<1>>, "gamepad_pressed(name) -> boolean", "Read the fixed-tick device input snapshot."},
    {"gamepad_released", sc_lua_guard<api_gamepad<2>>, "gamepad_released(name) -> boolean", "Read the fixed-tick device input snapshot."},
    {"gamepad_connected", sc_lua_guard<api_gamepad_connected>, "gamepad_connected() -> boolean", "Read the fixed-tick device input snapshot."},
    {"gamepad_axis", sc_lua_guard<api_gamepad_axis>, "gamepad_axis(axis, deadzone?) -> number", "Read the fixed-tick device input snapshot."},
    {"down", sc_lua_guard<api_down>, "down(action) -> boolean", "Read held input."},
    {"pressed", sc_lua_guard<api_pressed>, "pressed(action) -> boolean", "Read this tick's press edge."},
    {"released", sc_lua_guard<api_released>, "released(action) -> boolean", "Read this tick's release edge."},
    {"tile", sc_lua_guard<api_tile>, "tile(x, y [, char]) -> char", "Read or write a zero-based map cell; outside reads return #."},
    {"random", sc_lua_guard<api_random>, "random([min, max]) -> number", "Seeded randomness: [0,1), inclusive integer bounds, or float range."},
    {"emit", sc_lua_guard<api_emit>, "emit(x, y, count, color [, speed, life])", "Emit at most 1024 bounded particles; defaults speed30 life0.5."},
    {"tone", sc_lua_guard<api_tone>, "tone(frequency [, duration, volume])", "Queue a synthesized tone; defaults duration0.1 volume0.2."},
    {"camera", sc_lua_guard<api_camera>, "camera(id) | camera(x, y)", "Follow an entity or set the world-space top-left view position."},
    {"message", sc_lua_guard<api_message>, "message(text)", "Set the persistent HUD message (191 UTF-8 bytes maximum)."},
    {"scene", sc_lua_guard<api_scene>, "scene(relative_lua_path[,state])", "Request a transactional scene switch after this callback."},
    {"rect", sc_lua_guard<api_rect>, "rect(x, y, w, h, color [, screen])", "Queue a filled rectangle during draw; screen defaults false."},
    {"circle", sc_lua_guard<api_circle>, "circle(x, y, radius, color [, screen])", "Queue a filled circle during draw."},
    {"text", sc_lua_guard<api_text>, "text(text, x, y, size, color [, screen, options])", "Queue UTF-8 text (511 bytes) during draw; options font, wrap=0 (0..4096), align=0 (0 left/1 center/2 right)."},
    {"tick", sc_lua_guard<api_tick>, "tick() -> integer", "Return completed fixed simulation steps."},
    {"time", sc_lua_guard<api_time>, "time() -> number", "Return fixed simulation time in seconds."},
    {"log", sc_lua_guard<api_log>, "log(text)", "Write diagnostics to stderr, preserving headless JSON on stdout."},
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
        "title", "width", "height", "gravity", "ambient", "map", "entities", "init", "update", "draw", nullptr
    };
    strict_keys(L, index, keys, "scene");
    ScWorld *world = script_of(L)->world;
    string_field(L, index, "title", world->title, sizeof world->title);
    int_field(L, index, "width", &world->view_width, 64, 4096);
    int_field(L, index, "height", &world->view_height, 64, 4096);
    float_field(L, index, "gravity", &world->gravity, -1000000, 1000000);
    float_field(L, index, "ambient", &world->ambient, 0, 1);
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
        size_t count = array_length(L, -1, SC_MAX_ENTITIES, "entities");
        int entities = lua_gettop(L);
        for (size_t i = 0; i < count; ++i) {
            lua_rawgeti(L, entities, static_cast<lua_Integer>(i) + 1);
            ScEntity entity = entity_defaults();
            entity_patch(L, lua_gettop(L), &entity);
            if (!sc_spawn(world, &entity)) luaL_error(L, "entity capacity exhausted (256)");
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    const char *callbacks[] = {"init", "update", "draw"};
    for (size_t i = 0; i < 3; ++i) {
        lua_getfield(L, index, callbacks[i]);
        if (!lua_isnil(L, -1) && !lua_isfunction(L, -1))
            luaL_error(L, "scene.%s must be a function", callbacks[i]);
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
    sc_net_lua_register(L, network_mutation_guard);
#else
    lua_newtable(L);
    lua_pushboolean(L, false); lua_setfield(L, -2, "available");
    lua_setfield(L, -2, "net");
#endif
    sc_script_data_register(L);
    sc_script_physics_register(L);
    sc_script_audio_register(L);
    lua_setglobal(L, "sc");
    sc_script_project_load(L);
    ScScript *script = script_of(L);
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
    lua_sethook(L, instruction_hook, LUA_MASKCOUNT, SC_HOOK_INTERVAL);
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
    static const char *const names[] = {"init", "update", "draw"};
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
    script->phase=callback_id;
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
    script->instruction_budget = 0;
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
    lua_pushcfunction(script->lua, bootstrap);
    if (!protected_call(script, 0)) { sc_script_close(script); return false; }
    if (!callback(script, 0, false, 0, false)) { sc_script_close(script); return false; }
    return true;
}

void ScScript::LuaCloser::operator()(lua_State *state) const noexcept {
    auto *script = script_of(state);
    // Keep a quota for any remaining Lua execution during VM teardown.
    script->instruction_budget = SC_LUA_INSTRUCTIONS;
    lua_sethook(state, instruction_hook, LUA_MASKCOUNT, SC_HOOK_INTERVAL);
    lua_close(state);
    script->lua = nullptr;
    script->module_ref = LUA_NOREF;
}

ScScript::~ScScript() noexcept { sc_script_close(this); }

void sc_script_close(ScScript *script) noexcept {
    if (script) script->state_.reset();
}

bool sc_script_update(ScScript *script) { return callback(script, 1, true, SC_DT, false); }

bool sc_script_draw(ScScript *script, float alpha) {
    if (!script || !script->world) return false;
    script->world->draw_count = 0;
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1) {
        std::snprintf(script->error, sizeof script->error, "draw alpha must be within [0, 1]"); return false;
    }
    return callback(script, 2, true, alpha, true);
}

void sc_script_describe(void) {
    std::printf("{\"engine\":\"ShinyCore\",\"version\":\"%s\",\"language\":\"Lua 5.4\",\"fixed_hz\":60,", SC_VERSION);
    std::printf("\"limits\":{\"entities\":256,\"tiles\":16384,\"particles\":1024,\"draws\":512,\"tones_per_tick\":32,\"sound_voices\":32,\"music_streams\":2,\"state_bytes\":262144,\"state_depth\":16,\"layers\":16,\"compound_shapes\":4,\"joints\":256,\"lua_memory_bytes\":16777216,\"instructions_per_callback\":1000000},");
    auto names=[](const char* label,const auto& list) {
        std::printf("\"%s\":[",label); bool first=true;
        for(const auto& item:list) { std::printf("%s\"%.*s\"",first?"":",",static_cast<int>(item.name.size()),item.name.data()); first=false; }
        std::printf("],");
    };
    names("keys",SC_KEYS); names("gamepad_buttons",SC_BUTTONS); names("gamepad_axes",SC_AXES);
    std::printf("\"actions\":[\"left\",\"right\",\"up\",\"down\",\"jump\",\"action\"],");
#ifdef SC_HAS_NETWORK
    std::printf("\"network\":{\"available\":true,\"transport\":\"ENet 1.3.18\",\"max_peers\":32,\"max_payload\":1200,\"max_sessions\":4},");
#else
    std::printf("\"network\":{\"available\":false},");
#endif
    std::printf("\"scene_fields\":[\"title\",\"width\",\"height\",\"gravity\",\"ambient\",\"map\",\"entities\",\"init\",\"update\",\"draw\"],");
    std::printf("\"entity_fields\":[");
    for (size_t i = 0; entity_keys[i]; ++i) std::printf("%s\"%s\"", i ? "," : "", entity_keys[i]);
    std::printf("],\"entity_readonly_fields\":[\"id\",\"grounded\",\"support\",\"normal_x\",\"normal_y\"],\"map_fields\":[\"tile_size\",\"rows\",\"color\",\"accent\",\"background\"],");
    std::printf("\"colors\":\"#RRGGBB or #RRGGBBAA\",\"coordinates\":\"pixels; map cells are zero-based; positive y points down\",\"functions\":[");
    sc_api_describe(api,"sc.",false);
#ifdef SC_HAS_NETWORK
    sc_net_lua_describe();
#endif
    sc_script_data_describe();
    sc_script_physics_describe();
    sc_script_audio_describe();
    std::printf("]}\n");
}
