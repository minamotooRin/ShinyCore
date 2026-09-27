#pragma once
#include "shiny/presentation.h"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>
#include <array>
#include <span>

struct ScParticleKey { float time{},size{2}; std::uint32_t color{0xffffffff}; };
struct ScParticleEmitter {
    float speed_min{30},speed_max{30},life_min{1},life_max{1};
    float angle_min{},angle_max{6.283185307f},gravity{.15f};
    std::array<ScParticleKey,8> curve{{{0,2,0xffffffff},{1,0,0xffffff00}}};
    std::size_t keys{2};
    // 128 means untextured; image resource indices are 0..127.
    std::uint16_t image{128};
    int x{},y{},w{},h{};
    bool additive{};
};

// Room-owned columns. Live particles occupy [0,count), in creation order.
// No handles or per-particle callbacks; configure only while loading the room.
class ScParticles final {
public:
    explicit ScParticles(std::size_t capacity=32768);
    std::vector<float> x,y,vx,vy,life,max_life,size;
    std::vector<ScDisplayPoint> previous_display; // Empty unless interpolation was reserved during init.
    std::vector<std::uint32_t> color;
    std::vector<std::uint8_t> emitter;
    std::size_t count{};
    std::size_t capacity() const noexcept { return x.size(); }
    void configure(std::size_t capacity);
    std::expected<std::size_t,const char*> emit(std::uint32_t& rng,float px,float py,
        std::size_t amount,std::uint32_t rgba,float speed,float duration);
    void step(float gravity,float dt) noexcept;
    std::expected<std::uint8_t,const char*> define(const ScParticleEmitter&);
    std::expected<std::size_t,const char*> burst(std::uint32_t& rng,std::uint8_t id,
        float px,float py,std::size_t amount);
    std::size_t emitter_count() const noexcept { return emitters_.size(); }
    std::span<const ScParticleEmitter> definitions() const noexcept { return emitters_; }
private:
    std::vector<ScParticleEmitter> emitters_; // Reserved only when templates are enabled during init.
};
