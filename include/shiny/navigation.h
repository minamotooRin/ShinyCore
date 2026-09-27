#pragma once
#include "shiny/core.h"
#include <span>
#include <string_view>

struct ScPath { std::string_view status; std::vector<int> cells; std::size_t visited{}; };
ScPath sc_path(const ScMap& map,int start,int goal,std::size_t budget,float radius=0);
std::bitset<SC_MAX_TILES> sc_navigation_obstacles(const ScMap&,std::span<const ScTerrainShape>,float origin_x=0,float origin_y=0);
// Pure candidate update: map.navigation_blocked must describe before at the same grid/origin.
struct ScNavigationUpdate { std::bitset<SC_MAX_TILES> blocked,dirty; };
ScNavigationUpdate sc_navigation_patch(const ScMap&,std::span<const ScTerrainShape> before,
    std::span<const ScTerrainShape> after,float origin_x=0,float origin_y=0);
struct ScNavigationRegion { ScMap map; float x{},y{}; };
class ScFlowField final {
public:
    void build(const ScMap& map,int goal,std::size_t budget,float radius=0);
    void invalidate() noexcept { if(status!="unbuilt") status="stale"; }
    void refresh(const ScMap& map,std::size_t budget);
    std::string_view state(const ScMap& map) const noexcept {
        return status!="unbuilt"&&revision_!=map.navigation_revision?"stale":status;
    }
    std::pair<float,float> direction(const ScMap& map,float x,float y) const;
    std::string_view status{"unbuilt"};
    std::size_t visited{};
private:
    std::vector<int> distance_,queue_;
    std::bitset<SC_MAX_TILES> blocked_;
    float radius_{};
    int width_{},height_{},goal_{};
    std::uint64_t revision_{};
};
