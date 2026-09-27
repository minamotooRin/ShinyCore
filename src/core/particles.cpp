#include "shiny/particles.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace {
constexpr float bound=1e6f;
float random(std::uint32_t& rng) noexcept {
    auto n=rng; n^=n<<13; n^=n>>17; n^=n<<5; rng=n;
    return static_cast<float>(n>>8)*(1.0f/16777216.0f);
}
float bounded(double value) noexcept { return static_cast<float>(std::clamp(value,-double(bound),double(bound))); }
ScParticleKey sample(const ScParticleEmitter& emitter,float age) noexcept {
    std::size_t i=1;
    while(i+1<emitter.keys&&age>emitter.curve[i].time) ++i;
    const auto& a=emitter.curve[i-1]; const auto& b=emitter.curve[i];
    const float t=std::clamp((age-a.time)/(b.time-a.time),0.0f,1.0f);
    std::uint32_t rgba=0;
    for(int shift:{24,16,8,0}) {
        const auto from=static_cast<float>((a.color>>shift)&255),to=static_cast<float>((b.color>>shift)&255);
        rgba|=static_cast<std::uint32_t>(std::lround(from+(to-from)*t))<<shift;
    }
    return {age,a.size+(b.size-a.size)*t,rgba};
}
}
ScParticles::ScParticles(std::size_t capacity) {
    if(capacity>65536) throw std::invalid_argument("particle capacity outside 0..65536");
    for(auto* column:{&x,&y,&vx,&vy,&life,&max_life,&size}) column->resize(capacity);
    color.resize(capacity);
    emitter.resize(capacity);
}
void ScParticles::configure(std::size_t capacity) {
    if(count||!emitters_.empty()) throw std::logic_error("configure particle capacity before emission or emitter definition");
    *this=ScParticles(capacity); // Strong failure guarantee; zero releases all columns.
}
std::expected<std::size_t,const char*> ScParticles::emit(std::uint32_t& rng,float px,float py,
    std::size_t amount,std::uint32_t rgba,float speed,float duration) {
    if(!std::isfinite(px)||!std::isfinite(py)||std::fabs(px)>bound||std::fabs(py)>bound||
       !std::isfinite(speed)||speed<0||speed>bound||!std::isfinite(duration)||duration<=0||duration>bound)
        return std::unexpected("invalid particle position, speed or lifetime");
    if(amount>capacity()-count) return std::unexpected("particle capacity exhausted");
    for(std::size_t n=0;n<amount;++n) {
        const auto i=count++;
        if(!previous_display.empty()) previous_display[i]={};
        const float angle=random(rng)*(2.0f*std::numbers::pi_v<float>);
        const float velocity=speed*(.35f+.65f*random(rng));
        x[i]=px; y[i]=py; vx[i]=std::cos(angle)*velocity; vy[i]=std::sin(angle)*velocity;
        life[i]=max_life[i]=duration*(.6f+.4f*random(rng));
        size[i]=1+std::floor(random(rng)*3); color[i]=rgba; emitter[i]=0;
    }
    return amount;
}
void ScParticles::step(float gravity,float dt) noexcept {
    if(!std::isfinite(dt)||dt<=0) return;
    const double acceleration=std::isfinite(gravity)?double(gravity):0;
    std::size_t out=0;
    for(std::size_t i=0;i<count;++i) {
        if(!std::isfinite(life[i])||!std::isfinite(x[i])||!std::isfinite(y[i])||
           !std::isfinite(vx[i])||!std::isfinite(vy[i])||life[i]<=dt) continue;
        // Stable compaction keeps translucent overlap order without a sort.
        const float remaining=life[i]-dt;
        const auto style=emitter[i];
        const float factor=style?emitters_[style-1].gravity:.15f;
        const float velocity=bounded(double(vy[i])+acceleration*factor*dt);
        x[out]=bounded(double(x[i])+double(vx[i])*dt);
        y[out]=bounded(double(y[i])+double(velocity)*dt);
        vx[out]=vx[i]; vy[out]=velocity; life[out]=remaining;
        max_life[out]=max_life[i]; size[out]=size[i]; color[out]=color[i];
        emitter[out]=style;
        if(!previous_display.empty()) previous_display[out]=previous_display[i];
        if(style) {
            const auto value=sample(emitters_[style-1],1-remaining/max_life[out]);
            size[out]=value.size; color[out]=value.color;
        }
        ++out;
    }
    count=out;
}
std::expected<std::uint8_t,const char*> ScParticles::define(const ScParticleEmitter& spec) {
    auto valid=[](float n,float lo,float hi) { return std::isfinite(n)&&n>=lo&&n<=hi; };
    if(!capacity()) return std::unexpected("particles disabled by project capacity");
    if(emitters_.size()==64) return std::unexpected("particle emitter capacity exhausted (64)");
    if(spec.image>128||(spec.image<128&&(spec.x<0||spec.y<0||spec.w<1||spec.h<1||spec.w>8192||spec.h>8192||
        spec.x>8192-spec.w||spec.y>8192-spec.h)))
        return std::unexpected("invalid particle texture region");
    if(!valid(spec.speed_min,0,bound)||!valid(spec.speed_max,spec.speed_min,bound)||
       !valid(spec.life_min,.001f,60)||!valid(spec.life_max,spec.life_min,60)||
       !valid(spec.angle_min,-bound,bound)||!valid(spec.angle_max,spec.angle_min,bound)||
       !valid(spec.gravity,-10,10)||spec.keys<2||spec.keys>8)
        return std::unexpected("invalid particle emitter ranges or curve length");
    for(std::size_t i=0;i<spec.keys;++i) {
        const auto& key=spec.curve[i];
        if(!valid(key.time,0,1)||!valid(key.size,0,4096)||(i&&key.time<=spec.curve[i-1].time))
            return std::unexpected("particle curve requires increasing times and bounded sizes");
    }
    if(spec.curve[0].time!=0||spec.curve[spec.keys-1].time!=1)
        return std::unexpected("particle curve must start at 0 and end at 1");
    if(emitters_.empty()) emitters_.reserve(64);
    emitters_.push_back(spec); return static_cast<std::uint8_t>(emitters_.size());
}
std::expected<std::size_t,const char*> ScParticles::burst(std::uint32_t& rng,std::uint8_t id,
    float px,float py,std::size_t amount) {
    if(!id||id>emitters_.size()) return std::unexpected("invalid particle emitter");
    if(!std::isfinite(px)||!std::isfinite(py)||std::fabs(px)>bound||std::fabs(py)>bound)
        return std::unexpected("invalid particle burst position");
    if(amount>capacity()-count) return std::unexpected("particle capacity exhausted");
    const auto& spec=emitters_[id-1];
    for(std::size_t n=0;n<amount;++n) {
        const auto i=count++;
        if(!previous_display.empty()) previous_display[i]={};
        const float angle=spec.angle_min+(spec.angle_max-spec.angle_min)*random(rng);
        const float velocity=spec.speed_min+(spec.speed_max-spec.speed_min)*random(rng);
        x[i]=px; y[i]=py; vx[i]=std::cos(angle)*velocity; vy[i]=std::sin(angle)*velocity;
        life[i]=max_life[i]=spec.life_min+(spec.life_max-spec.life_min)*random(rng);
        size[i]=spec.curve[0].size; color[i]=spec.curve[0].color; emitter[i]=id;
    }
    return amount;
}
