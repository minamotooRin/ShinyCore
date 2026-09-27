#include "shiny/core.h"
#include "shiny/physics.h"
#include "shiny/projectiles.h"
#include "shiny/profile.h"
#include "shiny/identity.h"
#include "shiny/attachment.h"
#include "../profile_clock.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <span>

/* Fixed-capacity entities own a single Box2D world; only fixed ticks advance it. */
static constexpr float SC_VALUE_LIMIT = 1000000.0f;
static constexpr std::uint32_t SC_GENERATION_MASK = 0x0000ffffu;
static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559,
              "the stable state hash requires IEEE 754 binary32 floats");

static float clamp_float(double value, double minimum, double maximum) {
    return static_cast<float>(std::clamp(value, minimum, maximum));
}

static bool map_valid(const ScMap *map) {
    return map->width > 0 && map->height > 0 && map->tile_size > 0 &&
           map->height <= SC_MAX_TILES &&
           map->width <= SC_MAX_TILES / map->height;
}

static bool entity_valid(const ScEntity *entity) {
    return entity && std::isfinite(entity->x) && std::isfinite(entity->y) &&
           std::isfinite(entity->vx) && std::isfinite(entity->vy) &&
           std::fabs(entity->x) <= SC_VALUE_LIMIT && std::fabs(entity->y) <= SC_VALUE_LIMIT &&
           std::fabs(entity->vx) <= SC_VALUE_LIMIT && std::fabs(entity->vy) <= SC_VALUE_LIMIT &&
           std::isfinite(entity->w) && std::isfinite(entity->h) &&
           entity->w > 0 && entity->w <= 4096 && entity->h > 0 && entity->h <= 4096 &&
           std::isfinite(entity->gravity) && std::fabs(entity->gravity) <= 100 &&
           std::isfinite(entity->glow) && entity->glow >= 0 && entity->glow <= 1024;
}

void sc_world_init(ScWorld *world, std::uint32_t seed) {
    if (!world) return;
    *world = ScWorld{};
    if (seed) world->rng = seed;
}

ScEntityId sc_spawn(ScWorld *world, const ScEntity *entity,const char** error) {
    auto fail=[&](const char* reason)->ScEntityId { if(error) *error=reason; return 0; };
    if(error) *error=nullptr;
    if (!world || !entity_valid(entity) || !sc_physics_body_valid(*entity)) return fail("invalid entity geometry");
    const auto* end=static_cast<const char*>(std::memchr(entity->persistent_id,0,sizeof entity->persistent_id));
    if(!end) return fail("persistent_id is too long");
    std::string_view persistent_id(entity->persistent_id,static_cast<std::size_t>(end-entity->persistent_id));
    if(!persistent_id.empty() && !sc_identity_name_valid(persistent_id)) return fail("invalid persistent_id");
    for (std::size_t i = 0; i < world->entities.size(); ++i) {
        if (world->entities[i].alive) continue;
        /* Copy first: callers may pass an entity in this world's own pool. */
        ScEntity copy = *entity;
        copy.parent=0; copy.local_pose={}; // A new handle never inherits runtime relationships.
        std::uint32_t generation = world->generations[i] & SC_GENERATION_MASK;
        if (!generation) continue; // Exhausted slots never resurrect stale handles.
        copy.id = (ScEntityId{world->epoch} << 32) | (ScEntityId{generation} << 16) | i;
        if(!persistent_id.empty()) {
            if(!world->identities) return fail("persistent object registry is disabled");
            if(auto result=world->identities->bind(persistent_id,copy.id);!result) return fail(result.error());
        }
        world->generations[i] = generation;
        copy.alive = true;
        copy.grounded = false;
        copy.tag[sizeof(copy.tag) - 1] = '\0';
        copy.sprite[sizeof(copy.sprite) - 1] = '\0';
        world->entities[i] = copy;
        return copy.id;
    }
    return fail("entity capacity exhausted");
}

std::expected<void,const char*> sc_spawn_preflight(const ScWorld& world,std::span<const ScEntity> drafts) {
    if(drafts.empty()) return {};
    const std::less<const ScEntity*> before;
    if(!world.entities.empty()&&before(drafts.data(),world.entities.data()+world.entities.size())&&
       before(world.entities.data(),drafts.data()+drafts.size()))
        return std::unexpected("spawn drafts must not alias world entities");
    std::size_t available=0,new_names=0;
    for(std::size_t i=0;i<world.entities.size();++i)
        if(!world.entities[i].alive&&(world.generations[i]&SC_GENERATION_MASK)) ++available;
    if(drafts.size()>available) return std::unexpected("entity capacity exhausted");
    for(std::size_t i=0;i<drafts.size();++i) {
        const auto& entity=drafts[i];
        if(!entity_valid(&entity)||!sc_physics_body_valid(entity)) return std::unexpected("invalid entity geometry");
        const auto* end=static_cast<const char*>(std::memchr(entity.persistent_id,0,sizeof entity.persistent_id));
        if(!end) return std::unexpected("persistent_id is too long");
        const std::string_view name(entity.persistent_id,static_cast<std::size_t>(end-entity.persistent_id));
        if(name.empty()) continue;
        if(!sc_identity_name_valid(name)) return std::unexpected("invalid persistent_id");
        if(!world.identities) return std::unexpected("persistent object registry is disabled");
        const auto* record=world.identities->find(name);
        if(record&&record->status==ScIdentityStatus::active) return std::unexpected("persistent_id is already active");
        for(std::size_t j=0;j<i;++j)
            if(name==drafts[j].persistent_id) return std::unexpected("duplicate persistent_id in spawn batch");
        if(!record) ++new_names;
    }
    if(world.identities&&new_names>world.identities->capacity()-world.identities->size())
        return std::unexpected("persistent object capacity exhausted");
    return {};
}
std::expected<void,const char*> sc_spawn_many(ScWorld& world,std::span<ScEntity> drafts,std::span<const std::size_t> parents) {
    if(auto result=sc_spawn_preflight(world,drafts);!result) return result;
    if(auto result=sc_attachment_batch_preflight(drafts,parents);!result) return result;
    // Preflight covers every failure of sc_spawn. No callbacks, allocations or
    // solver steps occur between validation and commit.
    for(auto& draft:drafts) draft.id=sc_spawn(&world,&draft);
    for(std::size_t i=0;i<parents.size();++i) if(parents[i]) {
        auto* child=sc_entity(&world,drafts[i].id);
        child->parent=drafts[parents[i]-1].id;
        child->local_pose={drafts[i].x,drafts[i].y,drafts[i].angle};
    }
    if(!parents.empty()) sc_attachments_sync(world);
    return {};
}

ScEntity *sc_entity(ScWorld *world, ScEntityId id) {
    if (!world || !id) return nullptr;
    auto slot = sc_entity_slot(id);
    if (slot >= world->entities.size()) return nullptr;
    ScEntity *entity = &world->entities[slot];
    return entity->alive && entity->id == id && world->generations[slot] == ((id >> 16) & 65535) && world->epoch == (id >> 32)
               ? entity : nullptr;
}

bool sc_destroy(ScWorld *world, ScEntityId id,bool deleted) {
    ScEntity *entity = sc_entity(world, id);
    if (!entity) return false;
    sc_attachments_sync(*world);
    for(auto& child:world->entities) if(child.alive&&child.parent==id) {
        child.parent=0; child.local_pose={}; sc_presentation_snap(*world,child.id);
    }
    if(world->identities && entity->persistent_id[0]) world->identities->release(entity->persistent_id,id,deleted);
    auto slot = sc_entity_slot(id);
    *entity = ScEntity{};
    std::uint32_t generation = (world->generations[slot] + 1) & SC_GENERATION_MASK;
    world->generations[slot] = generation;
    if (world->camera_target == id) world->camera_target = 0;
    return true;
}

ScEntityId sc_find(const ScWorld *world, const char *tag) {
    if (!world || !tag || !*tag) return 0;
    for (const auto &entity : world->entities) {
        if (entity.alive && std::strncmp(entity.tag, tag, sizeof(entity.tag)) == 0)
            return entity.id;
    }
    return 0;
}

void sc_input(ScWorld *world, std::uint32_t held) {
    if (!world) return;
    held &= SC_LEFT | SC_RIGHT | SC_UP | SC_DOWN | SC_JUMP | SC_INTERACT;
    world->pressed = held & ~world->held;
    world->released = world->held & ~held;
    world->held = held;
}

char sc_tile(const ScWorld *world, int x, int y) {
    if (!world || !map_valid(&world->map)) return '#';
    if (x < 0 || y < 0 || x >= world->map.width || y >= world->map.height) return world->map.bounded?'#':'.';
    return world->map.tiles[static_cast<std::size_t>(y * world->map.width + x)];
}

bool sc_overlap(const ScEntity *a, const ScEntity *b) {
    if (!a || !b || !a->alive || !b->alive || !entity_valid(a) || !entity_valid(b))
        return false;
    return static_cast<double>(a->x) < static_cast<double>(b->x) + b->w &&
           static_cast<double>(a->x) + a->w > b->x &&
           static_cast<double>(a->y) < static_cast<double>(b->y) + b->h &&
           static_cast<double>(a->y) + a->h > b->y;
}

std::uint32_t sc_random_u32(ScWorld *world) {
    if (!world) return 0;
    std::uint32_t value = world->rng ? world->rng : SC_DEFAULT_SEED;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    world->rng = value;
    return value;
}

float sc_random(ScWorld *world) {
    return static_cast<float>(sc_random_u32(world) >> 8) * (1.0f / 16777216.0f);
}

std::expected<std::size_t,const char*> sc_emit(ScWorld* world,float x,float y,int count,
    std::uint32_t color,float speed,float life) {
    if(!world||count<0) return std::unexpected("invalid particle world or count");
    return world->particles.emit(world->visual_rng,x,y,static_cast<std::size_t>(count),color,speed,life);
}

void sc_step(ScWorld *world, ScStepProfile* profile) {
    if (!world) return;
    sc_presentation_capture(*world);
    if (!world->simulation_paused) {
        { ScProfileScope timing(profile?&profile->physics_ms:nullptr); sc_physics_step(world); }
        {
            ScProfileScope timing(profile?&profile->entities_ms:nullptr);
            for(auto& e:world->entities) if(e.alive&&!e.parent&&!e.body_type&&!e.dynamic) {
                e.x=clamp_float(e.x+static_cast<double>(e.vx)*SC_DT,-SC_VALUE_LIMIT,SC_VALUE_LIMIT);
                e.y=clamp_float(e.y+static_cast<double>(e.vy)*SC_DT,-SC_VALUE_LIMIT,SC_VALUE_LIMIT);
            }
            sc_attachments_sync(*world);
        }
        ScProfileScope timing(profile?&profile->projectiles_ms:nullptr);
        if(world->projectiles) world->projectiles->step(*world);
    }
    if(world->simulation_paused) sc_attachments_sync(*world);
    { ScProfileScope timing(profile?&profile->particles_ms:nullptr);
      if (!world->simulation_paused) world->particles.step(world->gravity,SC_DT); }
    sc_camera_step(*world);
    ++world->tick;
}

/* Explicit little-endian FNV-1a serialization hashes simulation fields, never C
 * structure padding, unused pool bytes, addresses, or transient draw/tone queues.
 * Equal signed zeros hash equally, as do NaNs inserted through the native API. */
static void hash_byte(std::uint64_t *hash, std::uint8_t value) {
    *hash ^= value;
    *hash *= UINT64_C(1099511628211);
}

static void hash_u32(std::uint64_t *hash, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) hash_byte(hash, static_cast<std::uint8_t>(value >> (i * 8)));
}

static void hash_u64(std::uint64_t *hash, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) hash_byte(hash, static_cast<std::uint8_t>(value >> (i * 8)));
}

static void hash_float(std::uint64_t *hash, float value) {
    std::uint32_t bits;
    if (value == 0) bits = 0;
    else if (std::isnan(value)) bits = UINT32_C(0x7fc00000);
    else bits = std::bit_cast<std::uint32_t>(value);
    hash_u32(hash, bits);
}

static void hash_string(std::uint64_t *hash, std::span<const char> value) {
    const auto end = std::ranges::find(value, '\0');
    const auto length = static_cast<std::size_t>(end - value.begin());
    hash_u32(hash, static_cast<std::uint32_t>(length));
    for (std::size_t i = 0; i < length; ++i) hash_byte(hash, static_cast<std::uint8_t>(value[i]));
}

std::uint64_t sc_state_hash(const ScWorld *world) {
    if (!world) return 0;
    std::uint64_t hash = UINT64_C(14695981039346656037);
    const ScMap *map = &world->map;
    hash_byte(&hash,map->bounded);
    hash_u32(&hash, static_cast<std::uint32_t>(map->width));
    hash_u32(&hash, static_cast<std::uint32_t>(map->height));
    hash_u32(&hash, static_cast<std::uint32_t>(map->tile_size));
    hash_u32(&hash, map->color);
    hash_u32(&hash, map->accent);
    hash_u32(&hash, map->background);
    if (map_valid(map)) {
        const auto count = static_cast<std::size_t>(map->width * map->height);
        for (const char tile : std::span{map->tiles}.first(count))
            hash_byte(&hash, static_cast<std::uint8_t>(tile));
    }
    for (std::size_t i = 0; i < world->entities.size(); ++i) {
        const ScEntity *entity = &world->entities[i];
        hash_u32(&hash, world->generations[i]);
        hash_byte(&hash, entity->alive);
        if (!entity->alive) continue;
        hash_u64(&hash, entity->id);
        if(entity->parent) {
            hash_u64(&hash,entity->parent);
            hash_float(&hash,entity->local_pose.x); hash_float(&hash,entity->local_pose.y);
            hash_float(&hash,entity->local_pose.angle);
        }
        hash_byte(&hash, entity->dynamic);
        hash_byte(&hash, entity->solid);
        hash_byte(&hash, entity->grounded);
        hash_float(&hash, entity->x); hash_float(&hash, entity->y);
        hash_float(&hash, entity->w); hash_float(&hash, entity->h);
        hash_float(&hash, entity->vx); hash_float(&hash, entity->vy);
        hash_float(&hash, entity->gravity); hash_float(&hash, entity->glow);
        hash_u32(&hash, entity->color);
        hash_string(&hash, entity->tag);
        hash_string(&hash, entity->persistent_id);
        hash_string(&hash, entity->sprite);
        hash_u32(&hash, static_cast<std::uint32_t>(entity->frame));
        hash_u32(&hash, static_cast<std::uint32_t>(entity->frame_w));
        hash_u32(&hash, static_cast<std::uint32_t>(entity->frame_h));
        hash_u32(&hash, static_cast<std::uint32_t>(entity->layer));
        hash_u32(&hash,static_cast<uint32_t>(entity->body_type)); hash_u32(&hash,static_cast<uint32_t>(entity->shape));
        hash_float(&hash,entity->angle); hash_float(&hash,entity->angular_velocity);
        hash_float(&hash,entity->density); hash_float(&hash,entity->friction); hash_float(&hash,entity->restitution);
        hash_float(&hash,entity->drop_time); hash_float(&hash,entity->normal_x); hash_float(&hash,entity->normal_y);
        hash_u32(&hash,entity->category); hash_u32(&hash,entity->mask); hash_u64(&hash,entity->support);
        hash_byte(&hash,entity->sensor); hash_byte(&hash,entity->bullet); hash_byte(&hash,entity->fixed_rotation);
        hash_byte(&hash,entity->one_way); hash_byte(&hash,entity->flip_x); hash_byte(&hash,entity->flip_y);
        hash_u32(&hash,static_cast<uint32_t>(entity->vertex_count));
        for(int j=0;j<entity->vertex_count*2;++j) hash_float(&hash,entity->vertices[static_cast<size_t>(j)]);
        hash_u32(&hash,static_cast<uint32_t>(entity->shape_count));
        for(int j=0;j<entity->shape_count;++j) {
            const auto& shape=entity->shapes[static_cast<size_t>(j)];
            hash_u32(&hash,static_cast<uint32_t>(shape.kind));
            hash_float(&hash,shape.x); hash_float(&hash,shape.y); hash_float(&hash,shape.w); hash_float(&hash,shape.h);
            hash_u32(&hash,static_cast<uint32_t>(shape.vertex_count));
            for(int k=0;k<shape.vertex_count*2;++k) hash_float(&hash,shape.vertices[static_cast<size_t>(k)]);
        }
    }
    if(world->identities) {
        hash_u64(&hash,world->identities->capacity());
        for(const auto& record:world->identities->records()) if(record.status!=ScIdentityStatus::absent) {
            hash_string(&hash,record.name); hash_u64(&hash,record.entity);
            hash_u32(&hash,static_cast<std::uint32_t>(record.status));
        }
    }
    const auto& particles=world->particles;
    for(std::size_t i=0;i<particles.capacity();++i) {
        const bool active=i<particles.count;
        hash_byte(&hash,active);
        if(!active) continue;
        hash_float(&hash,particles.x[i]); hash_float(&hash,particles.y[i]);
        hash_float(&hash,particles.vx[i]); hash_float(&hash,particles.vy[i]);
        hash_float(&hash,particles.life[i]); hash_float(&hash,particles.max_life[i]);
        hash_float(&hash,particles.size[i]); hash_u32(&hash,particles.color[i]);
        hash_byte(&hash,particles.emitter[i]);
    }
    hash_u32(&hash, world->held); hash_u32(&hash, world->pressed);
    for (const auto& key:SC_KEYS) {
        auto i=static_cast<std::size_t>(key.id);
        hash_u32(&hash,world->input.keys[i]); hash_u32(&hash,world->input.key_pressed[i]); hash_u32(&hash,world->input.key_released[i]);
    }
    hash_u32(&hash,world->input.connected); hash_u32(&hash,world->input.buttons);
    hash_u32(&hash,world->input.button_pressed); hash_u32(&hash,world->input.button_released);
    for(float axis:world->input.axes) hash_float(&hash,axis);
    const auto& input=world->input;
    for(float value:{input.mouse_x,input.mouse_y,input.mouse_dx,input.mouse_dy,input.wheel_x,input.wheel_y}) hash_float(&hash,value);
    hash_byte(&hash,input.mouse_inside);
    for(auto value:{input.mouse_buttons,input.mouse_pressed,input.mouse_released}) hash_u32(&hash,value);
    for(const auto& pad:input.pads) {
        hash_byte(&hash,pad.connected);
        for(auto value:{pad.buttons,pad.pressed,pad.released}) hash_u32(&hash,value);
        for(float axis:pad.axes) hash_float(&hash,axis);
    }
    hash_string(&hash,input.text); hash_string(&hash,input.composition); hash_string(&hash,input.clipboard);
    const auto edit=sc_composition_edit(input);
    hash_u32(&hash,static_cast<std::uint32_t>(edit.cursor));
    hash_u32(&hash,static_cast<std::uint32_t>(edit.start)); hash_u32(&hash,static_cast<std::uint32_t>(edit.finish));
    const auto segment_count=std::min(input.composition_segment_count,SC_COMPOSITION_SEGMENTS);
    hash_u32(&hash,static_cast<std::uint32_t>(segment_count));
    hash_u32(&hash,input.composition_segments_truncated);
    for(std::size_t i=0;i<segment_count;++i) {
        const auto& segment=input.composition_segments[i];
        hash_u32(&hash,segment.start); hash_u32(&hash,segment.finish); hash_u32(&hash,segment.kind);
    }
    hash_u32(&hash, world->released); hash_u32(&hash, world->rng);
    hash_u64(&hash, world->tick);
    hash_float(&hash, world->gravity); hash_float(&hash, world->camera_x);
    hash_float(&hash, world->camera_y); hash_float(&hash, world->ambient);
    hash_u64(&hash, world->camera_target);
    hash_u32(&hash, static_cast<std::uint32_t>(world->view_width));
    hash_u32(&hash, static_cast<std::uint32_t>(world->view_height));
    hash_string(&hash, world->title);
    hash_string(&hash, world->message);
    for(const auto& layer:world->layers) {
        hash_string(&hash,std::span(layer.name.data(),layer.name.size()));
        hash_float(&hash,layer.opacity); hash_float(&hash,layer.x); hash_float(&hash,layer.y); hash_byte(&hash,layer.visible);
        hash_u32(&hash,static_cast<uint32_t>(layer.order));
        for(auto cell:layer.cells) hash_u32(&hash,cell);
    }
    return hash;
}
