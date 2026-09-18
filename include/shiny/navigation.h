#pragma once
#include "shiny/core.h"
#include <span>
#include <string_view>

struct ScPath { std::string_view status; std::vector<int> cells; std::size_t visited{}; };
ScPath sc_path(const ScMap& map,int start,int goal,std::size_t budget);
class ScFlowField final {
public:
    void build(const ScMap& map,int goal,std::size_t budget);
    std::pair<float,float> direction(const ScMap& map,float x,float y) const;
    std::string_view status{"unbuilt"};
    std::size_t visited{};
private:
    std::vector<int> distance_,queue_;
    int width_{},height_{};
};
