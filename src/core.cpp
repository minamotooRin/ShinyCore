#include "shiny/core.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <span>

/* The core owns no heap memory and advances only through explicit fixed ticks.
 * Collision is against the tile map; entity/entity overlap is a game decision. */
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
    if (!world || !entity_valid(entity)) return 0;
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
    held &= SC_LEFT | SC_RIGHT | SC_UP | SC_DOWN | SC_JUMP | SC_ACTION;
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

/* Conversions to tile indices happen only after clipping to valid map extents.
 * Neither an extreme velocity nor a malformed public struct can produce an
 * out-of-range floating-to-integer cast or an unbounded collision loop. */
struct CellRange { int first{}, last{}; };

static std::optional<CellRange> cell_range(double start, double end, int tile_size, int count) {
    double extent = static_cast<double>(tile_size) * count;
    if (end <= 0 || start >= extent || end <= start) return std::nullopt;
    double low = std::floor(std::fmax(0, start) / tile_size);
    double high = std::ceil(std::fmin(extent, end) / tile_size) - 1;
    const int first = low >= count ? count - 1 : static_cast<int>(low);
    const int last = high >= count ? count - 1 : static_cast<int>(std::fmax(0, high));
    if (first > last) return std::nullopt;
    return CellRange{first, last};
}

static double contact_epsilon(double position) {
    return std::fmax(0.000001, std::fabs(position) * std::numeric_limits<float>::epsilon());
}

static void move_horizontal(const ScMap *map, ScEntity *entity, double dx) {
    double extent = static_cast<double>(map->width) * map->tile_size;
    double maximum = std::fmax(0, extent - entity->w);
    double old_x = entity->x;
    double next_x = std::fmax(0, std::fmin(maximum, old_x + dx));
    bool blocked = next_x != old_x + dx;
    double epsilon = contact_epsilon(old_x + entity->w);
    const auto rows = cell_range(entity->y, static_cast<double>(entity->y) + entity->h,
                                map->tile_size, map->height);
    const auto columns = dx > 0
        ? cell_range(old_x + entity->w - epsilon,
                     next_x + entity->w + epsilon, map->tile_size, map->width)
        : cell_range(next_x - epsilon, old_x + epsilon, map->tile_size, map->width);
    if (dx != 0 && rows && columns) {
        for (int row = rows->first; row <= rows->last; ++row) {
            for (int col = columns->first; col <= columns->last; ++col) {
                if (map->tiles[static_cast<std::size_t>(row * map->width + col)] != '#') continue;
                double left = static_cast<double>(col) * map->tile_size;
                double right = left + map->tile_size;
                if (dx > 0 && left >= old_x + entity->w - epsilon &&
                    left <= next_x + entity->w) {
                    next_x = left - entity->w;
                    blocked = true;
                } else if (dx < 0 && right <= old_x + epsilon && right >= next_x) {
                    next_x = right;
                    blocked = true;
                }
            }
        }
    }
    entity->x = clamp_float(next_x, 0, maximum);
    if (blocked) entity->vx = 0;
}

static void move_vertical(const ScMap *map, ScEntity *entity, double dy) {
    double extent = static_cast<double>(map->height) * map->tile_size;
    double maximum = std::fmax(0, extent - entity->h);
    double old_y = entity->y;
    double next_y = std::fmax(0, std::fmin(maximum, old_y + dy));
    bool blocked = next_y != old_y + dy;
    double epsilon = contact_epsilon(old_y + entity->h);
    const auto columns = cell_range(entity->x, static_cast<double>(entity->x) + entity->w,
                                   map->tile_size, map->width);
    const auto rows = dy > 0
        ? cell_range(old_y + entity->h - epsilon,
                     next_y + entity->h + epsilon, map->tile_size, map->height)
        : cell_range(next_y - epsilon, old_y + epsilon, map->tile_size, map->height);
    if (dy != 0 && columns && rows) {
        for (int row = rows->first; row <= rows->last; ++row) {
            for (int col = columns->first; col <= columns->last; ++col) {
                char tile = map->tiles[static_cast<std::size_t>(row * map->width + col)];
                double top = static_cast<double>(row) * map->tile_size;
                double bottom = top + map->tile_size;
                if ((tile == '#' || tile == '=') && dy > 0 &&
                    top >= old_y + entity->h - epsilon && top <= next_y + entity->h) {
                    next_y = top - entity->h;
                    blocked = true;
                } else if (tile == '#' && dy < 0 &&
                           bottom <= old_y + epsilon && bottom >= next_y) {
                    next_y = bottom;
                    blocked = true;
                }
            }
        }
    }
    entity->y = clamp_float(next_y, 0, maximum);
    if (blocked) {
        if (dy > 0) entity->grounded = true;
        entity->vy = 0;
    }
}

static bool supported(const ScMap *map, const ScEntity *entity) {
    if (entity->vy < 0) return false;
    double bottom = static_cast<double>(entity->y) + entity->h;
    double epsilon = contact_epsilon(bottom);
    double extent = static_cast<double>(map->height) * map->tile_size;
    if (bottom >= extent - epsilon) return true;
    const auto columns = cell_range(entity->x, static_cast<double>(entity->x) + entity->w,
                                    map->tile_size, map->width);
    const auto rows = cell_range(bottom - epsilon, bottom + epsilon,
                                 map->tile_size, map->height);
    if (!columns || !rows) return false;
    for (int row = rows->first; row <= rows->last; ++row) {
        if (std::fabs(bottom - static_cast<double>(row) * map->tile_size) > epsilon) continue;
        for (int col = columns->first; col <= columns->last; ++col) {
            char tile = map->tiles[static_cast<std::size_t>(row * map->width + col)];
            if (tile == '#' || tile == '=') return true;
        }
    }
    return false;
}

static void step_entity(ScWorld *world, ScEntity *entity, bool valid_map) {
    if (!entity->alive) return;
    entity->grounded = false;
    if (!entity->dynamic) return;
    if (!entity_valid(entity)) {
        entity->vx = entity->vy = 0;
        return;
    }
    double gravity = std::isfinite(world->gravity) ? world->gravity : 0;
    entity->vy = clamp_float(static_cast<double>(entity->vy) + gravity * entity->gravity * SC_DT,
                             -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
    if (!entity->solid) {
        entity->x = clamp_float(static_cast<double>(entity->x) + static_cast<double>(entity->vx) * SC_DT,
                                 -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        entity->y = clamp_float(static_cast<double>(entity->y) + static_cast<double>(entity->vy) * SC_DT,
                                 -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        return;
    }
    if (!valid_map) {
        entity->vx = entity->vy = 0;
        return;
    }
    /* Recover explicit teleports outside the map before sweeping movement. */
    entity->x = clamp_float(entity->x, 0,
        std::fmax(0, static_cast<double>(world->map.width) * world->map.tile_size - entity->w));
    entity->y = clamp_float(entity->y, 0,
        std::fmax(0, static_cast<double>(world->map.height) * world->map.tile_size - entity->h));
    move_horizontal(&world->map, entity, static_cast<double>(entity->vx) * SC_DT);
    move_vertical(&world->map, entity, static_cast<double>(entity->vy) * SC_DT);
    entity->grounded = entity->grounded || supported(&world->map, entity);
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
    for (auto &entity : world->entities)
        step_entity(world, &entity, valid_map);
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
    hash_u32(&hash, world->released); hash_u32(&hash, world->rng);
    hash_u64(&hash, world->tick);
    hash_float(&hash, world->gravity); hash_float(&hash, world->camera_x);
    hash_float(&hash, world->camera_y); hash_float(&hash, world->ambient);
    hash_u32(&hash, world->camera_target);
    hash_u32(&hash, static_cast<std::uint32_t>(world->view_width));
    hash_u32(&hash, static_cast<std::uint32_t>(world->view_height));
    hash_string(&hash, world->title);
    hash_string(&hash, world->message);
    return hash;
}
