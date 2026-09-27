#include "shiny/attachment.h"
#include <algorithm>
#include <cmath>

namespace {
const ScEntity* live(const ScWorld& w,ScEntityId id) noexcept {
    const auto slot=sc_entity_slot(id);
    if(!id||slot>=w.entities.size()) return nullptr;
    const auto& e=w.entities[slot];
    return e.alive&&e.id==id?&e:nullptr;
}
bool valid(ScPose p) noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.angle)&&
        std::abs(p.x)<=1e6f&&std::abs(p.y)<=1e6f&&std::abs(p.angle)<=1e6f;
}
ScPose bounded(ScPose p) noexcept {
    return {std::clamp(p.x,-1e6f,1e6f),std::clamp(p.y,-1e6f,1e6f),std::clamp(p.angle,-1e6f,1e6f)};
}
ScPose fixed_pose(const ScWorld& w,const ScEntity& entity) noexcept {
    std::array<const ScEntity*,SC_ATTACHMENT_DEPTH> path{};
    const auto* e=&entity; std::size_t count=0;
    while(e->parent&&count<path.size()) {
        const auto* parent=live(w,e->parent); if(!parent) break;
        path[count++]=e; e=parent;
    }
    ScPose pose{e->x,e->y,e->angle};
    while(count) {
        const auto* child=path[--count];
        pose=bounded(sc_attachment_transform(pose,e->w,e->h,*child,child->local_pose)); e=child;
    }
    return pose;
}
}
ScPose sc_attachment_transform(ScPose parent,float pw,float ph,const ScEntity& child,ScPose local) noexcept {
    const float x=local.x+(child.w-pw)*.5f,y=local.y+(child.h-ph)*.5f;
    const float c=std::cos(parent.angle),s=std::sin(parent.angle);
    return {parent.x+(pw-child.w)*.5f+x*c-y*s,
            parent.y+(ph-child.h)*.5f+x*s+y*c,parent.angle+local.angle};
}
bool sc_attachment_patch_valid(const ScEntity& before,const ScEntity& after) noexcept {
    return !before.parent||(!after.body_type&&!after.dynamic&&after.vx==0&&after.vy==0&&after.angular_velocity==0&&
        before.x==after.x&&before.y==after.y&&before.angle==after.angle);
}
std::expected<void,const char*> sc_attachment_batch_preflight(std::span<const ScEntity> drafts,
                                                             std::span<const std::size_t> parents) noexcept {
    if(parents.empty()) return {};
    if(parents.size()!=drafts.size()) return std::unexpected("parents must match entity batch length");
    for(std::size_t i=0;i<parents.size();++i) {
        if(parents[i]>drafts.size()) return std::unexpected("parent index is outside entity batch");
        const auto& e=drafts[i];
        if(parents[i]&&(e.body_type||e.dynamic||e.vx!=0||e.vy!=0||e.angular_velocity!=0))
            return std::unexpected("attached child must be bodyless with zero velocity");
    }
    for(std::size_t i=0;i<drafts.size();++i) if(parents[i]) {
        std::array<std::size_t,SC_ATTACHMENT_DEPTH> path{};
        std::size_t node=i,count=0;
        while(parents[node]) {
            if(count==path.size()) return std::unexpected("attachment cycle or depth exceeds 32");
            path[count++]=node; node=parents[node]-1;
        }
        const auto* parent=&drafts[node]; ScPose pose{parent->x,parent->y,parent->angle};
        while(count) {
            const auto& child=drafts[path[--count]];
            pose=sc_attachment_transform(pose,parent->w,parent->h,child,{child.x,child.y,child.angle});
            if(!valid(pose)) return std::unexpected("attachment world pose exceeds +/-1000000");
            parent=&child;
        }
    }
    return {};
}
std::expected<void,const char*> sc_attach(ScWorld& w,ScEntityId child_id,ScEntityId parent_id,ScPose local) noexcept {
    auto* child=sc_entity(&w,child_id); const auto* parent=sc_entity(&w,parent_id);
    if(!child||!parent) return std::unexpected("attachment requires live child and parent handles");
    if(child->body_type||child->dynamic||child->vx!=0||child->vy!=0||child->angular_velocity!=0)
        return std::unexpected("attached child must be bodyless with zero velocity");
    if(!valid(local)) return std::unexpected("attachment offset must be finite within +/-1000000");
    // Check the proposed chain for every descendant too: reparenting may deepen a whole subtree.
    for(const auto& item:w.entities) if(item.alive) {
        const ScEntity* node=&item; std::size_t depth=0;
        while(node) {
            const auto next=node->id==child_id?parent_id:node->parent;
            if(!next) break;
            if(++depth>SC_ATTACHMENT_DEPTH) return std::unexpected("attachment cycle or depth exceeds 32");
            node=live(w,next);
        }
    }
    if(!valid(sc_attachment_transform(fixed_pose(w,*parent),parent->w,parent->h,*child,local)))
        return std::unexpected("attachment world pose exceeds +/-1000000");
    sc_presentation_capture(w);
    if(child->parent!=parent_id) sc_presentation_snap(w,child_id);
    child->parent=parent_id; child->local_pose=local;
    sc_attachments_sync(w);
    return {};
}
std::expected<void,const char*> sc_detach(ScWorld& w,ScEntityId child_id) noexcept {
    auto* child=sc_entity(&w,child_id);
    if(!child) return std::unexpected("detach requires a live entity handle");
    if(!child->parent) return {};
    const auto pose=fixed_pose(w,*child);
    child->parent=0; child->local_pose={}; child->x=pose.x; child->y=pose.y; child->angle=pose.angle;
    sc_presentation_snap(w,child_id); sc_attachments_sync(w);
    return {};
}
void sc_attachments_sync(ScWorld& w) noexcept {
    for(auto& e:w.entities) if(e.alive&&e.parent) {
        if(!live(w,e.parent)) { e.parent=0; e.local_pose={}; sc_presentation_snap(w,e.id); continue; }
        const auto pose=fixed_pose(w,e); e.x=pose.x; e.y=pose.y; e.angle=pose.angle;
    }
}
