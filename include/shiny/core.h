#ifndef SHINY_CORE_H
#define SHINY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SC_VERSION "0.1.0"
#define SC_DT (1.0f / 60.0f)
#define SC_MAX_ENTITIES 256
#define SC_MAX_TILES 16384
#define SC_MAX_PARTICLES 1024
#define SC_MAX_TONES 32
#define SC_MAX_DRAWS 512
#define SC_PATH_MAX 512
#define SC_ERROR_MAX 2048

enum { SC_LEFT=1, SC_RIGHT=2, SC_UP=4, SC_DOWN=8, SC_JUMP=16, SC_ACTION=32 };

typedef struct {
    uint32_t id;
    bool alive, dynamic, solid, grounded;
    float x, y, w, h, vx, vy, gravity, glow;
    uint32_t color;
    char tag[48], sprite[128];
    int frame, frame_w, frame_h, layer;
} ScEntity;

typedef struct { float x,y,vx,vy,life,max_life,size; uint32_t color; } ScParticle;
typedef struct { float frequency, duration, volume; } ScTone;
typedef enum { SC_DRAW_RECT, SC_DRAW_CIRCLE, SC_DRAW_TEXT } ScDrawKind;
typedef struct {
    ScDrawKind kind;
    float x,y,w,h;
    uint32_t color;
    bool screen;
    char text[192];
} ScDraw;

typedef struct {
    int width,height,tile_size;
    char tiles[SC_MAX_TILES]; /* '.' empty, '#' solid, '=' one-way */
    uint32_t color, accent, background;
} ScMap;

typedef struct {
    ScMap map;
    ScEntity entities[SC_MAX_ENTITIES];
    uint32_t generations[SC_MAX_ENTITIES];
    ScParticle particles[SC_MAX_PARTICLES];
    ScTone tones[SC_MAX_TONES];
    ScDraw draws[SC_MAX_DRAWS];
    int tone_count, draw_count;
    uint32_t held, pressed, released, rng;
    uint64_t tick;
    float gravity, camera_x, camera_y, ambient;
    uint32_t camera_target;
    int view_width, view_height;
    char title[128], message[192];
} ScWorld;

void sc_world_init(ScWorld *world, uint32_t seed);
/* Returns 0 on invalid values or capacity exhaustion; IDs are generation checked. */
uint32_t sc_spawn(ScWorld *world, const ScEntity *entity);
ScEntity *sc_entity(ScWorld *world, uint32_t id);
bool sc_destroy(ScWorld *world, uint32_t id);
uint32_t sc_find(const ScWorld *world, const char *tag);
void sc_input(ScWorld *world, uint32_t held);
void sc_step(ScWorld *world); /* Physics + particles + camera, exactly SC_DT. */
char sc_tile(const ScWorld *world, int x, int y);
bool sc_overlap(const ScEntity *a, const ScEntity *b);
uint32_t sc_random_u32(ScWorld *world);
float sc_random(ScWorld *world);
void sc_emit(ScWorld *world, float x, float y, int count, uint32_t color, float speed, float life);
uint64_t sc_state_hash(const ScWorld *world);

#endif
