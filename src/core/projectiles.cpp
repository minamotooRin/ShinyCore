#include "shiny/projectiles.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

void ScProjectilesDeleter::operator()(ScProjectiles* p) const noexcept { delete p; }

namespace {
constexpr float cell_size=64;
int cell(float x) { return static_cast<int>(std::floor(x/cell_size)); }
float circle_hit(float x,float y,float dx,float dy,float cx,float cy,float r) {
    float ox=x-cx,oy=y-cy,c=ox*ox+oy*oy-r*r;
    if(c<=0) return 0;
    float a=dx*dx+dy*dy,b=ox*dx+oy*dy,d=b*b-a*c;
    if(a==0||d<0) return 2;
    float t=(-b-std::sqrt(d))/a;
    return t>=0&&t<=1?t:2;
}
// Swept circle versus an axis-aligned rectangle, including rounded corners.
float sweep(float x,float y,float dx,float dy,float r,float left,float top,float w,float h) {
    float right=left+w,bottom=top+h;
    float qx=std::clamp(x,left,right),qy=std::clamp(y,top,bottom);
    if((x-qx)*(x-qx)+(y-qy)*(y-qy)<=r*r) return 0;
    float best=2;
    auto side=[&](float t,float coordinate,float lo,float hi) {
        if(t>=0&&t<=1&&coordinate>=lo&&coordinate<=hi) best=std::min(best,t);
    };
    if(dx!=0) {
        float t=(left-r-x)/dx; side(t,y+t*dy,top,bottom);
        t=(right+r-x)/dx; side(t,y+t*dy,top,bottom);
    }
    if(dy!=0) {
        float t=(top-r-y)/dy; side(t,x+t*dx,left,right);
        t=(bottom+r-y)/dy; side(t,x+t*dx,left,right);
    }
    for(float cx:{left,right}) for(float cy:{top,bottom})
        best=std::min(best,circle_hit(x,y,dx,dy,cx,cy,r));
    return best;
}
bool valid(const ScProjectileSpec& p) {
    for(float v:{p.x,p.y,p.vx,p.vy,p.ax,p.ay}) if(!std::isfinite(v)||std::fabs(v)>1e6f) return false;
    return std::isfinite(p.radius)&&p.radius>0&&p.radius<=256&&std::isfinite(p.life)&&p.life>0&&p.life<=3600;
}
}
ScProjectiles::ScProjectiles(std::size_t capacity) {
    if(capacity==0||capacity>65536) throw std::invalid_argument("projectile capacity must be 1..65536");
    for(auto* v:{&x,&y,&vx,&vy,&ax,&ay,&radius,&life}) v->resize(capacity);
    mask.resize(capacity); color.resize(capacity); ids.resize(capacity); flags.resize(capacity);
    hits.reserve(capacity*4); cells_.reserve(262144); candidates_.reserve(65536);
}
std::vector<std::uint64_t> ScProjectiles::spawn(std::span<const ScProjectileSpec> batch) {
    if(batch.size()>x.size()-count) throw std::runtime_error("projectile capacity exhausted");
    if(next_id+batch.size()>SC_ID_MAX) throw std::runtime_error("projectile IDs exhausted");
    for(const auto& p:batch) if(!valid(p)) throw std::invalid_argument("invalid projectile geometry, motion or lifetime");
    std::vector<std::uint64_t> result; result.reserve(batch.size());
    for(const auto& p:batch) {
        auto i=count++; x[i]=p.x; y[i]=p.y; vx[i]=p.vx; vy[i]=p.vy; ax[i]=p.ax; ay[i]=p.ay;
        radius[i]=p.radius; life[i]=p.life; mask[i]=p.mask; color[i]=p.color;
        flags[i]=static_cast<std::uint8_t>((p.terrain?1:0)|(p.piercing?2:0));
        ids[i]=next_id++; result.push_back(ids[i]);
    }
    return result;
}
void ScProjectiles::erase(std::size_t i) noexcept {
    --count;
    for(auto* v:{&x,&y,&vx,&vy,&ax,&ay,&radius,&life}) (*v)[i]=(*v)[count];
    mask[i]=mask[count]; color[i]=color[count]; ids[i]=ids[count]; flags[i]=flags[count];
}
void ScProjectiles::clear() noexcept { count=0; hits.clear(); }
void ScProjectiles::step(const ScWorld& world) {
    cells_.clear(); hits.clear();
    for(std::size_t i=0;i<world.entities.size();++i) {
        const auto& e=world.entities[i]; if(!e.alive||!e.solid) continue;
        int left=cell(e.x),right=cell(e.x+e.w),top=cell(e.y),bottom=cell(e.y+e.h);
        for(int cy=top;cy<=bottom;++cy) for(int cx=left;cx<=right;++cx) {
            if(cells_.size()==cells_.capacity()) throw std::runtime_error("projectile spatial grid capacity exhausted");
            cells_.push_back({cx,cy,i});
        }
    }
    auto less=[](const Cell& a,const Cell& b) { return std::tie(a.x,a.y,a.entity)<std::tie(b.x,b.y,b.entity); };
    std::sort(cells_.begin(),cells_.end(),less);
    for(std::size_t i=0;i<count;) {
        life[i]-=SC_DT; if(life[i]<=0) { erase(i); continue; }
        vx[i]+=ax[i]*SC_DT; vy[i]+=ay[i]*SC_DT;
        float dx=vx[i]*SC_DT,dy=vy[i]*SC_DT,r=radius[i];
        float left=std::min(x[i],x[i]+dx)-r,right=std::max(x[i],x[i]+dx)+r;
        float top=std::min(y[i],y[i]+dy)-r,bottom=std::max(y[i],y[i]+dy)+r;
        if(!std::isfinite(right)||std::max({std::fabs(left),std::fabs(right),std::fabs(top),std::fabs(bottom)})>1e6f)
            throw std::runtime_error("projectile coordinate limit exceeded");
        int x0=cell(left),x1=cell(right),y0=cell(top),y1=cell(bottom);
        if(static_cast<std::int64_t>(x1-x0+1)*(y1-y0+1)>4096) throw std::runtime_error("projectile sweep budget exhausted");
        candidates_.clear();
        for(int cy=y0;cy<=y1;++cy) for(int cx=x0;cx<=x1;++cx) {
            auto it=std::lower_bound(cells_.begin(),cells_.end(),Cell{cx,cy,0},less);
            for(;it!=cells_.end()&&it->x==cx&&it->y==cy;++it) {
                if(candidates_.size()==candidates_.capacity()) throw std::runtime_error("projectile candidate budget exhausted");
                candidates_.push_back(it->entity);
            }
        }
        std::sort(candidates_.begin(),candidates_.end());
        candidates_.erase(std::unique(candidates_.begin(),candidates_.end()),candidates_.end());
        float wall=2;
        if(flags[i]&1) {
            const float tile=static_cast<float>(world.map.tile_size);
            int tx0=static_cast<int>(std::floor(left/tile)),tx1=static_cast<int>(std::floor(right/tile));
            int ty0=static_cast<int>(std::floor(top/tile)),ty1=static_cast<int>(std::floor(bottom/tile));
            if(static_cast<std::int64_t>(tx1-tx0+1)*(ty1-ty0+1)>16384) throw std::runtime_error("projectile terrain sweep budget exhausted");
            for(int ty=ty0;ty<=ty1;++ty) for(int tx=tx0;tx<=tx1;++tx)
                if(sc_tile(&world,tx,ty)!='.') wall=std::min(wall,sweep(x[i],y[i],dx,dy,r,static_cast<float>(tx)*tile,static_cast<float>(ty)*tile,tile,tile));
        }
        std::size_t first=hits.size();
        auto append=[&](ScEntityId target,float t) {
            if(hits.size()==hits.capacity()) throw std::runtime_error("projectile hit capacity exhausted");
            hits.push_back({ids[i],target,t,x[i]+t*dx,y[i]+t*dy});
        };
        if(wall<=1) append(0,wall);
        for(auto index:candidates_) {
            const auto& e=world.entities[index]; if(!(mask[i]&e.category)) continue;
            float t=sweep(x[i],y[i],dx,dy,r,e.x,e.y,e.w,e.h);
            if(t<=1&&t<wall) append(e.id,t);
        }
        std::sort(hits.begin()+static_cast<std::ptrdiff_t>(first),hits.end(),[](const auto& a,const auto& b) {
            return std::tie(a.fraction,a.target)<std::tie(b.fraction,b.target);
        });
        if(hits.size()>first&&!(flags[i]&2)) hits.resize(first+1);
        bool remove=hits.size()>first&&(!(flags[i]&2)||wall<=1);
        if(remove) erase(i); else { x[i]+=dx; y[i]+=dy; ++i; }
    }
    std::sort(hits.begin(),hits.end(),[](const auto& a,const auto& b) {
        return std::tie(a.projectile,a.fraction,a.target)<std::tie(b.projectile,b.fraction,b.target);
    });
    total_hits+=hits.size();
}
