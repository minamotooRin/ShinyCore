#pragma once
#include "shiny/core.h"
#include <span>

struct ScProjectileSpec {
    float x{}, y{}, vx{}, vy{}, ax{}, ay{}, radius{2}, life{3};
    std::uint32_t mask{0xffffffff}, color{0xffffffff};
    bool terrain{true}, piercing{};
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
    std::vector<std::uint32_t> mask,color;
    std::vector<std::uint64_t> ids;
    std::vector<std::uint8_t> flags;
    std::vector<ScProjectileHit> hits;
    std::size_t count{};
    std::uint64_t next_id{1}, total_hits{};
    std::vector<std::uint64_t> spawn(std::span<const ScProjectileSpec> batch);
    void step(const ScWorld& world);
    void clear() noexcept;
private:
    struct Cell { int x,y; std::size_t entity; };
    std::vector<Cell> cells_;
    std::vector<std::size_t> candidates_;
    void erase(std::size_t index) noexcept;
};
