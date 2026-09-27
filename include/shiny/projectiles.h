#pragma once
#include "shiny/core.h"
#include <span>

struct ScProjectileSpec {
    float x{}, y{}, vx{}, vy{}, ax{}, ay{}, radius{2}, life{3};
    std::uint32_t mask{0xffffffff}, color{0xffffffff};
    bool terrain{true}, piercing{};
    std::uint8_t sprite{};
};
struct ScProjectileSprite {
    std::uint16_t resource{};
    int x{},y{},w{},h{};
    float width{},height{};
};
struct ScProjectileHit {
    std::uint64_t projectile{};
    ScEntityId target{};
    float fraction{}, x{}, y{};
};
// A dedicated data-oriented pool; no Lua callbacks or Box2D bodies per bullet.
class ScProjectiles final {
public:
    explicit ScProjectiles(std::size_t capacity);
    std::vector<float> x,y,vx,vy,ax,ay,radius,life;
    std::vector<ScDisplayPoint> previous_display;
    std::vector<std::uint32_t> mask,color;
    std::vector<std::uint64_t> ids;
    std::vector<std::uint8_t> flags,sprite;
    std::array<ScProjectileSprite,64> sprites{};
    std::size_t sprite_count{};
    std::vector<ScProjectileHit> hits;
    std::size_t count{};
    std::uint64_t next_id{1}, total_hits{};
    std::vector<std::uint64_t> spawn(std::span<const ScProjectileSpec> batch);
    // Nonallocating commit; generated IDs form [return value, return value+batch.size()).
    std::uint64_t spawn_batch(std::span<const ScProjectileSpec> batch);
    void step(const ScWorld& world);
    void clear() noexcept;
    std::uint8_t add_sprite(const ScProjectileSprite&);
    std::span<const std::size_t> draw_order();
private:
    struct Cell { int x,y; std::size_t entity; };
    struct Target { float x,y,w,h; std::uint32_t category; ScEntityId id; };
    std::vector<Target> targets_;
    std::vector<Cell> cells_;
    std::vector<std::uint32_t> cell_lookup_;
    bool cell_lookup_ready_{};
    std::vector<Cell> terrain_cells_;
    const ScWorld* terrain_world_{}; // Identity only; never dereferenced.
    std::uint64_t terrain_revision_{};
    std::uint32_t terrain_epoch_{};
    std::vector<std::uint32_t> terrain_marks_;
    std::uint32_t terrain_query_{};
    std::vector<std::uint32_t> candidate_marks_;
    std::uint32_t candidate_query_{};
    std::vector<std::size_t> draw_order_;
    // Links use dense indices and are repaired when erase moves the last slot.
    static constexpr std::size_t no_link=static_cast<std::size_t>(-1);
    std::vector<std::size_t> previous_,next_;
    std::size_t first_{no_link},last_{no_link};
    bool draw_dirty_{true};
    void index_terrain(const ScWorld&);
    void erase(std::size_t index) noexcept;
    void validate_batch(std::span<const ScProjectileSpec>) const;
    std::uint64_t commit(std::span<const ScProjectileSpec>) noexcept;
    void index_cells() noexcept;
    std::vector<Cell>::const_iterator target_cell(int x,int y) const noexcept;
};
