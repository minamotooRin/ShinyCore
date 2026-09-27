#include "../src/render/occlusion.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>

#define CHECK(value) do { if(!(value)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#value); std::exit(1); } } while(false)
bool near(float a,float b) { return std::abs(a-b)<.002f; }
static void presentation_lighting() {
    ScWorld world; world.map.width=world.map.height=0; world.entities.resize(1);
    world.camera.bounds=ScCameraBounds::none; world.camera.pixel_snap=false;
    ScEntity spec; spec.x=10; spec.y=0; spec.w=10; spec.h=10; spec.glow=100; spec.body_type=1; spec.solid=true;
    const auto id=sc_spawn(&world,&spec); CHECK(id);
    sc_presentation_configure(world,true); sc_presentation_capture(world);
    sc_entity(&world,id)->x=50; ++world.tick;
    const auto hash=sc_state_hash(&world);
    ScOcclusion geometry; CHECK(geometry.collect(world,{-100,-100,400,400},{},.5f));
    CHECK(near(geometry.distance(0,5,1,0,100),30));
    ScLightFrame frame; ScLighting settings;
    CHECK(sc_collect_lights(world,settings,frame,.5f)); CHECK(frame.count==1&&near(frame.lights[0].x,35));
    CHECK(geometry.collect(world,{-100,-100,400,400},{},1)); CHECK(near(geometry.distance(0,5,1,0,100),50));
    CHECK(sc_state_hash(&world)==hash);
}
int main() {
    presentation_lighting();
    for(int count:{1,2,4,8}) {
        float x=0,y=0,weight=0;
        for(int i=0;i<count;++i) {
            auto sample=sc_light_sample(12,count,i);
            x+=sample.x*sample.weight; y+=sample.y*sample.weight; weight+=sample.weight;
        }
        CHECK(near(x,0)&&near(y,0)&&near(weight,1));
    }
    ScWorld world; world.map.width=world.map.height=0; world.entities.resize(2);
    for(bool diagonal:{false,true}) for(bool flip_x:{false,true}) for(bool flip_y:{false,true}) {
        const auto basis=sc_normal_basis(flip_x,flip_y,diagonal,std::numbers::pi_v<float>/2);
        float x=.6f,y=.8f;
        if(diagonal) std::swap(x,y);
        if(flip_x) x=-x;
        if(flip_y) y=-y;
        CHECK(near(basis[0]*.6f+basis[2]*.8f,-y)&&near(basis[1]*.6f+basis[3]*.8f,x));
    }
    ScLighting maps;
    world.resources={{"color","image","image.png"},{"normal","image","normal.png"},{"bad","image","bad.png"}};
    for(auto& resource:world.resources) resource.image_width=resource.image_height=32;
    world.resources[2].image_width=16;
    CHECK(sc_normal_bind(world,maps,"color","normal"));
    CHECK(maps.normal_maps->count==1&&std::string(maps.normal_maps->find("image.png"))=="normal.png");
    CHECK(!sc_normal_bind(world,maps,"color","bad")); CHECK(!sc_normal_bind(world,maps,"color","absent"));
    CHECK(maps.normal_maps->count==1&&std::string(maps.normal_maps->find("image.png"))=="normal.png");
    CHECK(sc_normal_bind(world,maps,"color","color")); CHECK(maps.normal_maps->count==1);
    for(int i=0;i<64;++i) {
        ScResource resource;resource.name="image"+std::to_string(i);resource.type="image";
        resource.path=resource.name+".png";resource.image_width=resource.image_height=32;
        world.resources.push_back(resource);
        auto bound=sc_normal_bind(world,maps,resource.name,"normal"); CHECK(bool(bound)==(i<63));
    }
    CHECK(maps.normal_maps->count==64); CHECK(!maps.normal_maps->find("image63.png"));
    ScLighting settings; ScLightFrame lights;
    settings.point_count=16;
    for(std::size_t i=0;i<settings.point_count;++i) { settings.points[i].x=50; settings.points[i].y=50; settings.points[i].shadows=i<8; }
    CHECK(sc_collect_lights(world,settings,lights)); CHECK(lights.count==16&&lights.shadow_count==8);
    settings.point_count=32;
    for(auto& p:settings.points) p.shadows=false;
    CHECK(sc_collect_lights(world,settings,lights)); CHECK(lights.count==32&&lights.shadow_count==0);
    for(auto& p:settings.points) p.shadows=true;
    CHECK(!sc_collect_lights(world,settings,lights)); CHECK(lights.count==0&&lights.shadow_count==0);
    for(auto& p:settings.points) { p.shadows=false; p.x=100000; }
    CHECK(sc_collect_lights(world,settings,lights)); CHECK(lights.count==0);
    settings.point_count=1; settings.points[0].x=50; settings.points[0].intensity=0;
    CHECK(sc_collect_lights(world,settings,lights)); CHECK(lights.count==0);
    settings.points[0].intensity=1; settings.points[0].color=0xffffff00;
    CHECK(sc_collect_lights(world,settings,lights)); CHECK(lights.count==0);
    settings.points[0].color=0x00ff0080;
    CHECK(sc_collect_lights(world,settings,lights)); CHECK(lights.count==1&&lights.lights[0].color==0x00ff0080);
    settings.points[0].radius=-1;
    CHECK(!sc_collect_lights(world,settings,lights)); CHECK(lights.count==0);
    settings.point_count=0;
    ScOcclusion geometry(8);
    const ScLightBounds region{-1000,-1000,2000,2000};
    CHECK(geometry.collect(world,region)); CHECK(geometry.size()==0);
    CHECK(near(geometry.distance(0,0,1,0,100),100));
    world.terrain_shapes={{10,-5,10,10}};
    CHECK(geometry.collect(world,region)); CHECK(geometry.size()==1);
    CHECK(near(geometry.distance(0,0,2,0,100),10));
    CHECK(near(geometry.distance(30,0,-1,0,100),10));
    CHECK(geometry.distance(15,0,1,0,100)==0);
    CHECK(geometry.distance(10,0,-1,0,100)==0);
    const auto unshadowed=geometry.outline(15,0,100,0,false);
    CHECK(unshadowed.size()==ScOcclusion::radial_rays+1);
    CHECK(std::all_of(unshadowed.begin(),unshadowed.end(),[](auto p){return near(p.distance,100);}));
    CHECK(near(geometry.distance(0,0,-1,0,100),100));
    auto outline=geometry.outline(0,0,100);
    CHECK(outline.size()>ScOcclusion::radial_rays);
    CHECK(outline.front().x==outline.back().x&&outline.front().y==outline.back().y);
    CHECK(std::any_of(outline.begin(),outline.end(),[](auto p){return near(p.x,10)&&near(p.y,5);}));
    CHECK(!world.physics); // Collection never creates or synchronizes a solver.
    const auto* storage=outline.data(); CHECK(geometry.outline(2,0,50).data()==storage);

    // Polygon order is independent of winding; local terrain coordinates may be negative.
    auto& t=world.terrain_shapes[0]; t={-20,-10,20,20}; t.vertex_count=3;
    t.vertices={0,0,20,10,0,20};
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(10,0,-1,0,100),10));
    t.vertices={0,20,20,10,0,0};
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(10,0,-1,0,100),10));
    t.one_way=true; CHECK(geometry.collect(world,region)); CHECK(geometry.size()==0);
    world.terrain_shapes.clear();

    auto& e=world.entities[0]; e.alive=e.solid=true; e.id=0x100010000ULL; e.body_type=3;
    e.x=10; e.y=-5; e.w=20; e.h=10;
    e.glow=100; settings.softness=4; settings.samples=4;
    CHECK(sc_collect_lights(world,settings,lights)); CHECK(lights.count==1&&lights.shadow_count==1);
    CHECK(lights.lights[0].ignore==e.id&&near(lights.lights[0].x,20)&&near(lights.lights[0].softness,4)&&lights.lights[0].samples==4);
    settings.point_count=32;
    for(auto& p:settings.points) { p={}; p.x=50; p.y=50; p.shadows=false; }
    CHECK(!sc_collect_lights(world,settings,lights)); CHECK(lights.count==0); // Glows and commands share the total budget.
    e.glow=0;
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(0,0,1,0,100),10));
    CHECK(near(geometry.distance(0,0,1,0,100,e.id),100));
    e.angle=std::numbers::pi_v<float>/2;
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(0,0,1,0,100),15));
    e.angle=0; e.shape=1;
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(0,0,1,0,100),15));
    CHECK(near(geometry.distance(20,20,0,-1,100),15));
    e.shape=2;
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(0,0,1,0,100),10));
    CHECK(near(geometry.distance(20,20,0,-1,100),15));
    e.w=10; e.h=20; e.y=-10;
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(15,-20,0,1,100),10));
    CHECK(near(geometry.distance(0,0,1,0,100),10));

    e.shape_count=2;
    e.shapes[0]={0,0,0,2,2}; e.shapes[1]={0,8,0,2,2};
    CHECK(geometry.collect(world,region)); CHECK(geometry.size()==2);
    CHECK(near(geometry.distance(15,-20,0,1,100),100)); // Gap between compound shapes.
    CHECK(near(geometry.distance(11,-20,0,1,100),10));
    e.sensor=true; CHECK(geometry.collect(world,region)); CHECK(geometry.size()==0);
    e.sensor=false; e.one_way=true; CHECK(geometry.collect(world,region)); CHECK(geometry.size()==0);
    e.one_way=false; e.solid=false; CHECK(geometry.collect(world,region)); CHECK(geometry.size()==0);
    std::vector<ScOccluderOverride> overrides(world.entities.size());
    overrides[0]={e.id,ScOccluderMode::shape};
    const auto unchanged=sc_state_hash(&world);
    CHECK(geometry.collect(world,region,overrides)); CHECK(geometry.size()==2);
    CHECK(near(geometry.distance(15,-20,0,1,100),100));
    overrides[0].mode=ScOccluderMode::bounds;
    CHECK(geometry.collect(world,region,overrides)); CHECK(geometry.size()==1);
    CHECK(near(geometry.distance(15,-20,0,1,100),10));
    CHECK(sc_state_hash(&world)==unchanged&&!world.physics);
    overrides[0].mode=ScOccluderMode::none;
    CHECK(geometry.collect(world,region,overrides)); CHECK(geometry.size()==0);
    e.body_type=0;e.shape_count=0;e.shape=0;e.solid=true;e.angle=std::numbers::pi_v<float>/2;
    CHECK(geometry.collect(world,region));CHECK(geometry.size()==0);
    overrides[0].mode=ScOccluderMode::bounds;
    CHECK(geometry.collect(world,region,overrides));CHECK(near(geometry.distance(0,0,1,0,100),5));
    CHECK(near(geometry.distance(0,0,1,0,100,e.id),100));
    e.id+=65536; // Reused slot, new generation: old override must not apply.
    CHECK(sc_occluder_mode(overrides,e.id)==ScOccluderMode::body);
    CHECK(geometry.collect(world,region,overrides)); CHECK(geometry.size()==0);
    e.solid=true; e.alive=false;

    world.map.width=4; world.map.height=1; world.map.tile_size=16;
    world.map.tiles[0]='#'; world.map.tiles[1]='#'; world.map.tiles[2]='=';
    CHECK(geometry.collect(world,region)); CHECK(geometry.size()==1);
    CHECK(near(geometry.distance(50,8,-1,0,100),18));
    world.map.tiles[1]='.';
    CHECK(geometry.collect(world,region)); CHECK(near(geometry.distance(50,8,-1,0,100),34));

    ScOcclusion limited(1);
    world.terrain_shapes={{100,100,10,10}};
    CHECK(!limited.collect(world,region)); CHECK(limited.required()==2&&limited.size()==0);
    CHECK(limited.collect(world,{90,90,30,30})); CHECK(limited.size()==1);
    CHECK(limited.collect(world,{500,500,20,20})); CHECK(limited.size()==0);
    CHECK(geometry.outline(0,0,100).data()==storage);
    std::puts("lighting geometry: bounded lights, silhouettes, normal bindings and tangent transforms passed");
}
