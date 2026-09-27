#include "shiny/camera.h"
#include "shiny/core.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
ScCameraPoint extent(const ScWorld& w) noexcept {
    const float c=std::abs(std::cos(w.camera.rotation)),s=std::abs(std::sin(w.camera.rotation));
    const float x=static_cast<float>(w.view_width)*.5f/w.camera.zoom;
    const float y=static_cast<float>(w.view_height)*.5f/w.camera.zoom;
    return {c*x+s*y,s*x+c*y};
}
float noise(std::uint32_t n) noexcept {
    n^=n>>16; n*=0x7feb352du; n^=n>>15; n*=0x846ca68bu; n^=n>>16;
    return static_cast<float>(n>>8)*(2.f/16777215.f)-1;
}
}
ScCameraPoint sc_camera_to_screen(const ScCameraView& v,ScCameraPoint p) noexcept {
    const float x=p.x-v.center.x,y=p.y-v.center.y,c=std::cos(v.rotation),s=std::sin(v.rotation);
    return {v.offset.x+(c*x-s*y)*v.zoom,v.offset.y+(s*x+c*y)*v.zoom};
}
ScCameraPoint sc_camera_to_world(const ScCameraView& v,ScCameraPoint p) noexcept {
    const float x=(p.x-v.offset.x)/v.zoom,y=(p.y-v.offset.y)/v.zoom,c=std::cos(v.rotation),s=std::sin(v.rotation);
    return {v.center.x+c*x+s*y,v.center.y-s*x+c*y};
}
ScCameraView sc_camera_view(const ScWorld& w,bool snap) noexcept {
    ScCameraView v;
    v.offset={static_cast<float>(w.view_width)*.5f,static_cast<float>(w.view_height)*.5f};
    const auto& camera=w.camera;
    float x=w.camera_x,y=w.camera_y;
    if(camera.shake_frame<camera.shake_frames) {
        const float amplitude=camera.shake_amplitude*(1-static_cast<float>(camera.shake_frame)/static_cast<float>(camera.shake_frames));
        x+=amplitude*noise(camera.shake_seed+camera.shake_frame*2);
        y+=amplitude*noise(camera.shake_seed+camera.shake_frame*2+1);
    }
    if(snap&&camera.pixel_snap) { x=std::floor(x); y=std::floor(y); }
    v.center={x+v.offset.x,y+v.offset.y}; v.zoom=camera.zoom; v.rotation=camera.rotation;
    const auto e=extent(w); v.visible={v.center.x-e.x,v.center.y-e.y,2*e.x,2*e.y}; return v;
}
ScCameraView sc_camera_mix(const ScCameraView& old,const ScCameraView& current,float alpha,bool snap) noexcept {
    auto v=current;
    v.center={std::lerp(old.center.x,v.center.x,alpha),std::lerp(old.center.y,v.center.y,alpha)};
    v.zoom=std::lerp(old.zoom,v.zoom,alpha);
    v.rotation=old.rotation+std::remainder(v.rotation-old.rotation,2*std::numbers::pi_v<float>)*alpha;
    if(snap) { v.center.x=std::floor(v.center.x-v.offset.x)+v.offset.x; v.center.y=std::floor(v.center.y-v.offset.y)+v.offset.y; }
    const float c=std::abs(std::cos(v.rotation)),s=std::abs(std::sin(v.rotation));
    const float x=(c*v.offset.x+s*v.offset.y)/v.zoom,y=(s*v.offset.x+c*v.offset.y)/v.zoom;
    v.visible={v.center.x-x,v.center.y-y,2*x,2*y}; return v;
}
void sc_camera_step(ScWorld& w,bool follow) noexcept {
    const float half_x=static_cast<float>(w.view_width)*.5f,half_y=static_cast<float>(w.view_height)*.5f;
    auto limits=w.camera.rectangle;
    const bool bounded=w.camera.bounds==ScCameraBounds::custom||(w.camera.bounds==ScCameraBounds::map&&w.map.bounded);
    if(w.camera.bounds==ScCameraBounds::map) limits={0,0,static_cast<float>(w.map.width)*static_cast<float>(w.map.tile_size),static_cast<float>(w.map.height)*static_cast<float>(w.map.tile_size)};
    const auto e=extent(w);
    auto clamp=[&](float value,bool horizontal) {
        if(!std::isfinite(value)) value=0;
        if(!bounded) return std::clamp(value,-1e6f,1e6f);
        const float start=horizontal?limits.x:limits.y,length=horizontal?limits.w:limits.h;
        const float reach=horizontal?e.x:e.y,half=horizontal?half_x:half_y;
        // An undersized bound aligns its top/left edge with the visible AABB.
        return std::clamp(value,start+reach-half,start+std::max(reach,length-reach)-half);
    };
    if(follow) {
        if(const auto* entity=sc_entity(&w,w.camera_target)) {
            const float x=clamp(entity->x+entity->w*.5f-half_x,true),y=clamp(entity->y+entity->h*.5f-half_y,false);
            w.camera_x+=(x-w.camera_x)*w.camera.smoothing; w.camera_y+=(y-w.camera_y)*w.camera.smoothing;
        } else w.camera_target=0;
        if(w.camera.shake_pending) w.camera.shake_pending=false;
        else if(w.camera.shake_frame<w.camera.shake_frames) ++w.camera.shake_frame;
    }
    w.camera_x=clamp(w.camera_x,true); w.camera_y=clamp(w.camera_y,false);
}
