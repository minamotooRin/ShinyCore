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
std::size_t cell_hash(int x,int y) noexcept {
    return ((static_cast<std::uint32_t>(x)*0x9e3779b9u)^(static_cast<std::uint32_t>(y)*0x85ebca6bu))&8191u;
}
float circle_hit(float x,float y,float dx,float dy,float cx,float cy,float r) {
    double ox=double(x)-cx,oy=double(y)-cy,c=ox*ox+oy*oy-double(r)*r;
    if(c<=0) return 0;
    double a=double(dx)*dx+double(dy)*dy,b=ox*dx+oy*dy,d=b*b-a*c;
    if(a==0||d<0) return 2;
    double t=(-b-std::sqrt(d))/a;
    return t>=0&&t<=1?static_cast<float>(t):2;
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
float polygon_hit(float x,float y,float dx,float dy,float r,const ScTerrainShape& shape) {
    float best=2;
    bool positive=false,negative=false;
    for(int i=0;i<shape.vertex_count;++i) {
        int j=(i+1)%shape.vertex_count;
        float ax=shape.x+shape.vertices[2*i],ay=shape.y+shape.vertices[2*i+1];
        float bx=shape.x+shape.vertices[2*j],by=shape.y+shape.vertices[2*j+1];
        double ex=double(bx)-ax,ey=double(by)-ay,length=std::hypot(ex,ey);
        if(length==0) continue;
        double px=double(x)-ax,py=double(y)-ay,cross=ex*py-ey*px;
        positive|=cross>0; negative|=cross<0;
        double along=(px*ex+py*ey)/length,normal=cross/length;
        if(along>=0&&along<=length&&std::fabs(normal)<=r) return 0;
        double normal_speed=(ex*dy-ey*dx)/length;
        if(normal_speed!=0) for(double side:{-double(r),double(r)}) {
            double t=(side-normal)/normal_speed;
            double projected=along+t*(double(dx)*ex+double(dy)*ey)/length;
            if(t>=0&&t<=1&&projected>=0&&projected<=length) best=std::min(best,static_cast<float>(t));
        }
        best=std::min(best,circle_hit(x,y,dx,dy,ax,ay,r));
    }
    return positive&&negative?best:0; // Initial center inside the convex polygon.
}
}
ScProjectiles::ScProjectiles(std::size_t capacity) {
    if(capacity==0||capacity>65536) throw std::invalid_argument("projectile capacity must be 1..65536");
    for(auto* v:{&x,&y,&vx,&vy,&ax,&ay,&radius,&life}) v->resize(capacity);
    mask.resize(capacity); color.resize(capacity); ids.resize(capacity); flags.resize(capacity); sprite.resize(capacity);
    draw_order_.reserve(capacity);
    previous_.resize(capacity); next_.resize(capacity);
    targets_.reserve(65536);
    cell_lookup_.resize(8192); terrain_marks_.resize(SC_MAX_TILES);
    hits.reserve(capacity*4); cells_.reserve(262144); terrain_cells_.reserve(262144); candidate_marks_.resize(65536);
}
void ScProjectiles::validate_batch(std::span<const ScProjectileSpec> batch) const {
    if(batch.size()>x.size()-count) throw std::runtime_error("projectile capacity exhausted (used="+std::to_string(count)+
        ", requested="+std::to_string(batch.size())+", capacity="+std::to_string(x.size())+")");
    if(next_id+batch.size()>SC_ID_MAX) throw std::runtime_error("projectile IDs exhausted");
    for(const auto& p:batch) if(!valid(p)||p.sprite>sprite_count) throw std::invalid_argument("invalid projectile geometry, motion, lifetime or sprite");
}
std::vector<std::uint64_t> ScProjectiles::spawn(std::span<const ScProjectileSpec> batch) {
    validate_batch(batch);
    std::vector<std::uint64_t> result; result.reserve(batch.size());
    for(std::size_t i=0;i<batch.size();++i) result.push_back(next_id+i);
    commit(batch); return result;
}
std::uint64_t ScProjectiles::spawn_batch(std::span<const ScProjectileSpec> batch) {
    validate_batch(batch); return commit(batch);
}
std::uint64_t ScProjectiles::commit(std::span<const ScProjectileSpec> batch) noexcept {
    const auto first=next_id;
    for(const auto& p:batch) {
        auto i=count++; x[i]=p.x; y[i]=p.y; vx[i]=p.vx; vy[i]=p.vy; ax[i]=p.ax; ay[i]=p.ay;
        radius[i]=p.radius; life[i]=p.life; mask[i]=p.mask; color[i]=p.color;
        flags[i]=static_cast<std::uint8_t>((p.terrain?1:0)|(p.piercing?2:0));
        sprite[i]=p.sprite; ids[i]=next_id++;
        if(!previous_display.empty()) previous_display[i]={};
        previous_[i]=last_; next_[i]=no_link;
        if(last_!=no_link) next_[last_]=i; else first_=i;
        last_=i;
    }
    draw_dirty_=true;
    return first;
}
void ScProjectiles::erase(std::size_t i) noexcept {
    if(previous_[i]!=no_link) next_[previous_[i]]=next_[i]; else first_=next_[i];
    if(next_[i]!=no_link) previous_[next_[i]]=previous_[i]; else last_=previous_[i];
    --count;
    if(!previous_display.empty()) previous_display[i]=previous_display[count];
    for(auto* v:{&x,&y,&vx,&vy,&ax,&ay,&radius,&life}) (*v)[i]=(*v)[count];
    mask[i]=mask[count]; color[i]=color[count]; ids[i]=ids[count]; flags[i]=flags[count]; sprite[i]=sprite[count]; draw_dirty_=true;
    if(i!=count) {
        previous_[i]=previous_[count]; next_[i]=next_[count];
        if(previous_[i]!=no_link) next_[previous_[i]]=i; else first_=i;
        if(next_[i]!=no_link) previous_[next_[i]]=i; else last_=i;
    }
}
void ScProjectiles::clear() noexcept { count=0; hits.clear(); first_=last_=no_link; draw_dirty_=true; }
std::uint8_t ScProjectiles::add_sprite(const ScProjectileSprite& s) {
    if(sprite_count==sprites.size()) throw std::runtime_error("projectile sprite capacity exhausted (64)");
    if(s.resource>=128||s.x<0||s.y<0||s.w<1||s.h<1||s.w>8192||s.h>8192||s.x>8192-s.w||s.y>8192-s.h||
       !std::isfinite(s.width)||!std::isfinite(s.height)||s.width<=0||s.height<=0||s.width>4096||s.height>4096)
        throw std::invalid_argument("invalid projectile sprite bounds");
    sprites[sprite_count++]=s; return static_cast<std::uint8_t>(sprite_count);
}
std::span<const std::size_t> ScProjectiles::draw_order() {
    if(draw_dirty_) {
        draw_order_.clear();
        for(auto i=first_;i!=no_link;i=next_[i]) draw_order_.push_back(i);
        draw_dirty_=false;
    }
    return draw_order_;
}
void ScProjectiles::index_terrain(const ScWorld& world) {
    if(terrain_world_==&world&&terrain_epoch_==world.epoch&&terrain_revision_==world.terrain_revision) return;
    terrain_world_=nullptr;
    terrain_cells_.clear();
    if(world.terrain_shapes.size()>SC_MAX_TILES) throw std::runtime_error("projectile terrain shape capacity exhausted");
    for(std::size_t i=0;i<world.terrain_shapes.size();++i) {
        const auto& shape=world.terrain_shapes[i];
        float left=shape.x,right=shape.x+shape.w,top=shape.y,bottom=shape.y+(shape.one_way?1:shape.h);
        if(shape.vertex_count) {
            if(shape.vertex_count<3||shape.vertex_count>8) throw std::runtime_error("invalid projectile terrain polygon");
            left=right=shape.x+shape.vertices[0]; top=bottom=shape.y+shape.vertices[1];
            for(int k=1;k<shape.vertex_count;++k) {
                const float px=shape.x+shape.vertices[2*k],py=shape.y+shape.vertices[2*k+1];
                if(!std::isfinite(px)||!std::isfinite(py)) throw std::runtime_error("nonfinite projectile terrain");
                left=std::min(left,px); right=std::max(right,px); top=std::min(top,py); bottom=std::max(bottom,py);
            }
        }
        for(float v:{left,right,top,bottom}) if(!std::isfinite(v)||std::fabs(v)>1e7f)
            throw std::runtime_error("projectile terrain bounds outside -1e7..1e7");
        for(int cy=cell(top);cy<=cell(bottom);++cy) for(int cx=cell(left);cx<=cell(right);++cx) {
            if(terrain_cells_.size()==262144) throw std::runtime_error("projectile terrain grid capacity exhausted (262144 entries)");
            terrain_cells_.push_back({cx,cy,i});
        }
    }
    std::sort(terrain_cells_.begin(),terrain_cells_.end(),[](const Cell& a,const Cell& b){return std::tie(a.x,a.y,a.entity)<std::tie(b.x,b.y,b.entity);});
    terrain_world_=&world; terrain_epoch_=world.epoch; terrain_revision_=world.terrain_revision;
}
void ScProjectiles::index_cells() noexcept {
    std::fill(cell_lookup_.begin(),cell_lookup_.end(),UINT32_MAX); cell_lookup_ready_=false;
    std::size_t groups=0;
    for(std::size_t i=0;i<cells_.size();) {
        // Keep probing bounded by a <= 50% load. Large sparse worlds use the
        // original sorted grid; this cache does not reduce the grid budget.
        if(++groups>cell_lookup_.size()/2) return;
        const auto& key=cells_[i]; auto slot=cell_hash(key.x,key.y);
        while(cell_lookup_[slot]!=UINT32_MAX) slot=(slot+1)&(cell_lookup_.size()-1);
        cell_lookup_[slot]=static_cast<std::uint32_t>(i);
        do { ++i; } while(i<cells_.size()&&cells_[i].x==key.x&&cells_[i].y==key.y);
    }
    cell_lookup_ready_=true;
}
std::vector<ScProjectiles::Cell>::const_iterator ScProjectiles::target_cell(int x,int y) const noexcept {
    if(cell_lookup_ready_) {
        auto slot=cell_hash(x,y);
        while(cell_lookup_[slot]!=UINT32_MAX) {
            const auto index=cell_lookup_[slot]; const auto& key=cells_[index];
            if(key.x==x&&key.y==y) return cells_.begin()+index;
            slot=(slot+1)&(cell_lookup_.size()-1);
        }
        return cells_.end();
    }
    return std::lower_bound(cells_.begin(),cells_.end(),Cell{x,y,0},[](const Cell& a,const Cell& b) {
        return std::tie(a.x,a.y,a.entity)<std::tie(b.x,b.y,b.entity);
    });
}
void ScProjectiles::step(const ScWorld& world) {
    cells_.clear(); targets_.clear(); hits.clear();
    if(count==0) return;
    if(world.entities.size()>candidate_marks_.size()) throw std::runtime_error("projectile target capacity exceeds 65536");
    bool terrain_needed=false;
    for(std::size_t i=0;i<count;++i) terrain_needed|=(flags[i]&1)!=0;
    if(terrain_needed&&world.map.tile_size<=0) throw std::runtime_error("projectile terrain requires positive tile size");
    if(terrain_needed) index_terrain(world);
    for(std::size_t i=0;i<world.entities.size();++i) {
        const auto& e=world.entities[i]; if(!e.alive||!e.solid) continue;
        const auto target=targets_.size();
        targets_.push_back({e.x,e.y,e.w,e.h,e.category,e.id});
        int left=cell(e.x),right=cell(e.x+e.w),top=cell(e.y),bottom=cell(e.y+e.h);
        for(int cy=top;cy<=bottom;++cy) for(int cx=left;cx<=right;++cx) {
            if(cells_.size()==cells_.capacity()) throw std::runtime_error("projectile spatial grid capacity exhausted");
            cells_.push_back({cx,cy,target});
        }
    }
    auto less=[](const Cell& a,const Cell& b) { return std::tie(a.x,a.y,a.entity)<std::tie(b.x,b.y,b.entity); };
    std::sort(cells_.begin(),cells_.end(),less);
    index_cells();
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
        float wall=2;
        if(flags[i]&1) {
            const float tile=static_cast<float>(world.map.tile_size);
            int tx0=static_cast<int>(std::floor(left/tile)),tx1=static_cast<int>(std::floor(right/tile));
            int ty0=static_cast<int>(std::floor(top/tile)),ty1=static_cast<int>(std::floor(bottom/tile));
            if(static_cast<std::int64_t>(tx1-tx0+1)*(ty1-ty0+1)>16384) throw std::runtime_error("projectile terrain sweep budget exhausted");
            for(int ty=ty0;ty<=ty1;++ty) for(int tx=tx0;tx<=tx1;++tx) {
                auto kind=sc_tile(&world,tx,ty);
                if(kind!='.') wall=std::min(wall,sweep(x[i],y[i],dx,dy,r,static_cast<float>(tx)*tile,static_cast<float>(ty)*tile,tile,kind=='='?1:tile));
            }
            // Terrain uses a revision-cached grid, not a per-projectile full-map scan.
            if(++terrain_query_==0) { std::fill(terrain_marks_.begin(),terrain_marks_.end(),0); ++terrain_query_; }
            for(int cy=y0;cy<=y1;++cy) for(int cx=x0;cx<=x1;++cx) {
                auto it=std::lower_bound(terrain_cells_.begin(),terrain_cells_.end(),Cell{cx,cy,0},less);
                for(;it!=terrain_cells_.end()&&it->x==cx&&it->y==cy;++it) {
                    if(terrain_marks_[it->entity]==terrain_query_) continue;
                    terrain_marks_[it->entity]=terrain_query_;
                    const auto& shape=world.terrain_shapes[it->entity];
                    float t=shape.vertex_count?polygon_hit(x[i],y[i],dx,dy,r,shape):
                        sweep(x[i],y[i],dx,dy,r,shape.x,shape.y,shape.w,shape.one_way?1:shape.h);
                    wall=std::min(wall,t);
                }
            }
        }
        std::size_t first=hits.size();
        auto append=[&](ScEntityId target,float t) {
            if(hits.size()==hits.capacity()) throw std::runtime_error("projectile hit capacity exhausted");
            hits.push_back({ids[i],target,t,x[i]+t*dx,y[i]+t*dy});
        };
        const bool piercing=(flags[i]&2)!=0;
        float nearest=wall; ScEntityId nearest_target=0;
        if(piercing&&wall<=1) append(0,wall);
        if(++candidate_query_==0) { std::fill(candidate_marks_.begin(),candidate_marks_.end(),0); ++candidate_query_; }
        for(int cy=y0;cy<=y1;++cy) for(int cx=x0;cx<=x1;++cx) {
            auto it=target_cell(cx,cy);
            for(;it!=cells_.end()&&it->x==cx&&it->y==cy;++it) {
                if(candidate_marks_[it->entity]==candidate_query_) continue;
                candidate_marks_[it->entity]=candidate_query_;
                const auto& e=targets_[it->entity]; if(!(mask[i]&e.category)) continue;
                if(e.x>right||e.y>bottom||e.x+e.w<left||e.y+e.h<top) continue;
                const float t=sweep(x[i],y[i],dx,dy,r,e.x,e.y,e.w,e.h);
                if(t>1||t>=wall) continue;
                if(piercing) append(e.id,t);
                else if(t<nearest||(t==nearest&&e.id<nearest_target)) { nearest=t; nearest_target=e.id; }
            }
        }
        if(!piercing&&nearest<=1) append(nearest_target,nearest);
        bool remove=hits.size()>first&&(!piercing||wall<=1);
        if(remove) erase(i); else { x[i]+=dx; y[i]+=dy; ++i; }
    }
    std::sort(hits.begin(),hits.end(),[](const auto& a,const auto& b) {
        return std::tie(a.projectile,a.fraction,a.target)<std::tie(b.projectile,b.fraction,b.target);
    });
    total_hits+=hits.size();
}
