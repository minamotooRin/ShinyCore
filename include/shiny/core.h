#ifndef SHINY_CORE_H
#define SHINY_CORE_H

#include <array>
#include <cstddef>
#include <cstdint>

inline constexpr char SC_VERSION[] = "0.1.0";
inline constexpr float SC_DT = 1.0f / 60.0f;
inline constexpr int SC_MAX_ENTITIES = 256;
inline constexpr int SC_MAX_TILES = 16384;
inline constexpr int SC_MAX_PARTICLES = 1024;
inline constexpr int SC_MAX_TONES = 32;
inline constexpr int SC_MAX_DRAWS = 512;
inline constexpr int SC_PATH_MAX = 512;
inline constexpr int SC_ERROR_MAX = 2048;
inline constexpr std::uint32_t SC_DEFAULT_SEED = 0x6d2b79f5u;

enum : std::uint32_t { SC_LEFT=1, SC_RIGHT=2, SC_UP=4, SC_DOWN=8, SC_JUMP=16, SC_ACTION=32 };

struct ScEntity {
    std::uint32_t id{};
    bool alive{}, dynamic{}, solid{}, grounded{};
    float x{}, y{}, w{}, h{}, vx{}, vy{}, gravity{}, glow{};
    std::uint32_t color{};
    char tag[48]{}, sprite[128]{};
    int frame{}, frame_w{}, frame_h{}, layer{};
};

struct ScParticle {
    float x{}, y{}, vx{}, vy{}, life{}, max_life{}, size{};
    std::uint32_t color{};
};
struct ScTone { float frequency{}, duration{}, volume{}; };
enum ScDrawKind { SC_DRAW_RECT, SC_DRAW_CIRCLE, SC_DRAW_TEXT };
struct ScDraw {
    ScDrawKind kind{SC_DRAW_RECT};
    float x{}, y{}, w{}, h{};
    std::uint32_t color{};
    bool screen{};
    char text[192]{};
};

struct ScMap {
    int width{48}, height{27}, tile_size{8};
    // '.' empty, '#' solid, '=' one-way. Only width*height cells are active.
    std::array<char, SC_MAX_TILES> tiles = [] {
        std::array<char, SC_MAX_TILES> cells{};
        cells.fill('.');
        return cells;
    }();
    std::uint32_t color{0x183244ffu}, accent{0x28566fffu}, background{0x070b19ffu};
};

// Value initialization creates a ready, allocation-free default world. Reset or
// reseed explicitly with sc_world_init(); never clear object storage with memset.
struct ScWorld {
    ScMap map{};
    std::array<ScEntity, SC_MAX_ENTITIES> entities{};
    std::array<std::uint32_t, SC_MAX_ENTITIES> generations = [] {
        std::array<std::uint32_t, SC_MAX_ENTITIES> values{};
        values.fill(1);
        return values;
    }();
    std::array<ScParticle, SC_MAX_PARTICLES> particles{};
    std::array<ScTone, SC_MAX_TONES> tones{};
    std::array<ScDraw, SC_MAX_DRAWS> draws{};
    int tone_count{}, draw_count{};
    std::uint32_t held{}, pressed{}, released{}, rng{SC_DEFAULT_SEED};
    std::uint64_t tick{};
    float gravity{600}, camera_x{}, camera_y{}, ambient{0.4f};
    std::uint32_t camera_target{};
    int view_width{384}, view_height{216};
    char title[128]{"ShinyCore"}, message[192]{};
};

void sc_world_init(ScWorld *world, std::uint32_t seed);
/* Returns 0 on invalid values or capacity exhaustion; IDs are generation checked. */
std::uint32_t sc_spawn(ScWorld *world, const ScEntity *entity);
ScEntity *sc_entity(ScWorld *world, std::uint32_t id);
bool sc_destroy(ScWorld *world, std::uint32_t id);
std::uint32_t sc_find(const ScWorld *world, const char *tag);
void sc_input(ScWorld *world, std::uint32_t held);
void sc_step(ScWorld *world); /* Physics + particles + camera, exactly SC_DT. */
char sc_tile(const ScWorld *world, int x, int y);
bool sc_overlap(const ScEntity *a, const ScEntity *b);
std::uint32_t sc_random_u32(ScWorld *world);
float sc_random(ScWorld *world);
void sc_emit(ScWorld *world, float x, float y, int count, std::uint32_t color, float speed, float life);
std::uint64_t sc_state_hash(const ScWorld *world);

#endif
