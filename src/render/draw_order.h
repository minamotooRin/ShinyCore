#pragma once
#include "shiny/core.h"
#include <algorithm>

enum class ScSceneKind { map, image, entity };
struct ScSceneItem { int layer{}; ScSceneKind kind{}; std::size_t index{}; };

// Backend-owned storage is allocated during room preparation. No GPU or allocation.
inline std::expected<std::size_t,const char*> sc_scene_order(const ScWorld& world,std::span<ScSceneItem> storage) {
    std::size_t count=0;
    auto add=[&](int layer,ScSceneKind kind,std::size_t index) {
        if(count==storage.size()) return false;
        storage[count++]={layer,kind,index}; return true;
    };
    for(std::size_t i=0;i<world.layers.size();++i)
        if(!add(world.layers[i].order,ScSceneKind::map,i)) return std::unexpected("scene draw order capacity exhausted");
    for(std::size_t i=0;i<static_cast<std::size_t>(world.draw_count);++i)
        if(world.draws[i].layered&&!add(world.draws[i].layer,ScSceneKind::image,i)) return std::unexpected("scene draw order capacity exhausted");
    for(std::size_t i=0;i<world.entities.size();++i)
        if(world.entities[i].alive&&!add(world.entities[i].layer,ScSceneKind::entity,i)) return std::unexpected("scene draw order capacity exhausted");
    std::sort(storage.begin(),storage.begin()+static_cast<std::ptrdiff_t>(count),[](const auto& a,const auto& b) {
        if(a.layer!=b.layer) return a.layer<b.layer;
        if(a.kind!=b.kind) return a.kind<b.kind;
        return a.index<b.index;
    });
    return count;
}
