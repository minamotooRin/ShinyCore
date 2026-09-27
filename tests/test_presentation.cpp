#include "shiny/core.h"
#include "shiny/projectiles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>

#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"presentation:%d: %s\n",__LINE__,#c); std::exit(1); } } while(false)
bool near(float a,float b) { return std::abs(a-b)<.002f; }
int main() {
    auto owner=std::make_unique<ScWorld>(); auto& w=*owner;
    w.map.bounded=false; w.gravity=0; w.particles.configure(4);
    w.projectiles.reset(new ScProjectiles(4));
    CHECK(!w.presentation&&w.particles.previous_display.empty()&&w.projectiles->previous_display.empty());
    ScEntity spec; spec.x=10; spec.y=20; spec.w=8; spec.h=8; spec.angle=3.1f;
    const auto id=sc_spawn(&w,&spec); CHECK(id);
    sc_presentation_configure(w,true);
    CHECK(w.presentation->entities.size()==w.entities.size()&&w.particles.previous_display.size()==4);
    const auto* storage=w.presentation->entities.data(); const auto* particles=w.particles.previous_display.data();
    w.camera.pixel_snap=false; w.camera.rotation=3.1f;
    sc_presentation_capture(w);
    auto& e=*sc_entity(&w,id); e.x=30; e.y=40; e.angle=-3.1f;
    w.camera_x=20; w.camera.zoom=2; w.camera.rotation=-3.1f;
    sc_step(&w);
    const auto hash=sc_state_hash(&w); const auto rng=w.rng,visual=w.visual_rng;
    auto pose=sc_display_pose(w,e,.5f);
    CHECK(near(pose.x,20)&&near(pose.y,30)&&near(pose.angle,std::numbers::pi_v<float>));
    CHECK(near(sc_display_pose(w,e,0).x,10)&&near(sc_display_pose(w,e,1).x,30));
    auto camera=sc_display_camera(w,.5f);
    CHECK(near(camera.center.x,static_cast<float>(w.view_width)*.5f+10)&&near(camera.zoom,1.5f));
    CHECK(near(camera.rotation,std::numbers::pi_v<float>));
    const auto point=sc_camera_to_world(camera,sc_camera_to_screen(camera,{10,20})); CHECK(near(point.x,10)&&near(point.y,20));
    CHECK(w.rng==rng&&w.visual_rng==visual&&sc_state_hash(&w)==hash);
    sc_presentation_capture(w); e.x=500; w.camera_x=400;
    sc_presentation_snap(w,id); sc_presentation_snap_camera(w); sc_step(&w);
    CHECK(near(sc_display_pose(w,e,0).x,500)&&near(sc_display_camera_anchor(w,0).x,400));
    sc_presentation_capture(w); CHECK(sc_destroy(&w,id)); spec.x=900;
    const auto replacement=sc_spawn(&w,&spec); CHECK(replacement!=id); sc_step(&w);
    CHECK(near(sc_display_pose(w,*sc_entity(&w,replacement),0).x,900));
    sc_presentation_configure(w,false);
    CHECK(!sc_presentation_ready(w)&&w.presentation->entities.data()==storage);
    sc_presentation_configure(w,true); CHECK(!sc_presentation_ready(w));

    ScProjectileSpec bullet; bullet.terrain=false; bullet.mask=0; bullet.vx=120;
    bullet.x=0; bullet.life=.001f; w.projectiles->spawn_batch(std::span{&bullet,1});
    bullet.x=10; bullet.life=1; w.projectiles->spawn_batch(std::span{&bullet,1});
    CHECK(w.particles.emit(w.visual_rng,0,0,2,0xffffffff,0,1));
    w.particles.life[0]=.001f; w.particles.x[1]=10; w.particles.vx[1]=120;
    sc_presentation_capture(w);
    bullet.x=40; const auto newborn=w.projectiles->spawn_batch(std::span{&bullet,1});
    CHECK(w.particles.emit(w.visual_rng,40,0,1,0xffffffff,0,1)); w.particles.vx[2]=120;
    sc_step(&w);
    CHECK(w.projectiles->count==2&&w.particles.count==2);
    for(std::size_t i=0;i<w.projectiles->count;++i) {
        const auto p=sc_display_point(w,w.projectiles->previous_display[i],w.projectiles->x[i],w.projectiles->y[i],.5f);
        CHECK(near(p.x,w.projectiles->ids[i]==newborn?42.f:11.f));
    }
    CHECK(near(sc_display_point(w,w.particles.previous_display[0],w.particles.x[0],w.particles.y[0],.5f).x,11));
    CHECK(near(sc_display_point(w,w.particles.previous_display[1],w.particles.x[1],w.particles.y[1],.5f).x,42));
    CHECK(w.presentation->entities.data()==storage&&w.particles.previous_display.data()==particles);
    sc_world_init(&w,42); CHECK(!w.presentation&&w.particles.previous_display.empty());
    std::puts("presentation: isolated poses, cuts, handles, batch compaction and room reset passed");
}
