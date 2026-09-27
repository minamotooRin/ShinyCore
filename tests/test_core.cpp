#include "shiny/core.h"
#include "shiny/physics.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
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
    static_assert(std::is_same_v<decltype(ScWorld::entities), std::vector<ScEntity>>);
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
    CHECK(world.particles.count==0);
    const auto initial_hash=sc_state_hash(&world);
    CHECK(initial_hash==UINT64_C(0x73dc3c94ee656cdf));
    world.map.tiles[1] = '#';
    world.held = SC_JUMP;
    world.tick = 42;
    sc_world_init(&world, 0);
    CHECK(sc_state_hash(&world) == initial_hash);
    sc_world_init(&world, 42);
    CHECK(sc_state_hash(&world) == UINT64_C(0x1327e893b2c81c77));
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
    ScEntityId first = sc_spawn(&world, &prototype);
    CHECK(first != 0 && sc_entity(&world, first) != nullptr);
    CHECK(sc_find(&world, "player") == first);
    CHECK(sc_find(&world, "missing") == 0 && sc_find(&world, "") == 0);
    world.camera_target = first;
    CHECK(sc_destroy(&world, first));
    CHECK(world.camera_target == 0);
    CHECK(sc_entity(&world, first) == nullptr && !sc_destroy(&world, first));
    ScEntityId second = sc_spawn(&world, &prototype);
    CHECK(second != first && second != 0);
    CHECK(sc_entity(&world, first) == nullptr && sc_entity(&world, second) != nullptr);
    std::array<ScEntityId, SC_MAX_ENTITIES> ids{};
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
    ScEntityId recycled = sc_spawn(&world, &prototype);
    CHECK(recycled != 0 && recycled != ids[123]);
    CHECK(sc_entity(&world, ids[123]) == nullptr);
    CHECK(sc_spawn(&world, &prototype) == 0);
    ScQueryShape area{{-1,-1,129,-1,129,97,-1,97},4,0};
    auto all=sc_physics_overlap(&world,area);
    CHECK(all.size()==ids.size()+1&&all.front()==0&&std::is_sorted(all.begin(),all.end()));
    CHECK(sc_physics_overlap(&world,area).data()==all.data());
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
    ScEntityId id = sc_spawn(&world, &prototype);
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
    CHECK(!sc_emit(&world, 10, 10, INT_MAX, UINT32_C(0xff8800ff), 20, 0.02f));
    CHECK(sc_emit(&world, 10, 10, SC_MAX_PARTICLES, UINT32_C(0xff8800ff), 20, 0.02f));
    for (std::size_t i = 0; i < world.particles.count; ++i) {
        CHECK(world.particles.life[i] > 0 && world.particles.max_life[i] > 0);
        CHECK(world.particles.size[i] >= 1 && world.particles.size[i] <= 3);
        CHECK(world.particles.color[i] == UINT32_C(0xff8800ff));
    }
    std::uint32_t old_rng = world.rng;
    CHECK(!sc_emit(&world, 10, 10, 10, 0, 20, 1));
    CHECK(world.rng == old_rng);
    sc_step(&world); sc_step(&world);
    CHECK(world.particles.count==0);
    CHECK(!sc_emit(&world, NAN, 0, 1, 0, 10, 1));
    CHECK(!sc_emit(&world, 0, 0, -1, 0, 10, 1));
    CHECK(!sc_emit(&world, 0, 0, 1, 0, INFINITY, 1));
    CHECK(!sc_emit(&world, 0, 0, 1, 0, 10, 0));
    CHECK(world.rng == old_rng);
    CHECK(sc_emit(&world, 0, 0, 1, 0, 0, 1));
    CHECK(world.particles.count==1 && world.particles.life[0]>0);
    world.particles.vx[0] = INFINITY;
    sc_step(&world);
    CHECK(world.particles.count==0);
}


static void test_deterministic_simulation(void) {
    const auto storage = std::make_unique<ScWorld>(), other_storage = std::make_unique<ScWorld>();
    auto& world = *storage; auto& other = *other_storage;
    fixture(&world); fixture(&other);
    row(&world, 10, '#'); row(&other, 10, '#');
    ScEntity prototype = body(8, 8);
    prototype.vx = 72;
    ScEntityId id = sc_spawn(&world, &prototype);
    ScEntityId other_id = sc_spawn(&other, &prototype);
    CHECK(id != 0 && id == other_id);
    world.camera_target = other.camera_target = id;
    for (int tick = 0; tick < 600; ++tick) {
        std::uint32_t input = tick % 80 < 40 ? SC_RIGHT : SC_LEFT;
        sc_input(&world, input); sc_input(&other, input);
        ScEntity *a = sc_entity(&world, id), *b = sc_entity(&other, other_id);
        CHECK(a && b);
        a->vx = b->vx = input == SC_RIGHT ? 72 : -72;
        if (tick % 37 == 0) {
            CHECK(sc_emit(&world, a->x, a->y, 12, 0xabcdef12u, 30, 0.5f));
            CHECK(sc_emit(&other, b->x, b->y, 12, 0xabcdef12u, 30, 0.5f));
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
    ScEntityId id = sc_spawn(&world, &prototype);
    CHECK(id != 0);
    world.draw_count = 1;
    world.draws[0].kind = SC_DRAW_TEXT;
    std::strcpy(world.draws[0].text, "hello");
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id);
    other.title[100] = 'x';
    other.map.tiles[SC_MAX_TILES - 1] = '#';
    other.entities[42].x = NAN;
    other.entities[42].tag[0] = 'x';
    other.particles.x[42] = NAN;
    other.draws[0].text[100] = 'x';
    std::size_t padding = offsetof(ScDraw, text) + sizeof(other.draws[0].text);
    for (std::size_t i = padding; i < sizeof(ScDraw); ++i)
        reinterpret_cast<unsigned char *>(&other.draws[0])[i] = 0xa5;
    other.camera_x = -0.0f;
    CHECK(sc_state_hash(&world) == sc_state_hash(&other));
    other.map.tiles[10] = '#';
    CHECK(sc_state_hash(&world) != sc_state_hash(&other));
    fixture(&other); CHECK(sc_spawn(&other, &prototype) == id); other.entities[sc_entity_slot(id)].vx = 1;
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


static void test_shape_queries() {
    auto storage=std::make_unique<ScWorld>(); auto* w=storage.get(); fixture(w);
    auto prototype=body(20,20); prototype.dynamic=false; prototype.body_type=1;
    prototype.w=prototype.h=20; prototype.shape=1;
    auto circle=sc_spawn(w,&prototype); CHECK(circle);
    prototype.body_type=0; auto art=sc_spawn(w,&prototype); CHECK(art);
    ScQueryShape shape{{21,21},1,1};
    CHECK(sc_physics_overlap(w,shape).empty()); // AABB corner is outside the circle.
    shape.points={30,30};
    auto ids=sc_physics_overlap(w,shape); CHECK(ids.size()==1&&ids[0]==circle);
    prototype.body_type=1; prototype.shape=0; prototype.x=70; prototype.y=20;
    prototype.w=30; prototype.h=4; prototype.angle=1.57079632679f;
    auto rotated=sc_spawn(w,&prototype); CHECK(rotated);
    shape.points={85,8}; ids=sc_physics_overlap(w,shape); CHECK(ids.size()==1&&ids[0]==rotated);
    shape.points={71,21}; CHECK(sc_physics_overlap(w,shape).empty());
    prototype=body(50,55); prototype.dynamic=false; prototype.body_type=1; prototype.sensor=true;
    prototype.category=prototype.mask=2; prototype.shape_count=2;
    prototype.shapes[0].w=prototype.shapes[0].h=8;
    prototype.shapes[1]=prototype.shapes[0]; prototype.shapes[1].x=4;
    auto compound=sc_spawn(w,&prototype); CHECK(compound);
    shape.points={56,59}; shape.radius=3;
    ids=sc_physics_overlap(w,shape); CHECK(ids.size()==1&&ids[0]==compound);
    sc_entity(w,compound)->solid=false; CHECK(sc_physics_overlap(w,shape).empty());
    sc_entity(w,compound)->solid=true;
    shape.points={-1,30}; ids=sc_physics_overlap(w,shape); CHECK(ids.size()==1&&ids[0]==0);
    shape.points={10,30}; shape.radius=2;
    auto hit=sc_physics_sweep(w,shape,50,0); CHECK(hit.hit&&hit.id==circle);
    CHECK(std::fabs(hit.fraction-.16f)<.02f&&hit.nx<-.99f);
    shape.points={30,30}; hit=sc_physics_sweep(w,shape,1,0);
    CHECK(hit.hit&&hit.id==circle&&hit.fraction==0&&hit.nx==0&&hit.ny==0);
    CHECK(sc_destroy(w,circle));
    prototype=body(20,20); prototype.body_type=1; prototype.dynamic=false; prototype.w=prototype.h=20; prototype.shape=1;
    auto replacement=sc_spawn(w,&prototype); CHECK(replacement>compound);
    auto twin=sc_spawn(w,&prototype); CHECK(twin);
    shape={{10,5,115,5,115,80,10,80},4,0}; ids=sc_physics_overlap(w,shape);
    CHECK(ids.size()==4&&std::is_sorted(ids.begin(),ids.end()));
    auto closest=std::min(replacement,twin);
    CHECK(sc_physics_ray(w,10,30,50,0).id==closest);
    shape={{10,30},1,2}; CHECK(sc_physics_sweep(w,shape,50,0).id==closest);
    shape.radius=0; bool rejected=false;
    try { (void)sc_physics_overlap(w,shape); } catch(const std::runtime_error&) { rejected=true; }
    CHECK(rejected);
}

static void test_joint_controls() {
    for(int kind=0;kind<3;++kind) {
        auto storage=std::make_unique<ScWorld>(); auto* w=storage.get(); fixture(w);
        w->map.width=w->map.height=64; w->gravity=0;
        auto prototype=body(80,80); prototype.w=prototype.h=10;
        prototype.body_type=1; prototype.dynamic=false; prototype.solid=false;
        auto a=sc_spawn(w,&prototype); CHECK(a);
        prototype.body_type=3; prototype.fixed_rotation=false; prototype.x=kind==1?80:120;
        auto b=sc_spawn(w,&prototype); CHECK(b);
        auto j=kind==0?sc_physics_joint(w,kind,a,b,85,85,40,std::array{125.f,85.f}):sc_physics_joint(w,kind,a,b,85,85,24);
        CHECK(j);
        auto config=sc_physics_joint_control(w,j); CHECK(config);
        config->limit=true; config->motor=true; config->max_effort=100;
        config->lower=kind==0?20:kind==1?-.4f:0;
        config->upper=kind==0?60:kind==1?.4f:24;
        config->speed=kind==1?1:48;
        CHECK(sc_physics_joint_control(w,j,*config));
        auto invalid=*config; invalid.lower=invalid.upper+1;
        CHECK(!sc_physics_joint_control(w,j,invalid));
        CHECK(sc_physics_joint_control(w,j)->lower==config->lower);
        for(int i=0;i<120;++i) sc_physics_step(w);
        auto* e=sc_entity(w,b);
        float position=kind==0?e->x-80:kind==1?e->angle:e->y-80;
        if(std::fabs(position-config->upper)>=(kind==1?.03f:1.f))
            std::fprintf(stderr,"joint kind %d: upper position %g, expected %g\n",kind,position,config->upper);
        CHECK(std::fabs(position-config->upper)<(kind==1?.03f:1.f));
        config->speed=-config->speed; CHECK(sc_physics_joint_control(w,j,*config));
        for(int i=0;i<120;++i) sc_physics_step(w);
        position=kind==0?e->x-80:kind==1?e->angle:e->y-80;
        CHECK(std::fabs(position-config->lower)<(kind==1?.03f:1.f));
        if(kind==0) {
            config->lower=config->upper=30; CHECK(sc_physics_joint_control(w,j,*config));
            for(int i=0;i<120;++i) sc_physics_step(w);
            CHECK(std::fabs(e->x-80-30)<1);
            config->motor=config->limit=false; CHECK(sc_physics_joint_control(w,j,*config));
            for(int i=0;i<120;++i) sc_physics_step(w);
            CHECK(std::fabs(e->x-80-40)<1);
        }
        e->w+=1; CHECK(!sc_physics_joint_control(w,j)); // Body rebuild invalidates the handle.
        CHECK(!sc_physics_joint_control(w,j,*config));
    }
}
static void test_joint_room_ids() {
    auto storage=std::make_unique<ScWorld>(); auto* w=storage.get(); fixture(w);
    auto prototype=body(20,20); auto a=sc_spawn(w,&prototype); prototype.x=40; auto b=sc_spawn(w,&prototype);
    auto first=sc_physics_joint(w,0,a,b,23,23,20,std::array{43.f,23.f}); CHECK(first>UINT32_MAX);
    fixture(w); w->epoch=0xfffff;
    prototype.x=20; a=sc_spawn(w,&prototype); prototype.x=40; b=sc_spawn(w,&prototype);
    auto last=sc_physics_joint(w,0,a,b,23,23,20,std::array{43.f,23.f});
    CHECK(last&&last<=SC_ID_MAX&&static_cast<ScJointId>(static_cast<double>(last))==last);
    CHECK(!sc_physics_joint_destroy(w,first)&&!sc_physics_joint_control(w,first));
    CHECK(!sc_physics_joint_control(w,SC_ID_MAX+1));
    CHECK(sc_physics_joint_control(w,last));
    CHECK(sc_physics_joint_destroy(w,last));
}

static void test_terrain_chunks() {
    auto storage=std::make_unique<ScWorld>(); auto* w=storage.get(); fixture(w);
    w->map.width=64; w->map.height=32;
    for(int x=0;x<64;++x) w->map.tiles[16*64+x]='#';
    sc_physics_sync(w);
    auto initial=sc_physics_terrain_stats(*w);
    CHECK(initial.chunks==3&&initial.replacements==3); // Two floor chunks and room bounds.
    CHECK(sc_physics_ray(w,20,100,0,50).hit);
    w->map.tiles[16*64+2]='.';
    sc_physics_sync(w);
    CHECK(sc_physics_terrain_stats(*w).replacements==initial.replacements+1);
    CHECK(!sc_physics_ray(w,20,100,0,50).hit);
    CHECK(sc_physics_ray(w,300,100,0,50).hit);
    for(int x=0;x<32;++x) w->map.tiles[16*64+x]='.';
    sc_physics_sync(w);
    CHECK(sc_physics_terrain_stats(*w).chunks==2);
    CHECK(sc_physics_terrain_stats(*w).replacements==initial.replacements+1);
    w->terrain_shapes.push_back({280,80,16,16}); ++w->terrain_revision;
    sc_physics_sync(w);
    auto before=sc_physics_terrain_stats(*w);
    CHECK(before.chunks==2&&before.replacements==initial.replacements+2);
    CHECK(sc_physics_ray(w,285,60,0,40).hit);
    w->map.tiles[16*64+2]='#'; // A valid candidate is prepared before the bad polygon.
    w->terrain_shapes[0].vertex_count=3; ++w->terrain_revision;
    bool rejected=false;
    try { sc_physics_sync(w); } catch(const std::runtime_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(sc_physics_terrain_stats(*w).chunks==before.chunks);
    CHECK(sc_physics_terrain_stats(*w).replacements==before.replacements);
    w->terrain_shapes[0].vertex_count=0;
    sc_physics_sync(w);
    CHECK(sc_physics_terrain_stats(*w).replacements==before.replacements+1);
    CHECK(sc_physics_ray(w,20,100,0,50).hit);
    sc_physics_sync(w);
    CHECK(sc_physics_terrain_stats(*w).replacements==before.replacements+1);
    auto current=sc_physics_terrain_stats(*w);
    w->terrain_shapes.push_back({-1,-1,2,2}); ++w->terrain_revision;
    sc_physics_sync(w);
    CHECK(sc_physics_terrain_stats(*w).chunks==current.chunks+1);
    CHECK(sc_physics_terrain_stats(*w).replacements==current.replacements+1);
    w->terrain_shapes.pop_back(); ++w->terrain_revision;
    sc_physics_sync(w);
    CHECK(sc_physics_terrain_stats(*w).chunks==current.chunks);
    CHECK(sc_physics_terrain_stats(*w).replacements==current.replacements+1);
    current=sc_physics_terrain_stats(*w);
    w->map.height=33;
    sc_physics_sync(w);
    CHECK(sc_physics_terrain_stats(*w).chunks==current.chunks);
    CHECK(sc_physics_terrain_stats(*w).replacements==current.replacements+1);
    auto floor=sc_physics_ray(w,100,240,0,40);
    CHECK(floor.hit&&std::fabs(floor.y-264)<.01f);
}

static void test_spawn_many() {
    auto owner=std::make_unique<ScWorld>(); auto& world=*owner;
    world.entities.resize(3); world.generations.resize(3);
    world.identities=std::make_unique<ScIdentities>(2);
    std::array<ScEntity,2> drafts{body(10,20),body(30,40)};
    std::strcpy(drafts[0].persistent_id,"map/a");
    std::strcpy(drafts[1].persistent_id,"map/a");
    CHECK(!sc_spawn_many(world,drafts));
    CHECK(!world.entities[0].alive&&world.identities->size()==0&&drafts[0].id==0);
    std::strcpy(drafts[1].persistent_id,"map/b"); drafts[1].w=0;
    CHECK(!sc_spawn_many(world,drafts));
    CHECK(world.identities->size()==0&&world.generations[0]==1);
    drafts[1].w=6;
    CHECK(sc_spawn_many(world,drafts));
    CHECK(sc_entity_slot(drafts[0].id)==0&&sc_entity_slot(drafts[1].id)==1);
    CHECK(sc_entity(&world,drafts[0].id)->x==10&&world.identities->size()==2);
    CHECK(!sc_spawn_many(world,std::span<ScEntity>{world.entities.data(),1}));
    const auto old=drafts[0].id;
    CHECK(sc_destroy(&world,old,false));
    CHECK(sc_destroy(&world,drafts[1].id));
    std::strcpy(drafts[1].persistent_id,"map/c");
    CHECK(!sc_spawn_many(world,drafts));
    CHECK(world.identities->find("map/a")->status==ScIdentityStatus::unloaded);
    CHECK(world.identities->find("map/b")->status==ScIdentityStatus::deleted);
    CHECK(world.generations[0]==2&&!world.entities[0].alive&&drafts[0].id==old);
    std::strcpy(drafts[1].persistent_id,"map/b");
    world.generations[1]=world.generations[2]=0;
    CHECK(!sc_spawn_many(world,drafts));
    CHECK(!world.entities[0].alive&&world.generations[0]==2);
    world.generations[2]=1;
    CHECK(sc_spawn_many(world,drafts));
    CHECK(drafts[0].id!=old&&sc_entity_slot(drafts[1].id)==2);
    CHECK(world.identities->find("map/b")->status==ScIdentityStatus::active);
    CHECK(sc_spawn_many(world,{}));
}

static void test_stream_terrain_replace() {
    auto world=std::make_unique<ScWorld>();
    std::array<ScTerrainShape,2> shapes{{{-40,32,32,8},{300,32,32,8}}};
    CHECK(sc_physics_replace_terrain(*world,shapes));
    CHECK(!world->map.bounded&&sc_tile(world.get(),-1,0)=='.');
    auto ray=sc_physics_ray(world.get(),-24,0,0,64); CHECK(ray.hit&&ray.id==0); NEAR(ray.y,32);
    const auto before=sc_physics_terrain_stats(*world);
    CHECK(world->map.navigation_blocked[4*world->map.width+37]);
    const auto navigation_revision=world->map.navigation_revision;
    auto invalid=shapes; invalid[1].vertex_count=3; invalid[1].vertices.fill(0);
    CHECK(!sc_physics_replace_terrain(*world,invalid));
    CHECK(sc_physics_terrain_stats(*world).replacements==before.replacements);
    CHECK(world->terrain_shapes[1].y==32&&sc_physics_ray(world.get(),-24,0,0,64).hit);
    CHECK(world->map.navigation_revision==navigation_revision);
    shapes[1].y=40; CHECK(sc_physics_replace_terrain(*world,shapes));
    CHECK(!world->map.navigation_blocked[4*world->map.width+37]);
    CHECK(world->map.navigation_blocked[5*world->map.width+37]);
    CHECK(world->map.navigation_revision==navigation_revision+1);
    CHECK(sc_physics_terrain_stats(*world).replacements==before.replacements+1);
    world->camera_x=-128; world->camera_y=-64; sc_step(world.get());
    NEAR(world->camera_x,-128); NEAR(world->camera_y,-64);
    CHECK(sc_physics_replace_terrain(*world,{}));
    CHECK(!sc_physics_ray(world.get(),-24,0,0,64).hit);
}

int main(void) {
    test_stream_terrain_replace();
    test_spawn_many();
    test_terrain_chunks();
    test_joint_room_ids();
    test_joint_controls();
    test_shape_queries();
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
