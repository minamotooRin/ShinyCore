#include "shiny/core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>

#define CHECK(condition) do { if(!(condition)) { std::fprintf(stderr,"camera:%d: %s\n",__LINE__,#condition); std::exit(1); } } while(false)
bool near(float a,float b) { return std::abs(a-b)<.001f; }
int main() {
    auto owner=std::make_unique<ScWorld>(); auto& w=*owner;
    w.view_width=200; w.view_height=100; w.camera.bounds=ScCameraBounds::none;
    w.camera_x=10.75f; w.camera_y=20.25f;
    auto view=sc_camera_view(w);
    CHECK(near(view.visible.x,10)&&near(view.visible.y,20));
    w.camera.pixel_snap=false; w.camera.zoom=2; w.camera.rotation=std::numbers::pi_v<float>*.5f;
    view=sc_camera_view(w);
    auto screen=sc_camera_to_screen(view,{view.center.x+10,view.center.y});
    CHECK(near(screen.x,100)&&near(screen.y,70));
    CHECK(near(view.visible.w,50)&&near(view.visible.h,100));
    for(auto p:{ScCameraPoint{-500,1000},ScCameraPoint{110.75f,70.25f},ScCameraPoint{0,0}}) {
        const auto result=sc_camera_to_world(view,sc_camera_to_screen(view,p)); CHECK(near(result.x,p.x)&&near(result.y,p.y));
    }
    w.camera.bounds=ScCameraBounds::custom; w.camera.rectangle={-200,-100,400,200};
    w.camera_x=1000; w.camera_y=-1000; sc_camera_step(w,false); view=sc_camera_view(w);
    CHECK(near(view.visible.x+view.visible.w,200)&&near(view.visible.y,-100));
    w.camera.rectangle={0,0,10,10}; sc_camera_step(w,false); view=sc_camera_view(w);
    CHECK(near(view.visible.x,0)&&near(view.visible.y,0));
    w.camera.bounds=ScCameraBounds::map; w.map.width=20; w.map.height=20; w.map.tile_size=16;
    w.camera.zoom=1; w.camera.rotation=0; w.camera_x=1000; w.camera_y=1000; sc_camera_step(w,false);
    CHECK(near(w.camera_x,120)&&near(w.camera_y,220));
    w.map.bounded=false; w.camera_x=-128; w.camera_y=-64; sc_camera_step(w,false);
    CHECK(near(w.camera_x,-128)&&near(w.camera_y,-64));
    ScEntity entity; entity.x=200; entity.y=100; entity.w=20; entity.h=20;
    w.camera_target=sc_spawn(&w,&entity); CHECK(w.camera_target);
    w.camera.smoothing=1; sc_camera_step(w); CHECK(near(w.camera_x,110)&&near(w.camera_y,60));
    const auto id=w.camera_target; CHECK(sc_destroy(&w,id)); w.camera_target=id;
    sc_camera_step(w); CHECK(!w.camera_target);
    const auto rng=w.rng,visual=w.visual_rng; const auto hash=sc_state_hash(&w);
    w.camera.shake_amplitude=12; w.camera.shake_frames=4; w.camera.shake_frame=0;
    const auto shaken=sc_camera_view(w); CHECK(!near(shaken.center.x,w.camera_x+100));
    CHECK(near(shaken.center.x,sc_camera_view(w).center.x));
    for(int i=0;i<4;++i) sc_camera_step(w);
    CHECK(near(sc_camera_view(w).center.x,w.camera_x+100));
    CHECK(w.rng==rng&&w.visual_rng==visual&&sc_state_hash(&w)==hash);
    w.camera.shake_frames=1; w.camera.shake_frame=0; w.camera.shake_pending=true;
    sc_camera_step(w); CHECK(!near(sc_camera_view(w).center.x,w.camera_x+100));
    sc_camera_step(w); CHECK(near(sc_camera_view(w).center.x,w.camera_x+100));
    sc_world_init(&w,1); CHECK(w.camera.zoom==1&&w.camera.rotation==0&&w.camera.shake_frames==0&&w.camera.pixel_snap);
    std::puts("camera transforms, bounds, follow, reset and isolated shake passed");
}
