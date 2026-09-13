#ifndef SHINY_CORE_H
#define SHINY_CORE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

inline constexpr char SC_VERSION[] = "0.2.0";
inline constexpr float SC_DT = 1.0f / 60.0f;
inline constexpr int SC_MAX_ENTITIES = 256;
inline constexpr int SC_MAX_TILES = 16384;
inline constexpr int SC_MAX_PARTICLES = 1024;
inline constexpr int SC_MAX_TONES = 32;
inline constexpr int SC_MAX_DRAWS = 512;
inline constexpr int SC_PATH_MAX = 512;
inline constexpr int SC_ERROR_MAX = 2048;
inline constexpr std::uint32_t SC_DEFAULT_SEED = 0x6d2b79f5u;

enum : std::uint32_t { SC_LEFT=1, SC_RIGHT=2, SC_UP=4, SC_DOWN=8, SC_JUMP=16, SC_INTERACT=32 };

struct ScBodyShape {
    int kind{};
    float x{},y{},w{8},h{8};
    std::array<float,16> vertices{};
    int vertex_count{};
    bool operator==(const ScBodyShape&) const = default;
};
struct ScEntity {
    std::uint32_t id{};
    bool alive{}, dynamic{}, solid{}, grounded{};
    float x{}, y{}, w{}, h{}, vx{}, vy{}, gravity{}, glow{};
    std::uint32_t color{};
    char tag[48]{}, sprite[128]{};
    int frame{}, frame_w{}, frame_h{}, layer{};
    // 0 absent, 1 static, 2 kinematic, 3 dynamic. Authored pixels, radians.
    int body_type{}, shape{}; // box, circle, capsule, convex polygon
    float angle{}, angular_velocity{}, density{1}, friction{0.3f}, restitution{};
    bool fixed_rotation{true}, sensor{}, bullet{}, one_way{}, flip_x{}, flip_y{};
    std::uint32_t category{1}, mask{0xffffffffu}, support{};
    float normal_x{}, normal_y{}, drop_time{}, force_x{}, force_y{}, impulse_x{}, impulse_y{};
    std::array<float,16> vertices{};
    int vertex_count{};
    std::array<ScBodyShape,4> shapes{};
    int shape_count{};
};

struct ScContact { std::uint32_t a{},b{}; float nx{},ny{}; bool sensor{}; int phase{}; };
struct ScPhysics;
struct ScPhysicsDeleter { void operator()(ScPhysics*) const noexcept; };

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
    char text[512]{},font[128]{};
    float wrap{};
    int align{};
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

// Reset with sc_world_init(); never clear owning object storage with memset.
struct ScTileGraphic {
    std::uint32_t gid{};
    std::string image;
    int x{},y{},w{},h{};
    char collision{'.'};
    std::array<float,16> vertices{};
    int vertex_count{};
};
struct ScLayer {
    std::string name;
    int order{};
    float opacity{1},x{},y{};
    bool visible{true};
    std::vector<std::uint32_t> cells;
};
struct ScTerrainShape {
    float x{},y{},w{},h{};
    bool one_way{};
    std::array<float,16> vertices{};
    int vertex_count{};
};
struct ScGlyph { int codepoint{},advance{}; };
struct ScResource {
    std::string name,type,path,characters;
    int size{16};
    float duration{};
    std::vector<ScGlyph> glyphs;
};
struct ScAudioVoice {
    std::uint32_t id{};
    bool alive{},music{},loop{},paused{},persistent{},stopping{};
    float volume{1},target_volume{1},pitch{1},fade{},position{},duration{};
    char path[128]{};
};
struct ScWorld {
    std::unique_ptr<ScPhysics,ScPhysicsDeleter> physics;
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
    std::array<ScContact,1024> contacts{};
    int contact_count{};
    char error[SC_ERROR_MAX]{};
    std::vector<ScLayer> layers;
    std::vector<ScTileGraphic> tile_graphics;
    std::vector<ScTerrainShape> terrain_shapes;
    std::uint64_t terrain_revision{};
    std::vector<ScResource> resources;
    std::array<ScAudioVoice,34> audio{};
    std::array<std::uint32_t,34> audio_generations{};
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
