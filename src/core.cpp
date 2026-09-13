#include "shiny/core.h"
#include "shiny/physics.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <span>

/* Fixed-capacity entities own a single Box2D world; only fixed ticks advance it. */
static constexpr float SC_VALUE_LIMIT = 1000000.0f;
static constexpr std::uint32_t SC_GENERATION_MASK = 0x00ffffffu;
static_assert(SC_MAX_ENTITIES == 256, "entity handles reserve eight bits for the pool slot");
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

std::uint32_t sc_spawn(ScWorld *world, const ScEntity *entity) {
    if (!world || !entity_valid(entity) || !sc_physics_body_valid(*entity)) return 0;
    for (std::size_t i = 0; i < world->entities.size(); ++i) {
        if (world->entities[i].alive) continue;
        /* Copy first: callers may pass an entity in this world's own pool. */
        ScEntity copy = *entity;
        std::uint32_t generation = world->generations[i] & SC_GENERATION_MASK;
        if (!generation) generation = 1;
        world->generations[i] = generation;
        copy.id = (generation << 8) | static_cast<std::uint32_t>(i);
        copy.alive = true;
        copy.grounded = false;
        copy.tag[sizeof(copy.tag) - 1] = '\0';
        copy.sprite[sizeof(copy.sprite) - 1] = '\0';
        world->entities[i] = copy;
        return copy.id;
    }
    return 0;
}

ScEntity *sc_entity(ScWorld *world, std::uint32_t id) {
    if (!world || !id) return nullptr;
    unsigned slot = id & UINT32_C(0xff);
    ScEntity *entity = &world->entities[slot];
    return entity->alive && entity->id == id && world->generations[slot] == (id >> 8)
               ? entity : nullptr;
}

bool sc_destroy(ScWorld *world, std::uint32_t id) {
    ScEntity *entity = sc_entity(world, id);
    if (!entity) return false;
    unsigned slot = id & UINT32_C(0xff);
    *entity = ScEntity{};
    std::uint32_t generation = (world->generations[slot] + 1) & SC_GENERATION_MASK;
    world->generations[slot] = generation ? generation : 1;
    if (world->camera_target == id) world->camera_target = 0;
    return true;
}

std::uint32_t sc_find(const ScWorld *world, const char *tag) {
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
    if (!world || !map_valid(&world->map) || x < 0 || y < 0 ||
        x >= world->map.width || y >= world->map.height) return '#';
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

void sc_emit(ScWorld *world, float x, float y, int count, std::uint32_t color,
             float speed, float life) {
    if (!world || count <= 0 || !std::isfinite(x) || !std::isfinite(y) ||
        std::fabs(x) > SC_VALUE_LIMIT || std::fabs(y) > SC_VALUE_LIMIT ||
        !std::isfinite(speed) || speed < 0 || speed > SC_VALUE_LIMIT ||
        !std::isfinite(life) || life <= 0 || life > SC_VALUE_LIMIT) return;
    for (auto &slot : world->particles) {
        if (count == 0) break;
        ScParticle *particle = &slot;
        if (particle->life > 0) continue;
        float angle = sc_random(world) * (2.0f * std::numbers::pi_v<float>);
        float velocity = speed * (0.35f + 0.65f * sc_random(world));
        float duration = life * (0.6f + 0.4f * sc_random(world));
        *particle = ScParticle{
            .x = x, .y = y,
            .vx = std::cos(angle) * velocity, .vy = std::sin(angle) * velocity,
            .life = duration, .max_life = duration,
            .size = 1 + std::floor(sc_random(world) * 3), .color = color
        };
        --count;
    }
}

static void step_particles(ScWorld *world) {
    double gravity = std::isfinite(world->gravity) ? world->gravity : 0;
    for (auto &slot : world->particles) {
        ScParticle *particle = &slot;
        if (!(particle->life > 0)) continue;
        if (!std::isfinite(particle->life) || !std::isfinite(particle->x) ||
            !std::isfinite(particle->y) || !std::isfinite(particle->vx) ||
            !std::isfinite(particle->vy)) {
            particle->life = 0;
            continue;
        }
        particle->life = std::fmax(0.0f, particle->life - SC_DT);
        if (particle->life == 0) continue;
        particle->vy = clamp_float(static_cast<double>(particle->vy) + gravity * 0.15 * SC_DT,
                                   -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        particle->x = clamp_float(static_cast<double>(particle->x) + static_cast<double>(particle->vx) * SC_DT,
                                   -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        particle->y = clamp_float(static_cast<double>(particle->y) + static_cast<double>(particle->vy) * SC_DT,
                                   -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
    }
}

static void step_camera(ScWorld *world, bool valid_map) {
    double maximum_x = valid_map
        ? std::fmax(0, static_cast<double>(world->map.width) * world->map.tile_size -
                   std::fmax(0, world->view_width)) : 0;
    double maximum_y = valid_map
        ? std::fmax(0, static_cast<double>(world->map.height) * world->map.tile_size -
                   std::fmax(0, world->view_height)) : 0;
    if (!std::isfinite(world->camera_x)) world->camera_x = 0;
    if (!std::isfinite(world->camera_y)) world->camera_y = 0;
    ScEntity *target = sc_entity(world, world->camera_target);
    if (target && entity_valid(target)) {
        double desired_x = std::fmax(0, std::fmin(maximum_x,
            static_cast<double>(target->x) + target->w * 0.5 - std::fmax(0, world->view_width) * 0.5));
        double desired_y = std::fmax(0, std::fmin(maximum_y,
            static_cast<double>(target->y) + target->h * 0.5 - std::fmax(0, world->view_height) * 0.5));
        world->camera_x = clamp_float(world->camera_x +
                                       (desired_x - world->camera_x) * 0.16,
                                       0, maximum_x);
        world->camera_y = clamp_float(world->camera_y +
                                       (desired_y - world->camera_y) * 0.16,
                                       0, maximum_y);
    } else {
        world->camera_x = clamp_float(world->camera_x, 0, maximum_x);
        world->camera_y = clamp_float(world->camera_y, 0, maximum_y);
        if (world->camera_target) world->camera_target = 0;
    }
}

void sc_step(ScWorld *world) {
    if (!world) return;
    bool valid_map = map_valid(&world->map);
    sc_physics_step(world);
    for(auto& voice:world->audio) if(voice.alive&&!voice.paused) {
        if(voice.fade>0) {
            float part=std::min(1.0f,SC_DT/voice.fade);
            voice.volume+=(voice.target_volume-voice.volume)*part;
            voice.fade=std::max(0.0f,voice.fade-SC_DT);
            if(voice.stopping&&voice.fade==0) voice.alive=false;
        }
        voice.position+=SC_DT*voice.pitch;
        if(voice.duration>0&&voice.position>=voice.duration) {
            if(voice.loop) voice.position=std::fmod(voice.position,voice.duration); else voice.alive=false;
        }
    }
    step_particles(world);
    step_camera(world, valid_map);
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
        hash_u32(&hash, entity->id);
        hash_byte(&hash, entity->dynamic);
        hash_byte(&hash, entity->solid);
        hash_byte(&hash, entity->grounded);
        hash_float(&hash, entity->x); hash_float(&hash, entity->y);
        hash_float(&hash, entity->w); hash_float(&hash, entity->h);
        hash_float(&hash, entity->vx); hash_float(&hash, entity->vy);
        hash_float(&hash, entity->gravity); hash_float(&hash, entity->glow);
        hash_u32(&hash, entity->color);
        hash_string(&hash, entity->tag);
        hash_string(&hash, entity->sprite);
        hash_u32(&hash, static_cast<std::uint32_t>(entity->frame));
        hash_u32(&hash, static_cast<std::uint32_t>(entity->frame_w));
        hash_u32(&hash, static_cast<std::uint32_t>(entity->frame_h));
        hash_u32(&hash, static_cast<std::uint32_t>(entity->layer));
        hash_u32(&hash,static_cast<uint32_t>(entity->body_type)); hash_u32(&hash,static_cast<uint32_t>(entity->shape));
        hash_float(&hash,entity->angle); hash_float(&hash,entity->angular_velocity);
        hash_float(&hash,entity->density); hash_float(&hash,entity->friction); hash_float(&hash,entity->restitution);
        hash_float(&hash,entity->drop_time); hash_float(&hash,entity->normal_x); hash_float(&hash,entity->normal_y);
        hash_u32(&hash,entity->category); hash_u32(&hash,entity->mask); hash_u32(&hash,entity->support);
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
    for (const auto &slot : world->particles) {
        const ScParticle *particle = &slot;
        bool active = particle->life > 0;
        hash_byte(&hash, active);
        if (!active) continue;
        hash_float(&hash, particle->x); hash_float(&hash, particle->y);
        hash_float(&hash, particle->vx); hash_float(&hash, particle->vy);
        hash_float(&hash, particle->life); hash_float(&hash, particle->max_life);
        hash_float(&hash, particle->size); hash_u32(&hash, particle->color);
    }
    hash_u32(&hash, world->held); hash_u32(&hash, world->pressed);
    for (const auto& key:SC_KEYS) {
        auto i=static_cast<std::size_t>(key.id);
        hash_u32(&hash,world->input.keys[i]); hash_u32(&hash,world->input.key_pressed[i]); hash_u32(&hash,world->input.key_released[i]);
    }
    hash_u32(&hash,world->input.connected); hash_u32(&hash,world->input.buttons);
    hash_u32(&hash,world->input.button_pressed); hash_u32(&hash,world->input.button_released);
    for(float axis:world->input.axes) hash_float(&hash,axis);
    hash_u32(&hash, world->released); hash_u32(&hash, world->rng);
    hash_u64(&hash, world->tick);
    hash_float(&hash, world->gravity); hash_float(&hash, world->camera_x);
    hash_float(&hash, world->camera_y); hash_float(&hash, world->ambient);
    hash_u32(&hash, world->camera_target);
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
