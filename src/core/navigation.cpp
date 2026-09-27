#include "shiny/navigation.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace {
bool walkable(const ScMap& map,int i) {
    return i>=0&&i<map.width*map.height&&map.tiles[static_cast<std::size_t>(i)]=='.'&&!map.navigation_blocked[static_cast<std::size_t>(i)];
}
std::array<int,4> neighbors(const ScMap& map,int i) {
    return {i%map.width>0?i-1:-1,i>=map.width?i-map.width:-1,i%map.width+1<map.width?i+1:-1,i/map.width+1<map.height?i+map.width:-1};
}
void validate(const ScMap& map,int goal,std::size_t budget) {
    if(map.width<=0||map.height<=0||map.tile_size<=0||map.width>SC_MAX_TILES/map.height||goal<0||goal>=map.width*map.height||budget==0||budget>1048576)
        throw std::invalid_argument("navigation requires valid dimensions, an in-bounds goal and budget 1..1048576");
}
void validate_radius(float radius) {
    if(!std::isfinite(radius)||radius<0||radius>4096)
        throw std::invalid_argument("navigation radius outside 0..4096 pixels");
}
std::bitset<SC_MAX_TILES> clearance(const ScMap& map,float radius) {
    std::bitset<SC_MAX_TILES> blocked;
    for(int i=0;i<map.width*map.height;++i) if(!walkable(map,i)) blocked.set(static_cast<std::size_t>(i));
    const double r=double(radius)/map.tile_size;
    if(r<=.5) return blocked;
    // Distance to the closest blocked cell in each row; fixed scratch, no per-agent storage.
    std::array<std::uint16_t,SC_MAX_TILES> horizontal{};
    for(int y=0;y<map.height;++y) {
        int nearest=-SC_MAX_TILES;
        for(int x=0;x<map.width;++x) {
            const auto i=static_cast<std::size_t>(y*map.width+x);
            if(blocked[i]) nearest=x;
            horizontal[i]=static_cast<std::uint16_t>(std::min(x-nearest,SC_MAX_TILES));
        }
        nearest=map.width+SC_MAX_TILES;
        for(int x=map.width-1;x>=0;--x) {
            const auto i=static_cast<std::size_t>(y*map.width+x);
            if(blocked[i]) nearest=x;
            horizontal[i]=static_cast<std::uint16_t>(std::min(int(horizontal[i]),nearest-x));
        }
    }
    const int span=static_cast<int>(std::ceil(r+.5));
    for(int y=0;y<map.height;++y) for(int x=0;x<map.width;++x) {
        const auto i=static_cast<std::size_t>(y*map.width+x);
        if(blocked[i]) continue;
        if(std::min({x+.5,y+.5,map.width-x-.5,map.height-y-.5})<r) { blocked.set(i); continue; }
        for(int row=std::max(0,y-span);row<=std::min(map.height-1,y+span);++row) {
            const double dx=std::max(double(horizontal[static_cast<std::size_t>(row*map.width+x)])-.5,0.0);
            const double dy=std::max(double(std::abs(row-y))-.5,0.0);
            if(dx*dx+dy*dy<r*r) { blocked.set(i); break; }
        }
    }
    return blocked;
}
}
namespace {
std::bitset<SC_MAX_TILES> raster(const ScMap& map,std::span<const ScTerrainShape> shapes,float origin_x,float origin_y,
    const std::bitset<SC_MAX_TILES>* selected=nullptr) {
    if(!std::isfinite(origin_x)||!std::isfinite(origin_y)||map.width<=0||map.height<=0||map.width>SC_MAX_TILES/map.height||map.tile_size<=0)
        throw std::invalid_argument("invalid navigation map dimensions");
    std::bitset<SC_MAX_TILES> blocked;
    const double tile=map.tile_size;
    const bool validate_only=selected&&selected->none();
    for(const auto& shape:shapes) {
        const int count=shape.vertex_count?shape.vertex_count:4;
        if(count<3||count>8) throw std::invalid_argument("invalid navigation collision polygon");
        std::array<double,16> points{};
        if(shape.vertex_count) {
            for(int i=0;i<count;++i) {
                points[2*i]=double(shape.x)+shape.vertices[2*i];
                points[2*i+1]=double(shape.y)+shape.vertices[2*i+1];
            }
        } else {
            const double h=shape.one_way?1:shape.h;
            if(shape.w<=0||h<=0) throw std::invalid_argument("invalid navigation collision bounds");
            points={shape.x,shape.y,double(shape.x)+shape.w,shape.y,
                    double(shape.x)+shape.w,double(shape.y)+h,shape.x,double(shape.y)+h};
        }
        for(int i=0;i<count;++i) { points[2*i]-=origin_x; points[2*i+1]-=origin_y; }
        double left=points[0],right=left,top=points[1],bottom=top;
        for(int i=0;i<count;++i) {
            const double x=points[2*i],y=points[2*i+1];
            if(!std::isfinite(x)||!std::isfinite(y)) throw std::invalid_argument("nonfinite navigation collision point");
            left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);
        }
        if(validate_only) continue;
        // Clip before integer conversion. Boundary-only contact does not block adjacent cells.
        const int x0=int(std::clamp(std::floor(left/tile),0.0,double(map.width)));
        const int x1=int(std::clamp(std::ceil(right/tile),0.0,double(map.width)));
        const int y0=int(std::clamp(std::floor(top/tile),0.0,double(map.height)));
        const int y1=int(std::clamp(std::ceil(bottom/tile),0.0,double(map.height)));
        struct Axis { double x{},y{},low{},high{}; };
        std::array<Axis,8> axes{};
        if(shape.vertex_count) for(int i=0;i<count;++i) {
            auto& axis=axes[i]; const int next=(i+1)%count;
            axis.x=points[2*next+1]-points[2*i+1]; axis.y=points[2*i]-points[2*next];
            axis.low=axis.high=axis.x*points[0]+axis.y*points[1];
            for(int j=1;j<count;++j) {
                const double projection=axis.x*points[2*j]+axis.y*points[2*j+1];
                axis.low=std::min(axis.low,projection); axis.high=std::max(axis.high,projection);
            }
        }
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
            const auto index=static_cast<std::size_t>(y*map.width+x);
            if(blocked[index]||(selected&&!(*selected)[index])) continue;
            bool overlap=true;
            if(shape.vertex_count) for(int i=0;i<count;++i) {
                const auto& axis=axes[i];
                if(axis.x==0&&axis.y==0) continue;
                const double center=axis.x*(x+.5)*tile+axis.y*(y+.5)*tile;
                const double radius=(std::fabs(axis.x)+std::fabs(axis.y))*.5*tile;
                if(axis.high<=center-radius||axis.low>=center+radius) { overlap=false; break; }
            }
            if(overlap) blocked.set(index);
        }
    }
    return blocked;
}
// Compare only geometry used by rasterization; inactive vertex storage is irrelevant.
bool shape_less(const ScTerrainShape* a,const ScTerrainShape* b) {
    const auto key_a=std::tie(a->x,a->y,a->one_way,a->vertex_count);
    const auto key_b=std::tie(b->x,b->y,b->one_way,b->vertex_count);
    if(key_a!=key_b) return key_a<key_b;
    if(a->vertex_count) return std::lexicographical_compare(a->vertices.begin(),a->vertices.begin()+a->vertex_count*2,
        b->vertices.begin(),b->vertices.begin()+b->vertex_count*2);
    return std::pair{a->w,a->one_way?1.f:a->h}<std::pair{b->w,b->one_way?1.f:b->h};
}
}
std::bitset<SC_MAX_TILES> sc_navigation_obstacles(const ScMap& map,std::span<const ScTerrainShape> shapes,float x,float y) {
    return raster(map,shapes,x,y);
}
ScNavigationUpdate sc_navigation_patch(const ScMap& map,std::span<const ScTerrainShape> before,
    std::span<const ScTerrainShape> after,float x,float y) {
    // Validate before sorting floats. Preparation owns temporary arrays; no world state changes here.
    const std::bitset<SC_MAX_TILES> none;
    (void)raster(map,before,x,y,&none); (void)raster(map,after,x,y,&none);
    std::vector<const ScTerrainShape*> old_shapes,new_shapes;
    old_shapes.reserve(before.size()); new_shapes.reserve(after.size());
    for(const auto& shape:before) old_shapes.push_back(&shape);
    for(const auto& shape:after) new_shapes.push_back(&shape);
    std::sort(old_shapes.begin(),old_shapes.end(),shape_less); std::sort(new_shapes.begin(),new_shapes.end(),shape_less);
    ScNavigationUpdate result{map.navigation_blocked,{}};
    std::size_t a=0,b=0;
    while(a<old_shapes.size()||b<new_shapes.size()) {
        const ScTerrainShape* changed=nullptr;
        if(b==new_shapes.size()||(a<old_shapes.size()&&shape_less(old_shapes[a],new_shapes[b]))) changed=old_shapes[a++];
        else if(a==old_shapes.size()||shape_less(new_shapes[b],old_shapes[a])) changed=new_shapes[b++];
        else { ++a; ++b; continue; }
        result.dirty|=raster(map,std::span{changed,1},x,y);
    }
    if(result.dirty.any()) {
        // Re-evaluate all contributors in dirty cells: removing one overlapping shape must not open a wall.
        result.blocked=(map.navigation_blocked&~result.dirty)|raster(map,after,x,y,&result.dirty);
    }
    return result;
}
ScPath sc_path(const ScMap& map,int start,int goal,std::size_t budget,float radius) {
    validate(map,goal,budget);
    validate_radius(radius);
    const auto blocked=clearance(map,radius);
    if(start<0||start>=map.width*map.height||blocked[static_cast<std::size_t>(start)]||blocked[static_cast<std::size_t>(goal)])
        return {"unreachable",{},0};
    const auto count=static_cast<std::size_t>(map.width*map.height);
    std::vector<int> distance(count,INT_MAX),parent(count,-1);
    using Node=std::tuple<int,int,int>; // cost+heuristic, cost, cell: stable ties.
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> open;
    auto heuristic=[&](int cell) { return std::abs(cell%map.width-goal%map.width)+std::abs(cell/map.width-goal/map.width); };
    distance[static_cast<std::size_t>(start)]=0; open.emplace(heuristic(start),0,start);
    ScPath result{"unreachable",{},0};
    while(!open.empty()) {
        auto [estimate,cost,current]=open.top(); open.pop(); (void)estimate;
        if(cost!=distance[static_cast<std::size_t>(current)]) continue;
        if(result.visited==budget) { result.status="budget_exhausted"; return result; }
        ++result.visited;
        if(current==goal) {
            result.status="ok";
            for(int at=goal;at!=-1;at=parent[static_cast<std::size_t>(at)]) result.cells.push_back(at);
            std::reverse(result.cells.begin(),result.cells.end()); return result;
        }
        for(int next:neighbors(map,current)) if(next>=0&&!blocked[static_cast<std::size_t>(next)]&&cost+1<distance[static_cast<std::size_t>(next)]) {
            distance[static_cast<std::size_t>(next)]=cost+1; parent[static_cast<std::size_t>(next)]=current;
            open.emplace(cost+1+heuristic(next),cost+1,next);
        }
    }
    return result;
}
void ScFlowField::build(const ScMap& map,int goal,std::size_t budget,float radius) {
    validate(map,goal,budget); validate_radius(radius); width_=map.width; height_=map.height; radius_=radius;
    goal_=goal; status="stale"; refresh(map,budget);
}
void ScFlowField::refresh(const ScMap& map,std::size_t budget) {
    if(status=="unbuilt"||map.width!=width_||map.height!=height_||map.tile_size<=0||budget==0||budget>1048576)
        throw std::invalid_argument("flow refresh requires original map dimensions and budget 1..1048576");
    if(revision_!=map.navigation_revision) status="stale";
    if(status=="ok"||status=="unreachable") return;
    if(status=="stale") {
        distance_.assign(static_cast<std::size_t>(width_*height_),INT_MAX);
        queue_.clear(); queue_.reserve(distance_.size()); visited=0;
        revision_=map.navigation_revision;
        blocked_=clearance(map,radius_);
        if(blocked_[static_cast<std::size_t>(goal_)]) { status="unreachable"; return; }
        queue_.push_back(goal_); distance_[static_cast<std::size_t>(goal_)]=0;
    }
    status="budget_exhausted";
    const auto end=visited+budget;
    while(visited<queue_.size()) {
        if(visited==end) return;
        int current=queue_[visited++];
        for(int next:neighbors(map,current)) if(next>=0&&!blocked_[static_cast<std::size_t>(next)]&&distance_[static_cast<std::size_t>(next)]==INT_MAX) {
            distance_[static_cast<std::size_t>(next)]=distance_[static_cast<std::size_t>(current)]+1; queue_.push_back(next);
        }
    }
    status="ok";
}
std::pair<float,float> ScFlowField::direction(const ScMap& map,float x,float y) const {
    if(state(map)!="ok"||width_!=map.width||height_!=map.height||distance_.empty()||map.tile_size<=0||
       !std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=float(width_)*float(map.tile_size)||y>=float(height_)*float(map.tile_size)) return {};
    int cx=static_cast<int>(std::floor(x/static_cast<float>(map.tile_size))),cy=static_cast<int>(std::floor(y/static_cast<float>(map.tile_size)));
    if(cx<0||cy<0||cx>=width_||cy>=height_) return {};
    int current=cy*width_+cx,best=current;
    if(blocked_[static_cast<std::size_t>(current)]) return {};
    for(int next:neighbors(map,current)) if(next>=0&&!blocked_[static_cast<std::size_t>(next)]&&distance_[static_cast<std::size_t>(next)]<distance_[static_cast<std::size_t>(best)]) best=next;
    if(best==current) return {};
    float dx=(static_cast<float>(best%width_)+.5f)*static_cast<float>(map.tile_size)-x;
    float dy=(static_cast<float>(best/width_)+.5f)*static_cast<float>(map.tile_size)-y;
    float length=std::hypot(dx,dy); return length>0?std::pair{dx/length,dy/length}:std::pair{0.0f,0.0f};
}
