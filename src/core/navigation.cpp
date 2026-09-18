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
bool walkable(const ScMap& map,int i) { return i>=0&&i<map.width*map.height&&map.tiles[static_cast<std::size_t>(i)]=='.'; }
std::array<int,4> neighbors(const ScMap& map,int i) {
    return {i%map.width>0?i-1:-1,i>=map.width?i-map.width:-1,i%map.width+1<map.width?i+1:-1,i/map.width+1<map.height?i+map.width:-1};
}
void validate(const ScMap& map,int goal,std::size_t budget) {
    if(map.width<=0||map.height<=0||map.tile_size<=0||map.width>SC_MAX_TILES/map.height||!walkable(map,goal)||budget==0||budget>1048576)
        throw std::invalid_argument("navigation requires a walkable goal and budget 1..1048576");
}
}
ScPath sc_path(const ScMap& map,int start,int goal,std::size_t budget) {
    validate(map,goal,budget);
    if(!walkable(map,start)) return {"unreachable",{},0};
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
        for(int next:neighbors(map,current)) if(walkable(map,next)&&cost+1<distance[static_cast<std::size_t>(next)]) {
            distance[static_cast<std::size_t>(next)]=cost+1; parent[static_cast<std::size_t>(next)]=current;
            open.emplace(cost+1+heuristic(next),cost+1,next);
        }
    }
    return result;
}
void ScFlowField::build(const ScMap& map,int goal,std::size_t budget) {
    validate(map,goal,budget); width_=map.width; height_=map.height;
    distance_.assign(static_cast<std::size_t>(width_*height_),INT_MAX);
    queue_.clear(); queue_.reserve(distance_.size()); queue_.push_back(goal);
    distance_[static_cast<std::size_t>(goal)]=0; visited=0; status="ok";
    for(std::size_t at=0;at<queue_.size();++at) {
        if(visited==budget) { status="budget_exhausted"; return; }
        ++visited; int current=queue_[at];
        for(int next:neighbors(map,current)) if(walkable(map,next)&&distance_[static_cast<std::size_t>(next)]==INT_MAX) {
            distance_[static_cast<std::size_t>(next)]=distance_[static_cast<std::size_t>(current)]+1; queue_.push_back(next);
        }
    }
}
std::pair<float,float> ScFlowField::direction(const ScMap& map,float x,float y) const {
    if(width_!=map.width||height_!=map.height||distance_.empty()||map.tile_size<=0||
       !std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1e6f||std::fabs(y)>1e6f) return {};
    int cx=static_cast<int>(std::floor(x/static_cast<float>(map.tile_size))),cy=static_cast<int>(std::floor(y/static_cast<float>(map.tile_size)));
    if(cx<0||cy<0||cx>=width_||cy>=height_) return {};
    int current=cy*width_+cx,best=current;
    for(int next:neighbors(map,current)) if(walkable(map,next)&&distance_[static_cast<std::size_t>(next)]<distance_[static_cast<std::size_t>(best)]) best=next;
    if(best==current) return {};
    float dx=(static_cast<float>(best%width_)+.5f)*static_cast<float>(map.tile_size)-x;
    float dy=(static_cast<float>(best/width_)+.5f)*static_cast<float>(map.tile_size)-y;
    float length=std::hypot(dx,dy); return length>0?std::pair{dx/length,dy/length}:std::pair{0.0f,0.0f};
}
