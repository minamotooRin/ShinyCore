#include "occlusion.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {
constexpr double tau=2*std::numbers::pi;
bool overlaps(ScLightBounds a,ScLightBounds b) {
    return a.x<=b.x+b.w&&b.x<=a.x+a.w&&a.y<=b.y+b.h&&b.y<=a.y+a.h;
}
bool valid(ScLightBounds b) {
    return std::isfinite(b.x)&&std::isfinite(b.y)&&std::isfinite(b.w)&&std::isfinite(b.h)&&b.w>=0&&b.h>=0;
}
double cross(double x,double y,double dx,double dy) { return x*dy-y*dx; }
}
ScOcclusion::ScOcclusion(std::size_t capacity) {
    if(capacity>16384) throw std::invalid_argument("occluder capacity must be 0..16384");
    polygons_.resize(capacity);
    angles_.resize(radial_rays+capacity*18*3);
    outline_.resize(angles_.size()+1);
}
ScOcclusion::Polygon ScOcclusion::shape(float x,float y,float w,float h,int kind,const float* vertices,int count) {
    Polygon p;
    if(kind==3) {
        if(count<3||count>8) throw std::invalid_argument("occluder polygon requires 3..8 vertices");
        p.count=static_cast<std::size_t>(count);
        for(std::size_t i=0;i<p.count;++i) p.points[i]={x+vertices[2*i],y+vertices[2*i+1]};
    } else if(kind==1||kind==2) {
        const float r=std::min(w,h)*.5f,cx=x+w*.5f,cy=y+h*.5f;
        const float half=kind==2?(std::max(w,h)*.5f-r):0;
        if(half>0) {
            p.count=18;
            for(std::size_t i=0;i<18;++i) {
                const bool first=i<9;
                const double angle=(first?-.5:.5)*std::numbers::pi+double(i%9)*std::numbers::pi/8;
                const float u=(first?half:-half)+r*static_cast<float>(std::cos(angle));
                const float v=r*static_cast<float>(std::sin(angle));
                p.points[i]=w>h?Point{cx+u,cy+v}:Point{cx-v,cy+u};
            }
        } else {
            p.count=curve_vertices;
            for(std::size_t i=0;i<p.count;++i) {
                const double angle=double(i)*tau/double(p.count);
                p.points[i]={cx+r*static_cast<float>(std::cos(angle)),cy+r*static_cast<float>(std::sin(angle))};
            }
        }
    } else {
        p.count=4; p.points[0]={x,y}; p.points[1]={x+w,y}; p.points[2]={x+w,y+h}; p.points[3]={x,y+h};
    }
    return p;
}
void ScOcclusion::append(Polygon p,ScLightBounds region) {
    float x=p.points[0].x,y=p.points[0].y,right=x,bottom=y;
    for(std::size_t i=1;i<p.count;++i) {
        x=std::min(x,p.points[i].x); y=std::min(y,p.points[i].y);
        right=std::max(right,p.points[i].x); bottom=std::max(bottom,p.points[i].y);
    }
    p.bounds={x,y,right-x,bottom-y};
    if(!overlaps(p.bounds,region)) return;
    ++required_;
    if(count_<polygons_.size()) polygons_[count_++]=p;
}
std::expected<void,const char*> ScOcclusion::collect(const ScWorld& world,ScLightBounds region,std::span<const ScOccluderOverride> modes,float alpha) {
    count_=required_=0;
    if(!valid(region)) return std::unexpected("invalid occluder collection region");
    const float tile=static_cast<float>(world.map.tile_size);
    for(int y=0;y<world.map.height;++y) for(int x=0;x<world.map.width;) {
        if(sc_tile(&world,x,y)!='#') { ++x; continue; }
        int end=x+1;
        while(end<world.map.width&&sc_tile(&world,end,y)=='#') ++end;
        append(shape(static_cast<float>(x)*tile,static_cast<float>(y)*tile,static_cast<float>(end-x)*tile,tile,0,nullptr,0),region);
        x=end;
    }
    for(const auto& terrain:world.terrain_shapes) if(!terrain.one_way)
        append(shape(terrain.x,terrain.y,terrain.w,terrain.h,terrain.vertex_count?3:0,terrain.vertices.data(),terrain.vertex_count),region);
    for(const auto& e:world.entities) {
        if(!e.alive) continue;
        const auto mode=sc_occluder_mode(modes,e.id);
        if(mode==ScOccluderMode::none) continue;
        if(mode==ScOccluderMode::body&&(!e.solid||e.sensor||e.one_way||(!e.body_type&&!e.dynamic))) continue;
        const auto pose=sc_display_pose(world,e,alpha);
        const float cosine=std::cos(pose.angle),sine=std::sin(pose.angle),cx=pose.x+e.w*.5f,cy=pose.y+e.h*.5f;
        auto add=[&](Polygon p) {
            p.entity=e.id;
            for(std::size_t i=0;i<p.count;++i) {
                const float x=p.points[i].x-e.w*.5f,y=p.points[i].y-e.h*.5f;
                p.points[i]={cx+x*cosine-y*sine,cy+x*sine+y*cosine};
            }
            append(p,region);
        };
        if(mode==ScOccluderMode::bounds) add(shape(0,0,e.w,e.h,0,nullptr,0));
        else if(e.shape_count) for(int i=0;i<e.shape_count;++i) {
            const auto& s=e.shapes[static_cast<std::size_t>(i)];
            add(shape(s.x,s.y,s.w,s.h,s.kind,s.vertices.data(),s.vertex_count));
        } else add(shape(0,0,e.w,e.h,e.shape,e.vertices.data(),e.vertex_count));
    }
    if(required_>polygons_.size()) { count_=0; return std::unexpected("visible occluder capacity exceeded"); }
    return {};
}
float ScOcclusion::distance(float x,float y,float dx,float dy,float radius,ScEntityId ignore) const {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(radius)||radius<0)
        throw std::invalid_argument("invalid light ray");
    const double magnitude=std::hypot(double(dx),double(dy));
    if(magnitude==0) throw std::invalid_argument("light ray requires a nonzero direction");
    const double vx=dx/magnitude,vy=dy/magnitude;
    double nearest=radius;
    const ScLightBounds bounds{x-radius,y-radius,2*radius,2*radius};
    for(std::size_t i=0;i<count_;++i) {
        const auto& p=polygons_[i];
        if((ignore&&p.entity==ignore)||!overlaps(bounds,p.bounds)) continue;
        bool positive=false,negative=false;
        for(std::size_t j=0;j<p.count;++j) {
            const auto a=p.points[j],b=p.points[(j+1)%p.count];
            const double ax=double(a.x)-x,ay=double(a.y)-y,ex=double(b.x)-a.x,ey=double(b.y)-a.y;
            const double side=cross(ex,ey,-ax,-ay);
            positive|=side>1e-7; negative|=side< -1e-7;
            const double divisor=cross(vx,vy,ex,ey);
            if(std::abs(divisor)<1e-12) continue;
            const double t=cross(ax,ay,ex,ey)/divisor,u=cross(ax,ay,vx,vy)/divisor;
            if(t>=0&&t<nearest&&u>=0&&u<=1) nearest=t;
        }
        if(!(positive&&negative)) return 0; // Inside or touching this closed convex occluder.
    }
    return static_cast<float>(nearest);
}
std::span<const ScLightPoint> ScOcclusion::outline(float x,float y,float radius,ScEntityId ignore,bool shadows) {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(radius)||radius<=0)
        throw std::invalid_argument("invalid light outline");
    std::size_t count=0;
    auto angle=[&](double a) { a=std::fmod(a+tau,tau); angles_[count++]=a; };
    for(std::size_t i=0;i<radial_rays;++i) angle(double(i)*tau/double(radial_rays));
    const ScLightBounds bounds{x-radius,y-radius,2*radius,2*radius};
    for(std::size_t i=0;shadows&&i<count_;++i) {
        const auto& p=polygons_[i];
        if((ignore&&p.entity==ignore)||!overlaps(bounds,p.bounds)) continue;
        for(std::size_t j=0;j<p.count;++j) {
            const double a=std::atan2(double(p.points[j].y)-y,double(p.points[j].x)-x);
            angle(a-1e-5); angle(a); angle(a+1e-5);
        }
    }
    std::sort(angles_.begin(),angles_.begin()+static_cast<std::ptrdiff_t>(count));
    for(std::size_t i=0;i<count;++i) {
        const float dx=static_cast<float>(std::cos(angles_[i])),dy=static_cast<float>(std::sin(angles_[i]));
        const float d=shadows?distance(x,y,dx,dy,radius,ignore):radius;
        outline_[i]={x+dx*d,y+dy*d,d};
    }
    outline_[count]=outline_[0]; return {outline_.data(),count+1};
}
