#include "shiny/core.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* The core owns no heap memory and advances only through explicit fixed ticks.
 * Collision is against the tile map; entity/entity overlap is a game decision. */
#define SC_VALUE_LIMIT 1000000.0f
#define SC_GENERATION_MASK UINT32_C(0x00ffffff)

static float clamp_float(double value, double minimum, double maximum) {
    if (value < minimum) return (float)minimum;
    if (value > maximum) return (float)maximum;
    return (float)value;
}

static bool map_valid(const ScMap *map) {
    return map->width > 0 && map->height > 0 && map->tile_size > 0 &&
           map->height <= SC_MAX_TILES &&
           map->width <= SC_MAX_TILES / map->height;
}

static bool entity_valid(const ScEntity *entity) {
    return entity && isfinite(entity->x) && isfinite(entity->y) &&
           isfinite(entity->vx) && isfinite(entity->vy) &&
           fabsf(entity->x) <= SC_VALUE_LIMIT && fabsf(entity->y) <= SC_VALUE_LIMIT &&
           fabsf(entity->vx) <= SC_VALUE_LIMIT && fabsf(entity->vy) <= SC_VALUE_LIMIT &&
           isfinite(entity->w) && isfinite(entity->h) &&
           entity->w > 0 && entity->w <= 4096 && entity->h > 0 && entity->h <= 4096 &&
           isfinite(entity->gravity) && fabsf(entity->gravity) <= 100 &&
           isfinite(entity->glow) && entity->glow >= 0 && entity->glow <= 1024;
}

void sc_world_init(ScWorld *world, uint32_t seed) {
    if (!world) return;
    memset(world, 0, sizeof(*world));
    world->rng = seed ? seed : UINT32_C(0x6d2b79f5);
    world->gravity = 600;
    world->ambient = 0.4f;
    world->view_width = 384;
    world->view_height = 216;
    world->map.width = 48;
    world->map.height = 27;
    world->map.tile_size = 8;
    world->map.color = UINT32_C(0x183244ff);
    world->map.accent = UINT32_C(0x28566fff);
    world->map.background = UINT32_C(0x070b19ff);
    memset(world->map.tiles, '.', sizeof(world->map.tiles));
    memcpy(world->title, "ShinyCore", sizeof("ShinyCore"));
    for (int i = 0; i < SC_MAX_ENTITIES; ++i) world->generations[i] = 1;
}

uint32_t sc_spawn(ScWorld *world, const ScEntity *entity) {
    if (!world || !entity_valid(entity)) return 0;
    for (int i = 0; i < SC_MAX_ENTITIES; ++i) {
        if (world->entities[i].alive) continue;
        /* Copy first: callers may pass an entity in this world's own pool. */
        ScEntity copy = *entity;
        uint32_t generation = world->generations[i] & SC_GENERATION_MASK;
        if (!generation) generation = 1;
        world->generations[i] = generation;
        copy.id = (generation << 8) | (uint32_t)i;
        copy.alive = true;
        copy.grounded = false;
        copy.tag[sizeof(copy.tag) - 1] = '\0';
        copy.sprite[sizeof(copy.sprite) - 1] = '\0';
        world->entities[i] = copy;
        return copy.id;
    }
    return 0;
}

ScEntity *sc_entity(ScWorld *world, uint32_t id) {
    if (!world || !id) return NULL;
    unsigned slot = id & UINT32_C(0xff);
    ScEntity *entity = &world->entities[slot];
    return entity->alive && entity->id == id && world->generations[slot] == (id >> 8)
               ? entity : NULL;
}

bool sc_destroy(ScWorld *world, uint32_t id) {
    ScEntity *entity = sc_entity(world, id);
    if (!entity) return false;
    unsigned slot = id & UINT32_C(0xff);
    memset(entity, 0, sizeof(*entity));
    uint32_t generation = (world->generations[slot] + 1) & SC_GENERATION_MASK;
    world->generations[slot] = generation ? generation : 1;
    if (world->camera_target == id) world->camera_target = 0;
    return true;
}

uint32_t sc_find(const ScWorld *world, const char *tag) {
    if (!world || !tag || !*tag) return 0;
    for (int i = 0; i < SC_MAX_ENTITIES; ++i) {
        const ScEntity *entity = &world->entities[i];
        if (entity->alive && strncmp(entity->tag, tag, sizeof(entity->tag)) == 0)
            return entity->id;
    }
    return 0;
}

void sc_input(ScWorld *world, uint32_t held) {
    if (!world) return;
    held &= SC_LEFT | SC_RIGHT | SC_UP | SC_DOWN | SC_JUMP | SC_ACTION;
    world->pressed = held & ~world->held;
    world->released = world->held & ~held;
    world->held = held;
}

char sc_tile(const ScWorld *world, int x, int y) {
    if (!world || !map_valid(&world->map) || x < 0 || y < 0 ||
        x >= world->map.width || y >= world->map.height) return '#';
    return world->map.tiles[y * world->map.width + x];
}

bool sc_overlap(const ScEntity *a, const ScEntity *b) {
    if (!a || !b || !a->alive || !b->alive || !entity_valid(a) || !entity_valid(b))
        return false;
    return (double)a->x < (double)b->x + b->w &&
           (double)a->x + a->w > b->x &&
           (double)a->y < (double)b->y + b->h &&
           (double)a->y + a->h > b->y;
}

/* Conversions to tile indices happen only after clipping to valid map extents.
 * Neither an extreme velocity nor a malformed public struct can produce an
 * out-of-range floating-to-integer cast or an unbounded collision loop. */
static bool cell_range(double start, double end, int tile_size, int count,
                       int *first, int *last) {
    double extent = (double)tile_size * count;
    if (end <= 0 || start >= extent || end <= start) return false;
    double low = floor(fmax(0, start) / tile_size);
    double high = ceil(fmin(extent, end) / tile_size) - 1;
    *first = low >= count ? count - 1 : (int)low;
    *last = high >= count ? count - 1 : (int)fmax(0, high);
    return *first <= *last;
}

static double contact_epsilon(double position) {
    return fmax(0.000001, fabs(position) * FLT_EPSILON);
}

static void move_horizontal(const ScMap *map, ScEntity *entity, double dx) {
    double extent = (double)map->width * map->tile_size;
    double maximum = fmax(0, extent - entity->w);
    double old_x = entity->x;
    double next_x = fmax(0, fmin(maximum, old_x + dx));
    bool blocked = next_x != old_x + dx;
    int first_row, last_row, first_col, last_col;
    double epsilon = contact_epsilon(old_x + entity->w);
    bool rows = cell_range(entity->y, (double)entity->y + entity->h,
                           map->tile_size, map->height, &first_row, &last_row);
    bool columns = dx > 0
        ? cell_range(old_x + entity->w - epsilon,
                     next_x + entity->w + epsilon, map->tile_size, map->width,
                     &first_col, &last_col)
        : cell_range(next_x - epsilon, old_x + epsilon, map->tile_size, map->width,
                     &first_col, &last_col);
    if (dx != 0 && rows && columns) {
        for (int row = first_row; row <= last_row; ++row) {
            for (int col = first_col; col <= last_col; ++col) {
                if (map->tiles[row * map->width + col] != '#') continue;
                double left = (double)col * map->tile_size;
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
    double extent = (double)map->height * map->tile_size;
    double maximum = fmax(0, extent - entity->h);
    double old_y = entity->y;
    double next_y = fmax(0, fmin(maximum, old_y + dy));
    bool blocked = next_y != old_y + dy;
    int first_col, last_col, first_row, last_row;
    double epsilon = contact_epsilon(old_y + entity->h);
    bool columns = cell_range(entity->x, (double)entity->x + entity->w,
                              map->tile_size, map->width, &first_col, &last_col);
    bool rows = dy > 0
        ? cell_range(old_y + entity->h - epsilon,
                     next_y + entity->h + epsilon, map->tile_size, map->height,
                     &first_row, &last_row)
        : cell_range(next_y - epsilon, old_y + epsilon, map->tile_size, map->height,
                     &first_row, &last_row);
    if (dy != 0 && columns && rows) {
        for (int row = first_row; row <= last_row; ++row) {
            for (int col = first_col; col <= last_col; ++col) {
                char tile = map->tiles[row * map->width + col];
                double top = (double)row * map->tile_size;
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
    double bottom = (double)entity->y + entity->h;
    double epsilon = contact_epsilon(bottom);
    double extent = (double)map->height * map->tile_size;
    if (bottom >= extent - epsilon) return true;
    int first_col, last_col, first_row, last_row;
    if (!cell_range(entity->x, (double)entity->x + entity->w, map->tile_size,
                    map->width, &first_col, &last_col) ||
        !cell_range(bottom - epsilon, bottom + epsilon, map->tile_size,
                    map->height, &first_row, &last_row)) return false;
    for (int row = first_row; row <= last_row; ++row) {
        if (fabs(bottom - (double)row * map->tile_size) > epsilon) continue;
        for (int col = first_col; col <= last_col; ++col) {
            char tile = map->tiles[row * map->width + col];
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
    double gravity = isfinite(world->gravity) ? world->gravity : 0;
    entity->vy = clamp_float((double)entity->vy + gravity * entity->gravity * SC_DT,
                             -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
    if (!entity->solid) {
        entity->x = clamp_float((double)entity->x + (double)entity->vx * SC_DT,
                                 -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        entity->y = clamp_float((double)entity->y + (double)entity->vy * SC_DT,
                                 -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        return;
    }
    if (!valid_map) {
        entity->vx = entity->vy = 0;
        return;
    }
    /* Recover explicit teleports outside the map before sweeping movement. */
    entity->x = clamp_float(entity->x, 0,
        fmax(0, (double)world->map.width * world->map.tile_size - entity->w));
    entity->y = clamp_float(entity->y, 0,
        fmax(0, (double)world->map.height * world->map.tile_size - entity->h));
    move_horizontal(&world->map, entity, (double)entity->vx * SC_DT);
    move_vertical(&world->map, entity, (double)entity->vy * SC_DT);
    entity->grounded = entity->grounded || supported(&world->map, entity);
}

uint32_t sc_random_u32(ScWorld *world) {
    if (!world) return 0;
    uint32_t value = world->rng ? world->rng : UINT32_C(0x6d2b79f5);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    world->rng = value;
    return value;
}

float sc_random(ScWorld *world) {
    return (float)(sc_random_u32(world) >> 8) * (1.0f / 16777216.0f);
}

void sc_emit(ScWorld *world, float x, float y, int count, uint32_t color,
             float speed, float life) {
    if (!world || count <= 0 || !isfinite(x) || !isfinite(y) ||
        fabsf(x) > SC_VALUE_LIMIT || fabsf(y) > SC_VALUE_LIMIT ||
        !isfinite(speed) || speed < 0 || speed > SC_VALUE_LIMIT ||
        !isfinite(life) || life <= 0 || life > SC_VALUE_LIMIT) return;
    for (int i = 0; i < SC_MAX_PARTICLES && count > 0; ++i) {
        ScParticle *particle = &world->particles[i];
        if (particle->life > 0) continue;
        float angle = sc_random(world) * 6.2831853071795864769f;
        float velocity = speed * (0.35f + 0.65f * sc_random(world));
        float duration = life * (0.6f + 0.4f * sc_random(world));
        *particle = (ScParticle){
            .x = x, .y = y,
            .vx = cosf(angle) * velocity, .vy = sinf(angle) * velocity,
            .life = duration, .max_life = duration,
            .size = 1 + floorf(sc_random(world) * 3), .color = color
        };
        --count;
    }
}

static void step_particles(ScWorld *world) {
    double gravity = isfinite(world->gravity) ? world->gravity : 0;
    for (int i = 0; i < SC_MAX_PARTICLES; ++i) {
        ScParticle *particle = &world->particles[i];
        if (!(particle->life > 0)) continue;
        if (!isfinite(particle->life) || !isfinite(particle->x) ||
            !isfinite(particle->y) || !isfinite(particle->vx) ||
            !isfinite(particle->vy)) {
            particle->life = 0;
            continue;
        }
        particle->life = fmaxf(0, particle->life - SC_DT);
        if (particle->life == 0) continue;
        particle->vy = clamp_float((double)particle->vy + gravity * 0.15 * SC_DT,
                                   -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        particle->x = clamp_float((double)particle->x + (double)particle->vx * SC_DT,
                                   -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
        particle->y = clamp_float((double)particle->y + (double)particle->vy * SC_DT,
                                   -SC_VALUE_LIMIT, SC_VALUE_LIMIT);
    }
}

static void step_camera(ScWorld *world, bool valid_map) {
    double maximum_x = valid_map
        ? fmax(0, (double)world->map.width * world->map.tile_size -
                   fmax(0, world->view_width)) : 0;
    double maximum_y = valid_map
        ? fmax(0, (double)world->map.height * world->map.tile_size -
                   fmax(0, world->view_height)) : 0;
    if (!isfinite(world->camera_x)) world->camera_x = 0;
    if (!isfinite(world->camera_y)) world->camera_y = 0;
    ScEntity *target = sc_entity(world, world->camera_target);
    if (target && entity_valid(target)) {
        double desired_x = fmax(0, fmin(maximum_x,
            (double)target->x + target->w * 0.5 - fmax(0, world->view_width) * 0.5));
        double desired_y = fmax(0, fmin(maximum_y,
            (double)target->y + target->h * 0.5 - fmax(0, world->view_height) * 0.5));
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
    for (int i = 0; i < SC_MAX_ENTITIES; ++i)
        step_entity(world, &world->entities[i], valid_map);
    step_particles(world);
    step_camera(world, valid_map);
    ++world->tick;
}

/* Explicit little-endian FNV-1a serialization hashes simulation fields, never C
 * structure padding, unused pool bytes, addresses, or transient draw/tone queues.
 * Equal signed zeros hash equally, as do NaNs inserted through the native API. */
static void hash_byte(uint64_t *hash, uint8_t value) {
    *hash ^= value;
    *hash *= UINT64_C(1099511628211);
}

static void hash_u32(uint64_t *hash, uint32_t value) {
    for (int i = 0; i < 4; ++i) hash_byte(hash, (uint8_t)(value >> (i * 8)));
}

static void hash_u64(uint64_t *hash, uint64_t value) {
    for (int i = 0; i < 8; ++i) hash_byte(hash, (uint8_t)(value >> (i * 8)));
}

static void hash_float(uint64_t *hash, float value) {
    uint32_t bits;
    if (value == 0) bits = 0;
    else if (isnan(value)) bits = UINT32_C(0x7fc00000);
    else memcpy(&bits, &value, sizeof(bits));
    hash_u32(hash, bits);
}

static void hash_string(uint64_t *hash, const char *value, size_t capacity) {
    size_t length = 0;
    while (length < capacity && value[length]) ++length;
    hash_u32(hash, (uint32_t)length);
    for (size_t i = 0; i < length; ++i) hash_byte(hash, (uint8_t)value[i]);
}

uint64_t sc_state_hash(const ScWorld *world) {
    if (!world) return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    const ScMap *map = &world->map;
    hash_u32(&hash, (uint32_t)map->width);
    hash_u32(&hash, (uint32_t)map->height);
    hash_u32(&hash, (uint32_t)map->tile_size);
    hash_u32(&hash, map->color);
    hash_u32(&hash, map->accent);
    hash_u32(&hash, map->background);
    if (map_valid(map)) {
        for (int i = 0; i < map->width * map->height; ++i)
            hash_byte(&hash, (uint8_t)map->tiles[i]);
    }
    for (int i = 0; i < SC_MAX_ENTITIES; ++i) {
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
        hash_string(&hash, entity->tag, sizeof(entity->tag));
        hash_string(&hash, entity->sprite, sizeof(entity->sprite));
        hash_u32(&hash, (uint32_t)entity->frame);
        hash_u32(&hash, (uint32_t)entity->frame_w);
        hash_u32(&hash, (uint32_t)entity->frame_h);
        hash_u32(&hash, (uint32_t)entity->layer);
    }
    for (int i = 0; i < SC_MAX_PARTICLES; ++i) {
        const ScParticle *particle = &world->particles[i];
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
    hash_u32(&hash, (uint32_t)world->view_width);
    hash_u32(&hash, (uint32_t)world->view_height);
    hash_string(&hash, world->title, sizeof(world->title));
    hash_string(&hash, world->message, sizeof(world->message));
    return hash;
}
