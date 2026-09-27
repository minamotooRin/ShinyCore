#ifndef SHINY_CORE_H
#define SHINY_CORE_H

#include "shiny/input.h"
#include "shiny/camera.h"
#include "shiny/presentation.h"
#include "shiny/identity.h"
#include "shiny/particles.h"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

inline constexpr char SC_VERSION[] = "1.0.0-dev";
inline constexpr float SC_DT = 1.0f / 60.0f;
inline constexpr int SC_MAX_ENTITIES = 4096;
inline constexpr int SC_MAX_IDENTITIES = 4096;
inline constexpr int SC_MAX_TILES = 16384;
inline constexpr int SC_MAX_PARTICLES = 32768;
inline constexpr int SC_MAX_TONES = 32;
inline constexpr int SC_MAX_DRAWS = 4096;
inline constexpr int SC_PATH_MAX = 512;
inline constexpr int SC_ERROR_MAX = 2048;
inline constexpr std::uint32_t SC_DEFAULT_SEED = 0x6d2b79f5u;

enum : std::uint32_t { SC_LEFT=1, SC_RIGHT=2, SC_UP=4, SC_DOWN=8, SC_JUMP=16, SC_INTERACT=32 };

using ScEntityId = std::uint64_t;
inline constexpr ScEntityId SC_ID_MAX = (ScEntityId{1} << 52) - 1;
inline constexpr std::size_t sc_entity_slot(ScEntityId id) { return id & 65535; }

struct ScBodyShape {
    int kind{};
    float x{},y{},w{8},h{8};
    std::array<float,16> vertices{};
    int vertex_count{};
    bool operator==(const ScBodyShape&) const = default;
};
struct ScEntity {
    ScEntityId id{};
    ScEntityId parent{}; // Visual attachment only; never a solver constraint or persistent reference.
    ScPose local_pose{};
    bool alive{}, dynamic{}, solid{}, grounded{};
    float x{}, y{}, w{}, h{}, vx{}, vy{}, gravity{}, glow{};
    std::uint32_t color{};
    char tag[48]{}, sprite[128]{};
    char persistent_id[128]{};
    int frame{}, frame_w{}, frame_h{}, layer{};
    // 0 absent, 1 static, 2 kinematic, 3 dynamic. Authored pixels, radians.
    int body_type{}, shape{}; // box, circle, capsule, convex polygon
    float angle{}, angular_velocity{}, density{1}, friction{0.3f}, restitution{};
    bool fixed_rotation{true}, sensor{}, bullet{}, one_way{}, flip_x{}, flip_y{};
    std::uint32_t category{1}, mask{0xffffffffu};
    ScEntityId support{};
    float normal_x{}, normal_y{}, drop_time{}, force_x{}, force_y{}, impulse_x{}, impulse_y{};
    std::array<float,16> vertices{};
    int vertex_count{};
    std::array<ScBodyShape,4> shapes{};
    int shape_count{};
};

struct ScContact { ScEntityId a{},b{}; float nx{},ny{}; bool sensor{}; int phase{}; };
struct ScPhysics;
struct ScPhysicsDeleter { void operator()(ScPhysics*) const noexcept; };

struct ScTone { float frequency{}, duration{}, volume{}; };
enum ScDrawKind { SC_DRAW_RECT, SC_DRAW_CIRCLE, SC_DRAW_TEXT, SC_DRAW_CLIP, SC_DRAW_UNCLIP, SC_DRAW_IMAGE };
struct ScDraw {
    std::uint64_t material{};
    bool default_material{true};
    ScDrawKind kind{SC_DRAW_RECT};
    float x{}, y{}, w{}, h{};
    std::uint32_t color{};
    bool screen{};
    char text[512]{},font[128]{};
    float wrap{};
    int align{};
    float source_x{},source_y{},source_w{},source_h{};
    float slice_left{},slice_right{},slice_top{},slice_bottom{};
    bool flip_x{},flip_y{},diagonal{};
    bool layered{};
    int layer{};
};

struct ScMap {
    bool bounded{true};
    int width{48}, height{27}, tile_size{8};
    std::bitset<SC_MAX_TILES> navigation_blocked;
    std::uint64_t navigation_revision{};
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
    bool operator==(const ScTerrainShape&) const = default;
};
struct ScGlyph { int codepoint{},advance{}; };
struct ScResource {
    std::string name,type,path,characters;
    int size{16};
    int image_width{},image_height{}; // PNG header dimensions, available without a GPU.
    bool streamed{}; // Explicit PNG residency through sc.images; optional streaming module.
    float duration{};
    mutable std::vector<ScGlyph> glyphs;
    std::vector<unsigned char> font_bytes;
};
class ScProjectiles;
struct ScProjectilesDeleter { void operator()(ScProjectiles*) const noexcept; };
struct ScWorld {
    std::unique_ptr<ScPresentation> presentation;
    std::unique_ptr<ScIdentities> identities = std::make_unique<ScIdentities>(SC_MAX_IDENTITIES);
    std::unique_ptr<ScProjectiles,ScProjectilesDeleter> projectiles;
    std::unique_ptr<ScPhysics,ScPhysicsDeleter> physics;
    ScMap map{};
    std::vector<ScEntity> entities = std::vector<ScEntity>(SC_MAX_ENTITIES);
    std::vector<std::uint32_t> generations = std::vector<std::uint32_t>(SC_MAX_ENTITIES, 1);
    std::uint32_t epoch{1}, visual_rng{0x91e10da5};
    bool simulation_paused{}, exit_requested{};
    bool text_focus{}, clipboard_write{};
    float text_x{},text_y{};
    std::array<char,4096> clipboard_out{};
    ScParticles particles{};
    std::array<ScTone, SC_MAX_TONES> tones{};
    std::vector<ScDraw> draws = std::vector<ScDraw>(SC_MAX_DRAWS);
    int tone_count{}, draw_count{};
    ScDeviceInput input{};
    std::uint32_t held{}, pressed{}, released{}, rng{SC_DEFAULT_SEED};
    std::uint64_t tick{};
    float gravity{600}, camera_x{}, camera_y{}, ambient{0.4f};
    ScEntityId camera_target{};
    ScCamera camera{};
    int view_width{384}, view_height{216};
    char title[128]{"ShinyCore"}, message[192]{};
    std::vector<ScContact> contacts = std::vector<ScContact>(16384);
    int contact_count{};
    char error[SC_ERROR_MAX]{};
    std::vector<ScLayer> layers;
    std::vector<ScTileGraphic> tile_graphics;
    std::vector<ScTerrainShape> object_terrain; // Authored static objects survive tile edits.
    std::vector<ScTerrainShape> terrain_shapes;
    std::uint64_t terrain_revision{};
    std::vector<ScResource> resources;

};

void sc_world_init(ScWorld *world, std::uint32_t seed);
/* Returns 0 on invalid values or capacity exhaustion; IDs are generation checked. */
ScEntityId sc_spawn(ScWorld *world, const ScEntity *entity,const char** error=nullptr);
// Drafts must not alias the world pool. Success fills their new IDs;
// failure leaves drafts and world unchanged. No storage grows here.
// Read-only preflight, reusable by native transactions; world must not change before commit.
std::expected<void,const char*> sc_spawn_preflight(const ScWorld&,std::span<const ScEntity> drafts);
// Optional parents are zero (root) or one-based batch indices. Attached drafts
// use local x/y/angle; all relationships and world poses are preflighted atomically.
std::expected<void,const char*> sc_spawn_many(ScWorld&,std::span<ScEntity> drafts,
                                          std::span<const std::size_t> parents={});
ScEntity *sc_entity(ScWorld *world, ScEntityId id);
bool sc_destroy(ScWorld *world, ScEntityId id,bool deleted=true);
ScEntityId sc_find(const ScWorld *world, const char *tag);
void sc_input(ScWorld *world, std::uint32_t held);
struct ScStepProfile;
void sc_step(ScWorld *world, ScStepProfile* profile=nullptr); /* Exactly SC_DT; optional timings accumulate. */
char sc_tile(const ScWorld *world, int x, int y);
bool sc_overlap(const ScEntity *a, const ScEntity *b);
std::uint32_t sc_random_u32(ScWorld *world);
float sc_random(ScWorld *world);
std::expected<std::size_t,const char*> sc_emit(ScWorld *world, float x, float y, int count, std::uint32_t color, float speed, float life);
std::uint64_t sc_state_hash(const ScWorld *world);

#endif
