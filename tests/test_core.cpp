#include "shiny/core.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>

/* Deliberately independent of assert(): Release builds must execute checks. */
static int checks;
static int failures;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
        return; \
    } \
} while (0)

#define NEAR(actual, expected) CHECK(std::fabs(static_cast<double>(actual) - (expected)) < 0.0001)

static void fixture(ScWorld *world) {
    sc_world_init(world, 12345);
    world->map.width = 16;
    world->map.height = 12;
    world->map.tile_size = 8;
    world->view_width = 32;
    world->view_height = 24;
}

static ScEntity body(float x, float y) {
    ScEntity entity{};
    entity.x = x;
    entity.y = y;
    entity.w = entity.h = 6;
    entity.gravity = 1;
    entity.dynamic = entity.solid = true;
    entity.color = UINT32_C(0xffffffff);
    return entity;
}

static void row(ScWorld *world, int y, char tile) {
    for (int x = 0; x < world->map.width; ++x)
        world->map.tiles[static_cast<std::size_t>(y * world->map.width + x)] = tile;
}

static void column(ScWorld *world, int x, char tile) {
    for (int y = 0; y < world->map.height; ++y)
        world->map.tiles[static_cast<std::size_t>(y * world->map.width + x)] = tile;
}

static void test_cpp_value_initialization(void) {
    static_assert(std::is_aggregate_v<ScWorld>);
    static_assert(std::is_trivially_copyable_v<ScWorld>);
    static_assert(std::is_same_v<decltype(ScWorld::entities), std::array<ScEntity, SC_MAX_ENTITIES>>);
    // Bare default initialization must be safe, including every bounded pool.
    ScWorld world;
    CHECK(world.rng == SC_DEFAULT_SEED);
    CHECK(world.map.width == 48 && world.map.height == 27 && world.map.tile_size == 8);
    CHECK(world.gravity == 600 && world.ambient == 0.4f);
    CHECK(world.view_width == 384 && world.view_height == 216);
    CHECK(world.tick == 0 && world.draw_count == 0 && world.tone_count == 0);
    CHECK(std::ranges::all_of(world.map.tiles, [](char tile) { return tile == '.'; }));
    CHECK(std::ranges::all_of(world.generations, [](std::uint32_t generation) { return generation == 1; }));
    CHECK(std::ranges::all_of(world.entities, [](const ScEntity &entity) {
        return !entity.alive && entity.id == 0 && entity.x == 0 && entity.w == 0;
    }));
    CHECK(std::ranges::all_of(world.particles, [](const ScParticle &particle) { return particle.life == 0; }));
    // These platform-independent initial-state hashes were captured from C11.
    CHECK(sc_state_hash(&world) == UINT64_C(0x60e0f9bf1d922288));
    world.map.tiles[1] = '#';
    world.held = SC_JUMP;
    world.tick = 42;
    sc_world_init(&world, 0);
    CHECK(sc_state_hash(&world) == UINT64_C(0x60e0f9bf1d922288));
    sc_world_init(&world, 42);
    CHECK(sc_state_hash(&world) == UINT64_C(0x5dc417192fb381e0));
    ScWorld copy = world;
    copy.map.tiles[1] = '#';
    CHECK(world.map.tiles[1] == '.' && copy.map.tiles[1] == '#');
}

static void test_initialization_and_input(void) {
    ScWorld world{};
    sc_world_init(&world, 0);
    CHECK(world.rng != 0);
    CHECK(world.view_width == 384 && world.view_height == 216);
    CHECK(world.map.width * world.map.height <= SC_MAX_TILES);
    CHECK(world.tick == 0 && world.camera_target == 0);
    CHECK(sc_tile(&world, 1, 1) == '.');
    CHECK(sc_tile(&world, -1, 0) == '#');
    CHECK(sc_tile(&world, INT_MAX, INT_MIN) == '#');
    sc_input(&world, SC_LEFT | SC_JUMP);
    CHECK(world.held == (SC_LEFT | SC_JUMP));
    CHECK(world.pressed == (SC_LEFT | SC_JUMP) && world.released == 0);
    sc_input(&world, SC_LEFT | SC_JUMP);
    CHECK(world.pressed == 0 && world.released == 0);
    sc_input(&world, SC_RIGHT);
    CHECK(world.pressed == SC_RIGHT && world.released == (SC_LEFT | SC_JUMP));
    sc_input(&world, UINT32_C(0x80000000));
    CHECK(world.held == 0 && world.released == SC_RIGHT);
    sc_step(&world);
    CHECK(world.tick == 1);
}

static void test_handles_and_capacity(void) {
    ScWorld world{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    std::strcpy(prototype.tag, "player");
    std::uint32_t first = sc_spawn(&world, &prototype);
    CHECK(first != 0 && sc_entity(&world, first) != nullptr);
    CHECK(sc_find(&world, "player") == first);
    CHECK(sc_find(&world, "missing") == 0 && sc_find(&world, "") == 0);
    world.camera_target = first;
    CHECK(sc_destroy(&world, first));
    CHECK(world.camera_target == 0);
    CHECK(sc_entity(&world, first) == nullptr && !sc_destroy(&world, first));
    std::uint32_t second = sc_spawn(&world, &prototype);
    CHECK(second != first && second != 0);
    CHECK(sc_entity(&world, first) == nullptr && sc_entity(&world, second) != nullptr);
    std::array<std::uint32_t, SC_MAX_ENTITIES> ids{};
    ids[0] = second;
    for (std::size_t i = 1; i < ids.size(); ++i) {
        ids[i] = sc_spawn(&world, &prototype);
        CHECK(ids[i] != 0 && sc_entity(&world, ids[i]) != nullptr);
        for (std::size_t j = 0; j < i; ++j) CHECK(ids[i] != ids[j]);
    }
    CHECK(sc_spawn(&world, &prototype) == 0);
    CHECK(sc_entity(&world, 0) == nullptr && sc_entity(&world, 255) == nullptr);
    CHECK(sc_entity(&world, UINT32_MAX) == nullptr);
    CHECK(sc_destroy(&world, ids[123]));
    std::uint32_t recycled = sc_spawn(&world, &prototype);
    CHECK(recycled != 0 && recycled != ids[123]);
    CHECK(sc_entity(&world, ids[123]) == nullptr);
    CHECK(sc_spawn(&world, &prototype) == 0);
}

static void test_spawn_validation(void) {
    ScWorld world{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    prototype.x = NAN;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.vx = INFINITY;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.y = FLT_MAX;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.w = 0;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.h = -1;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.h = 4097;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.gravity = NAN;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.glow = -1;
    CHECK(sc_spawn(&world, &prototype) == 0);
    prototype = body(8, 8); prototype.glow = INFINITY;
    CHECK(sc_spawn(&world, &prototype) == 0);
    CHECK(sc_spawn(&world, nullptr) == 0);
    prototype = body(8, 8);
    std::ranges::fill(prototype.tag, 'a');
    std::ranges::fill(prototype.sprite, 's');
    std::uint32_t id = sc_spawn(&world, &prototype);
    CHECK(id != 0);
    ScEntity *entity = sc_entity(&world, id);
    CHECK(entity->tag[sizeof(entity->tag) - 1] == '\0');
    CHECK(entity->sprite[sizeof(entity->sprite) - 1] == '\0');
}

static void test_fixed_step_and_flags(void) {
    ScWorld world{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    prototype.vx = 60;
    ScEntity *entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    sc_step(&world);
    NEAR(entity->x, 9);
    NEAR(entity->vy, 10);
    NEAR(entity->y, 8 + 10.0 / 60);
    entity->dynamic = false;
    float old_x = entity->x, old_y = entity->y;
    sc_step(&world);
    NEAR(entity->x, old_x); NEAR(entity->y, old_y);
    entity->dynamic = true;
    entity->solid = false;
    entity->gravity = 0;
    entity->vy = 6000;
    row(&world, 8, '#');
    sc_step(&world);
    CHECK(entity->y > 96 && !entity->grounded);
    CHECK(world.tick == 3);
}

static void test_floor_and_grounded(void) {
    ScWorld world{};
    fixture(&world);
    row(&world, 8, '#');
    ScEntity prototype = body(8, 8);
    ScEntity *entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    for (int i = 0; i < 180; ++i) sc_step(&world);
    NEAR(entity->y, 58);
    CHECK(entity->grounded && entity->vy == 0);
    for (int i = 0; i < 120; ++i) {
        sc_step(&world);
        CHECK(entity->grounded && entity->y == 58 && entity->vy == 0);
    }
    entity->gravity = 0;
    sc_step(&world);
    CHECK(entity->grounded);
    entity->vy = -180;
    sc_step(&world);
    CHECK(entity->y < 58 && !entity->grounded);
}

static void test_swept_walls_and_ceiling(void) {
    ScWorld world{};
    fixture(&world);
    column(&world, 10, '#');
    ScEntity prototype = body(8, 8);
    prototype.gravity = 0;
    prototype.vx = 1000000;
    ScEntity *entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    sc_step(&world);
    NEAR(entity->x, 74); CHECK(entity->vx == 0);
    entity->x = 110; entity->vx = -1000000;
    sc_step(&world);
    NEAR(entity->x, 88); CHECK(entity->vx == 0);
    fixture(&world);
    row(&world, 3, '#');
    prototype = body(8, 60); prototype.gravity = 0; prototype.vy = -1000000;
    entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    sc_step(&world);
    NEAR(entity->y, 32); CHECK(entity->vy == 0 && !entity->grounded);
    fixture(&world);
    row(&world, 8, '#');
    prototype = body(8, 8); prototype.vy = 1000000;
    entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    sc_step(&world);
    NEAR(entity->y, 58); CHECK(entity->vy == 0 && entity->grounded);
}

static void test_map_perimeter(void) {
    ScWorld world{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    prototype.gravity = 0; prototype.vx = -1000000; prototype.vy = -1000000;
    ScEntity *entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    sc_step(&world);
    NEAR(entity->x, 0); NEAR(entity->y, 0);
    CHECK(entity->vx == 0 && entity->vy == 0 && !entity->grounded);
    entity->vx = 1000000; entity->vy = 1000000;
    sc_step(&world);
    NEAR(entity->x, 122); NEAR(entity->y, 90);
    CHECK(entity->vx == 0 && entity->vy == 0 && entity->grounded);
    entity->x = -1000000; entity->y = 1000000;
    sc_step(&world);
    NEAR(entity->x, 0); NEAR(entity->y, 90);
    entity->w = entity->h = 200;
    sc_step(&world);
    NEAR(entity->x, 0); NEAR(entity->y, 0);
}

static void test_one_way_platforms(void) {
    ScWorld world{};
    fixture(&world);
    row(&world, 6, '=');
    ScEntity prototype = body(8, 4);
    prototype.gravity = 0; prototype.vy = 1000000;
    ScEntity *entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    sc_step(&world);
    NEAR(entity->y, 42); CHECK(entity->grounded && entity->vy == 0);
    sc_step(&world); CHECK(entity->grounded);
    entity->y = 60; entity->vy = -1200;
    sc_step(&world);
    NEAR(entity->y, 40); CHECK(entity->vy == -1200 && !entity->grounded);
    entity->vy = 1200;
    sc_step(&world);
    NEAR(entity->y, 42); CHECK(entity->grounded);
    entity->y = 46; entity->vy = 120;
    sc_step(&world);
    NEAR(entity->y, 48); CHECK(!entity->grounded && entity->vy == 120);
    entity->vy = 0; entity->vx = 1200;
    sc_step(&world);
    NEAR(entity->x, 28); CHECK(entity->vx == 1200);
    entity->x = 8; entity->y = 42; entity->vy = -180; entity->vx = 0;
    sc_step(&world);
    NEAR(entity->y, 39); CHECK(!entity->grounded);
}

static void test_contact_precision_and_ledge(void) {
    ScWorld world{};
    fixture(&world);
    for (int x = 4; x < 8; ++x) world.map.tiles[static_cast<std::size_t>(6 * world.map.width + x)] = '=';
    ScEntity prototype = body(60, 42.7f);
    prototype.h = 5.3f; prototype.gravity = 0;
    ScEntity *entity = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(entity != nullptr);
    sc_step(&world);
    CHECK(entity->grounded);
    entity->gravity = 1;
    for (int i = 0; i < 120; ++i) {
        sc_step(&world);
        CHECK(entity->grounded);
        NEAR(static_cast<double>(entity->y) + entity->h, 48);
    }
    entity->gravity = 0;
    entity->vx = 240;
    sc_step(&world);
    NEAR(entity->x, 64);
    CHECK(!entity->grounded);
}

static void test_overlap_and_independent_bodies(void) {
    ScWorld world{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    prototype.gravity = 0;
    ScEntity *a = sc_entity(&world, sc_spawn(&world, &prototype));
    prototype.x = 10;
    ScEntity *b = sc_entity(&world, sc_spawn(&world, &prototype));
    CHECK(a && b);
    CHECK(sc_overlap(a, b));
    a->vx = 60;
    sc_step(&world);
    NEAR(a->x, 9); NEAR(b->x, 10);
    CHECK(sc_overlap(a, b));
    b->x = 15;
    CHECK(!sc_overlap(a, b));
    b->x = 14.99f;
    CHECK(sc_overlap(a, b));
    CHECK(sc_destroy(&world, b->id));
    CHECK(!sc_overlap(a, b) && !sc_overlap(a, nullptr));
}

static void test_particles_and_random(void) {
    ScWorld world{}, other{};
    fixture(&world); fixture(&other);
    CHECK(sc_random_u32(&world) == UINT32_C(3336926330));
    CHECK(sc_random_u32(&other) == UINT32_C(3336926330));
    for (int i = 0; i < 1000; ++i) {
        float value = sc_random(&world);
        CHECK(value >= 0 && value < 1);
        CHECK(value == sc_random(&other));
    }
    sc_emit(&world, 10, 10, INT_MAX, UINT32_C(0xff8800ff), 20, 0.02f);
    for (std::size_t i = 0; i < world.particles.size(); ++i) {
        CHECK(world.particles[i].life > 0 && world.particles[i].max_life > 0);
        CHECK(world.particles[i].size >= 1 && world.particles[i].size <= 3);
        CHECK(world.particles[i].color == UINT32_C(0xff8800ff));
    }
    std::uint32_t old_rng = world.rng;
    sc_emit(&world, 10, 10, 10, 0, 20, 1);
    CHECK(world.rng == old_rng);
    sc_step(&world); sc_step(&world);
    for (std::size_t i = 0; i < world.particles.size(); ++i) CHECK(world.particles[i].life == 0);
    sc_emit(&world, NAN, 0, 1, 0, 10, 1);
    sc_emit(&world, 0, 0, -1, 0, 10, 1);
    sc_emit(&world, 0, 0, 1, 0, INFINITY, 1);
    sc_emit(&world, 0, 0, 1, 0, 10, 0);
    CHECK(world.rng == old_rng);
    sc_emit(&world, 0, 0, 1, 0, 0, 1);
    CHECK(world.particles[0].life > 0 && world.particles[1].life == 0);
    world.particles[0].vx = INFINITY;
    sc_step(&world);
    CHECK(world.particles[0].life == 0);
}

static void test_camera(void) {
    ScWorld world{};
    fixture(&world);
    ScEntity prototype = body(100, 70);
    prototype.dynamic = false;
    std::uint32_t id = sc_spawn(&world, &prototype);
    CHECK(id != 0);
    world.camera_target = id;
    sc_step(&world);
    CHECK(world.camera_x > 0 && world.camera_x < 87);
    CHECK(world.camera_y > 0 && world.camera_y < 61);
    for (int i = 0; i < 180; ++i) sc_step(&world);
    NEAR(world.camera_x, 87); NEAR(world.camera_y, 61);
    ScEntity *entity = sc_entity(&world, id);
    CHECK(entity != nullptr);
    entity->x = 1000000; entity->y = 1000000;
    for (int i = 0; i < 180; ++i) sc_step(&world);
    NEAR(world.camera_x, 96); NEAR(world.camera_y, 72);
    world.view_width = 1000; world.view_height = 1000;
    sc_step(&world);
    NEAR(world.camera_x, 0); NEAR(world.camera_y, 0);
    CHECK(sc_destroy(&world, id) && world.camera_target == 0);
    world.camera_x = NAN; world.camera_y = INFINITY;
    sc_step(&world);
    CHECK(world.camera_x == 0 && world.camera_y == 0);
}

static void test_deterministic_simulation(void) {
    ScWorld world{}, other{};
    fixture(&world); fixture(&other);
    row(&world, 10, '#'); row(&other, 10, '#');
    ScEntity prototype = body(8, 8);
    prototype.vx = 72;
    std::uint32_t id = sc_spawn(&world, &prototype);
    std::uint32_t other_id = sc_spawn(&other, &prototype);
    CHECK(id != 0 && id == other_id);
    world.camera_target = other.camera_target = id;
    for (int tick = 0; tick < 600; ++tick) {
        std::uint32_t input = tick % 80 < 40 ? SC_RIGHT : SC_LEFT;
        sc_input(&world, input); sc_input(&other, input);
        ScEntity *a = sc_entity(&world, id), *b = sc_entity(&other, other_id);
        CHECK(a && b);
        a->vx = b->vx = input == SC_RIGHT ? 72 : -72;
        if (tick % 37 == 0) {
            sc_emit(&world, a->x, a->y, 12, 0xabcdef12u, 30, 0.5f);
            sc_emit(&other, b->x, b->y, 12, 0xabcdef12u, 30, 0.5f);
        }
        if (tick % 50 == 0 && a->grounded) a->vy = b->vy = -150;
        sc_step(&world); sc_step(&other);
        CHECK(sc_state_hash(&world) == sc_state_hash(&other));
    }
    CHECK(world.tick == 600);
    other.rng ^= 1;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
}

static void test_logical_hash(void) {
    ScWorld world{}, other{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    std::uint32_t id = sc_spawn(&world, &prototype);
    CHECK(id != 0);
    world.draw_count = 1;
    world.draws[0].kind = SC_DRAW_TEXT;
    std::strcpy(world.draws[0].text, "hello");
    other = world;
    other.title[100] = 'x';
    other.map.tiles[SC_MAX_TILES - 1] = '#';
    other.entities[42].x = NAN;
    other.entities[42].tag[0] = 'x';
    other.particles[42].x = NAN;
    other.draws[0].text[100] = 'x';
    std::size_t padding = offsetof(ScDraw, text) + sizeof(other.draws[0].text);
    for (std::size_t i = padding; i < sizeof(ScDraw); ++i)
        reinterpret_cast<unsigned char *>(&other.draws[0])[i] = 0xa5;
    other.camera_x = -0.0f;
    CHECK(sc_state_hash(&world) == sc_state_hash(&other));
    other.map.tiles[10] = '#';
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
    other = world; other.entities[id & 255u].vx = 1;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
    other = world; other.generations[42]++;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
    other = world; other.draws[0].text[0] = 'j';
    CHECK(sc_state_hash(&world) == sc_state_hash(&other));
    other = world; other.tick++;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
}

static void test_hash_ignores_presentation_queues(void) {
    ScWorld world{}, other{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    CHECK(sc_spawn(&world, &prototype) != 0);
    other = world;
    other.draw_count = SC_MAX_DRAWS;
    for (std::size_t i = 0; i < other.draws.size(); ++i) {
        other.draws[i] = ScDraw{
            .kind = SC_DRAW_RECT, .x = static_cast<float>(i), .y = 10,
            .w = 32, .h = 16, .color = UINT32_C(0xff8800ff), .screen = true
        };
    }
    other.tone_count = SC_MAX_TONES;
    for (std::size_t i = 0; i < other.tones.size(); ++i)
        other.tones[i] = ScTone{.frequency = 440, .duration = 0.1f, .volume = 0.5f};
    CHECK(sc_state_hash(&world) == sc_state_hash(&other));
    /* Interpolated draw positions and draining audio are presentation work. */
    other.draws[0].x += 0.5f;
    other.tone_count = 0;
    CHECK(sc_state_hash(&world) == sc_state_hash(&other));
    other.entities[0].x += 0.5f;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
}

static void test_malformed_native_state(void) {
    ScWorld world{};
    fixture(&world);
    ScEntity prototype = body(8, 8);
    std::uint32_t id = sc_spawn(&world, &prototype);
    ScEntity *entity = sc_entity(&world, id);
    CHECK(entity != nullptr);
    world.map.width = INT_MAX; world.map.height = INT_MAX;
    CHECK(sc_tile(&world, 100, 100) == '#');
    sc_step(&world);
    CHECK(entity->x == 8 && entity->y == 8 && entity->vx == 0 && entity->vy == 0);
    world.map.width = 16; world.map.height = 12; world.map.tile_size = 0;
    sc_step(&world);
    CHECK(entity->x == 8 && entity->y == 8);
    world.map.tile_size = INT_MAX;
    entity->vx = 1000000; entity->vy = 1000000;
    sc_step(&world);
    CHECK(std::isfinite(entity->x) && std::isfinite(entity->y));
    entity->x = FLT_MAX; entity->vy = INFINITY;
    sc_step(&world);
    CHECK(entity->vx == 0 && entity->vy == 0);
    world.map.height = INT_MIN;
    world.tone_count = INT_MAX; world.draw_count = INT_MAX;
    CHECK(sc_state_hash(&world) != 0);
    CHECK(sc_state_hash(nullptr) == 0);
    sc_step(nullptr); sc_input(nullptr, 0); sc_world_init(nullptr, 0);
    CHECK(sc_tile(nullptr, 0, 0) == '#');
    CHECK(sc_entity(nullptr, 1) == nullptr && !sc_destroy(nullptr, 1));
}

int main(void) {
    test_cpp_value_initialization();
    test_initialization_and_input();
    test_handles_and_capacity();
    test_spawn_validation();
    test_fixed_step_and_flags();
    test_floor_and_grounded();
    test_swept_walls_and_ceiling();
    test_map_perimeter();
    test_one_way_platforms();
    test_contact_precision_and_ledge();
    test_overlap_and_independent_bodies();
    test_particles_and_random();
    test_camera();
    test_deterministic_simulation();
    test_logical_hash();
    test_hash_ignores_presentation_queues();
    test_malformed_native_state();
    if (failures) {
        std::fprintf(stderr, "%d core test(s) failed (%d checks)\n", failures, checks);
        return 1;
    }
    std::printf("core: %d checks passed\n", checks);
    return 0;
}
