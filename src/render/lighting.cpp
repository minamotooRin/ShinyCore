#include "shiny/lighting.h"
#include "shiny/core.h"
#include <cmath>
#include <numbers>
#include <stdexcept>

ScLightSample sc_light_sample(float softness,int samples,int index) {
    if(!std::isfinite(softness)||softness<0||softness>32||samples<1||samples>8||index<0||index>=samples)
        throw std::invalid_argument("invalid area-light sample");
    if(samples==1) return {0,0,1};
    const double angle=2*std::numbers::pi*index/samples;
    return {softness*static_cast<float>(std::cos(angle)),softness*static_cast<float>(std::sin(angle)),1.f/static_cast<float>(samples)};
}
std::expected<void,const char*> sc_collect_lights(const ScWorld& world,const ScLighting& settings,ScLightFrame& frame,float alpha) {
    frame.count=frame.shadow_count=0;
    const auto view=sc_display_camera(world,alpha).visible;
    auto append=[&](ScPointLight point)->const char* {
        for(float n:{point.x,point.y,point.radius,point.intensity,point.softness,point.height})
            if(!std::isfinite(n)) return "point light fields must be finite";
        if(point.radius<=0||point.radius>1024||point.intensity<0||point.intensity>1||point.softness<0||point.softness>32||point.samples<1||point.samples>8||point.height<=0||point.height>4096)
            return "point light geometry or quality outside supported range";
        const float reach=point.radius+(point.shadows&&point.samples>1?point.softness:0);
        if(point.intensity==0||(point.color&255)==0||point.x+reach<view.x||point.y+reach<view.y||
           point.x-reach>view.x+view.w||point.y-reach>view.y+view.h) return nullptr;
        if(frame.count==frame.lights.size()) return "visible light capacity exceeded (capacity=32)";
        if(point.shadows&&frame.shadow_count==ScLighting::shadow_capacity) return "visible shadow light capacity exceeded (capacity=16)";
        frame.lights[frame.count++]=point; frame.shadow_count+=point.shadows?1:0; return nullptr;
    };
    auto fail=[&](const char* error)->std::expected<void,const char*> {
        frame.count=frame.shadow_count=0; return std::unexpected(error);
    };
    for(const auto& entity:world.entities) if(entity.alive&&entity.glow>0) {
        // Preserve the existing glow hue and brightness while sharing the same path.
        auto channel=[&](unsigned shift) { return 160+((entity.color>>shift)&255)*95/255; };
        ScPointLight point;
        const auto pose=sc_display_pose(world,entity,alpha);
        point.x=pose.x+entity.w*.5f; point.y=pose.y+entity.h*.5f; point.radius=entity.glow;
        point.intensity=.95f; point.softness=settings.softness; point.samples=settings.samples; point.ignore=entity.id;
        point.color=(channel(24)<<24)|(channel(16)<<16)|(channel(8)<<8)|255;
        if(auto error=append(point)) return fail(error);
    }
    if(settings.point_count>settings.points.size()) return fail("point light command capacity exceeded (capacity=32)");
    for(std::size_t i=0;i<settings.point_count;++i) if(auto error=append(settings.points[i])) return fail(error);
    return {};
}
const char* ScNormalMaps::find(std::string_view image) const noexcept {
    for(std::size_t i=0;i<count;++i) if(bindings[i].image==image) return bindings[i].normal.c_str();
    return nullptr;
}
ScOccluderMode sc_occluder_mode(std::span<const ScOccluderOverride> modes,std::uint64_t entity) noexcept {
    const auto slot=sc_entity_slot(entity);
    return entity&&slot<modes.size()&&modes[slot].entity==entity?modes[slot].mode:ScOccluderMode::body;
}
std::expected<void,std::string> sc_normal_bind(const ScWorld& world,ScLighting& lighting,std::string_view image,std::string_view normal) {
    const ScResource* diffuse=nullptr; const ScResource* normals=nullptr;
    for(const auto& resource:world.resources) if(resource.type=="image") {
        if(resource.name==image) diffuse=&resource;
        if(resource.name==normal) normals=&resource;
    }
    if(!diffuse||!normals) return std::unexpected("normal binding requires two declared image resources");
    if(diffuse->image_width!=normals->image_width||diffuse->image_height!=normals->image_height)
        return std::unexpected("normal map dimensions must match its image/atlas exactly");
    // Construct strings before changing the active binding, including replacement.
    ScNormalBinding binding{diffuse->path,normals->path};
    if(!lighting.normal_maps) lighting.normal_maps=std::make_unique<ScNormalMaps>();
    auto& maps=*lighting.normal_maps;
    for(std::size_t i=0;i<maps.count;++i) if(maps.bindings[i].image==binding.image) {
        maps.bindings[i]=std::move(binding); return {};
    }
    if(maps.count==maps.bindings.size()) return std::unexpected("normal map capacity exceeded (capacity=64)");
    maps.bindings[maps.count++]=std::move(binding); return {};
}
std::array<float,4> sc_normal_basis(bool flip_x,bool flip_y,bool diagonal,float angle) {
    const float x=flip_x?-1.f:1.f,y=flip_y?-1.f:1.f,c=std::cos(angle),s=std::sin(angle);
    return diagonal?std::array<float,4>{-s*y,c*y,c*x,s*x}:std::array<float,4>{c*x,s*x,-s*y,c*y};
}
