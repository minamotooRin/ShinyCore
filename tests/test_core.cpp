#include "shiny/core.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
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

static void test_cpp_value_initialization(void) {
    static_assert(std::is_aggregate_v<ScWorld>);
    static_assert(!std::is_copy_constructible_v<ScWorld>);
    static_assert(std::is_same_v<decltype(ScWorld::entities), std::array<ScEntity, SC_MAX_ENTITIES>>);
    // Bare default initialization must be safe, including every bounded pool.
    const auto storage = std::unique_ptr<ScWorld>(new ScWorld);
    auto& world = *storage;
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
    // Golden logical-field hash includes the neutral device input snapshot.
    CHECK(sc_state_hash(&world) == UINT64_C(0x2987410a525336f8));
    world.map.tiles[1] = '#';
    world.held = SC_JUMP;
    world.tick = 42;
    sc_world_init(&world, 0);
    CHECK(sc_state_hash(&world) == UINT64_C(0x2987410a525336f8));
    sc_world_init(&world, 42);
    CHECK(sc_state_hash(&world) == UINT64_C(0x8e7afd2936df5550));
    const auto copy_storage = std::unique_ptr<ScWorld>(new ScWorld);
    auto& copy = *copy_storage; copy.map = world.map;
    copy.map.tiles[1] = '#';
    CHECK(world.map.tiles[1] == '.' && copy.map.tiles[1] == '#');
}

static void test_initialization_and_input(void) {
    const auto storage = std::make_unique<ScWorld>();
    auto& world = *storage;
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
    const auto storage = std::make_unique<ScWorld>();
    auto& world = *storage;
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
    const auto storage = std::make_unique<ScWorld>();
    auto& world = *storage;
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








static void test_particles_and_random(void) {
    const auto storage = std::make_unique<ScWorld>(), other_storage = std::make_unique<ScWorld>();
    auto& world = *storage; auto& other = *other_storage;
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


static void test_deterministic_simulation(void) {
    const auto storage = std::make_unique<ScWorld>(), other_storage = std::make_unique<ScWorld>();
    auto& world = *storage; auto& other = *other_storage;
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
    const auto storage = std::make_unique<ScWorld>(), other_storage = std::make_unique<ScWorld>();
    auto& world = *storage; auto& other = *other_storage;
    fixture(&world);
    ScEntity prototype = body(8, 8);
    std::uint32_t id = sc_spawn(&world, &prototype);
    CHECK(id != 0);
    world.draw_count = 1;
    world.draws[0].kind = SC_DRAW_TEXT;
    std::strcpy(world.draws[0].text, "hello");
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id);
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
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id); other.entities[id & 255u].vx = 1;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id); other.generations[42]++;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id); other.draws[0].text[0] = 'j';
    CHECK(sc_state_hash(&world) == sc_state_hash(&other));
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id); other.tick++;
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
}

static void test_hash_ignores_presentation_queues(void) {
    const auto storage = std::make_unique<ScWorld>(), other_storage = std::make_unique<ScWorld>();
    auto& world = *storage; auto& other = *other_storage;
    fixture(&world);
    ScEntity prototype = body(8, 8);
    auto id=sc_spawn(&world, &prototype); CHECK(id != 0);
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id);
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


int main(void) {
    test_cpp_value_initialization();
    test_initialization_and_input();
    test_handles_and_capacity();
    test_spawn_validation();
    test_particles_and_random();
    test_deterministic_simulation();
    test_logical_hash();
    test_hash_ignores_presentation_queues();
    if (failures) {
        std::fprintf(stderr, "%d core test(s) failed (%d checks)\n", failures, checks);
        return 1;
    }
    std::printf("core: %d checks passed\n", checks);
    return 0;
}
