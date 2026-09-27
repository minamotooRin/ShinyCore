#pragma once
#include "shiny/core.h"
#include <algorithm>
#include <array>

// Destination positions and normalized source coordinates before flip/diagonal UVs.
// Insets follow the transformed image; undersized destinations shrink opposing
// borders together, leaving a zero-width center instead of overlapping corners.
struct ScImageAxis { std::array<float,4> position{},uv{}; int cells{1}; };
struct ScImageGrid { ScImageAxis x,y; };
inline ScImageGrid sc_image_grid(const ScDraw& draw,float width,float height) {
    auto axis=[](float size,float source,float first,float last) {
        if(first==0&&last==0) return ScImageAxis{{0,size,0,0},{0,1,0,0},1};
        const float scale=std::min(1.f,size/(first+last));
        return ScImageAxis{{0,first*scale,size-last*scale,size},{0,first/source,1-last/source,1},3};
    };
    float left=draw.slice_left,right=draw.slice_right,top=draw.slice_top,bottom=draw.slice_bottom;
    if(draw.diagonal) { std::swap(left,top); std::swap(right,bottom); std::swap(width,height); }
    if(draw.flip_x) std::swap(left,right);
    if(draw.flip_y) std::swap(top,bottom);
    return {axis(draw.w,width,left,right),axis(draw.h,height,top,bottom)};
}
