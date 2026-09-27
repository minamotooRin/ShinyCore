#include "shiny/presentation.h"
#include "shiny/core.h"
#include "shiny/projectiles.h"
#include "shiny/attachment.h"
#include <algorithm>
#include <cmath>
#include <numbers>

void sc_presentation_configure(ScWorld& w,bool enabled) {
    if(!w.presentation&&enabled) {
        auto history=std::make_unique<ScPresentation>(w.entities.size());
        std::vector<ScDisplayPoint> particles(w.particles.capacity());
        std::vector<ScDisplayPoint> projectiles(w.projectiles?w.projectiles->x.size():0);
        w.particles.previous_display=std::move(particles);
        if(w.projectiles) w.projectiles->previous_display=std::move(projectiles);
        w.presentation=std::move(history);
    }
    if(w.presentation&&w.presentation->enabled!=enabled) {
        w.presentation->enabled=enabled; w.presentation->captured=false;
    }
}
void sc_presentation_capture(ScWorld& w) noexcept {
    auto* h=w.presentation.get();
    if(!h||!h->enabled||(h->captured&&h->tick==w.tick)) return;
    for(std::size_t i=0;i<w.entities.size();++i) {
        const auto& e=w.entities[i]; h->entities[i]={e.alive?e.id:0,{e.x,e.y,e.angle},e.parent,e.local_pose};
    }
    for(std::size_t i=0;i<w.particles.count;++i) w.particles.previous_display[i]={w.particles.x[i],w.particles.y[i],true};
    if(auto* p=w.projectiles.get()) for(std::size_t i=0;i<p->count;++i) p->previous_display[i]={p->x[i],p->y[i],true};
    h->camera=sc_camera_view(w,false); h->camera_x=w.camera_x; h->camera_y=w.camera_y;
    h->camera_valid=true; h->tick=w.tick; h->captured=true;
}
void sc_presentation_snap(ScWorld& w,std::uint64_t entity) noexcept {
    if(!w.presentation) return;
    sc_presentation_capture(w);
    const auto slot=sc_entity_slot(entity);
    if(slot<w.presentation->entities.size()) w.presentation->entities[slot].id=0;
}
void sc_presentation_snap_camera(ScWorld& w) noexcept {
    if(!w.presentation) return;
    sc_presentation_capture(w); w.presentation->camera_valid=false;
}
bool sc_presentation_ready(const ScWorld& w) noexcept {
    const auto* h=w.presentation.get();
    return h&&h->enabled&&h->captured&&h->tick+1==w.tick;
}
ScPose sc_display_pose(const ScWorld& w,const ScEntity& e,float alpha) noexcept {
    if(alpha>=1||!sc_presentation_ready(w)) return {e.x,e.y,e.angle};
    std::array<const ScEntity*,SC_ATTACHMENT_DEPTH+1> path{};
    std::size_t count=0; const auto* node=&e;
    while(node&&count<path.size()) {
        path[count++]=node;
        const auto slot=sc_entity_slot(node->id);
        if(slot>=w.presentation->entities.size()) return {e.x,e.y,e.angle};
        const auto& old=w.presentation->entities[slot];
        // Cuts and relationship changes apply to the entire descendant chain.
        if(old.id!=node->id||old.parent!=node->parent) return {e.x,e.y,e.angle};
        if(!node->parent) break;
        const auto parent_slot=sc_entity_slot(node->parent);
        if(parent_slot>=w.entities.size()) return {e.x,e.y,e.angle};
        const auto& parent=w.entities[parent_slot];
        if(!parent.alive||parent.id!=node->parent) return {e.x,e.y,e.angle};
        node=&parent;
    }
    auto mix=[alpha](ScPose a,ScPose b) {
        const float turn=std::remainder(b.angle-a.angle,2*std::numbers::pi_v<float>);
        return ScPose{std::lerp(a.x,b.x,alpha),std::lerp(a.y,b.y,alpha),a.angle+turn*alpha};
    };
    const auto* parent=path[--count];
    ScPose pose=mix(w.presentation->entities[sc_entity_slot(parent->id)].pose,{parent->x,parent->y,parent->angle});
    while(count) {
        const auto* child=path[--count]; const auto& old=w.presentation->entities[sc_entity_slot(child->id)];
        pose=sc_attachment_transform(pose,parent->w,parent->h,*child,mix(old.local,child->local_pose));
        pose.x=std::clamp(pose.x,-1e6f,1e6f); pose.y=std::clamp(pose.y,-1e6f,1e6f);
        pose.angle=std::clamp(pose.angle,-1e6f,1e6f); parent=child;
    }
    return pose;
}
ScCameraView sc_display_camera(const ScWorld& w,float alpha) noexcept {
    if(alpha>=1||!sc_presentation_ready(w)||!w.presentation->camera_valid) return sc_camera_view(w);
    return sc_camera_mix(w.presentation->camera,sc_camera_view(w,false),alpha,w.camera.pixel_snap);
}
ScCameraPoint sc_display_camera_anchor(const ScWorld& w,float alpha) noexcept {
    if(alpha>=1||!sc_presentation_ready(w)||!w.presentation->camera_valid) return {w.camera_x,w.camera_y};
    return {std::lerp(w.presentation->camera_x,w.camera_x,alpha),std::lerp(w.presentation->camera_y,w.camera_y,alpha)};
}
ScCameraPoint sc_display_point(const ScWorld& w,const ScDisplayPoint& old,float x,float y,float alpha) noexcept {
    if(alpha>=1||!old.valid||!sc_presentation_ready(w)) return {x,y};
    return {std::lerp(old.x,x,alpha),std::lerp(old.y,y,alpha)};
}
