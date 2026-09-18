#include "shiny/core.h"
#include "shiny/navigation.h"
#include "shiny/projectiles.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
int failures=0;
void check(bool value,const char* message) { if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); ++failures; } }
}
int main() {
    ScWorld world; sc_world_init(&world,42);
    world.map.width=world.map.height=16; world.map.tile_size=16;
    ScEntity target{}; target.x=100; target.y=40; target.w=4; target.h=20; target.solid=true; target.category=2;
    auto target_id=sc_spawn(&world,&target);
    check(target_id>UINT32_MAX,"entity ID preserves room generation");
    ScWorld other; other.epoch=2; auto other_id=sc_spawn(&other,&target);
    check(!sc_entity(&other,target_id)&&other_id!=target_id,"old-room handle is invalid");
    check(sc_destroy(&world,target_id)&&!sc_entity(&world,target_id),"destroy invalidates handle");
    target_id=sc_spawn(&world,&target);
    ScProjectiles pool(8);
    ScProjectileSpec bullet; bullet.x=10; bullet.y=50; bullet.vx=12000; bullet.terrain=false; bullet.mask=2;
    auto ids=pool.spawn(std::array{bullet}); pool.step(world);
    check(pool.count==0&&pool.hits.size()==1,"continuous sweep hits thin target");
    check(pool.hits[0].target==target_id&&pool.hits[0].projectile==ids[0],"hit IDs intact");
    bullet.mask=4; pool.spawn(std::array{bullet}); pool.step(world);
    check(pool.count==1&&pool.hits.empty(),"collision mask excludes target");
    auto bad=bullet; bad.radius=-1;
    try { pool.spawn(std::array{bullet,bad}); check(false,"invalid batch must fail"); } catch(const std::invalid_argument&) {}
    check(pool.count==1,"invalid batch did not partially spawn");
    pool.clear(); bullet.mask=2; bullet.piercing=true;
    target.x=140; auto next=sc_spawn(&world,&target); pool.spawn(std::array{bullet}); pool.step(world);
    check(pool.hits.size()==2&&pool.hits[0].target==target_id&&pool.hits[1].target==next,"piercing hits ordered by fraction");
    auto path=sc_path(world.map,0,255,1024);
    check(path.status=="ok"&&path.cells.front()==0&&path.cells.back()==255,"A-star finds route");
    check(sc_path(world.map,0,255,1).status=="budget_exhausted","search budget enforced");
    for(int x=0;x<16;++x) world.map.tiles[static_cast<size_t>(8*16+x)]='#';
    check(sc_path(world.map,0,255,1024).status=="unreachable","wall blocks route");
    ScFlowField flow; flow.build(world.map,15,1024);
    auto direction=flow.direction(world.map,8,8);
    check(direction.first>0&&std::fabs(direction.second)<.001f,"shared flow heads toward goal");
    check(flow.direction(world.map,std::numeric_limits<float>::infinity(),0)==std::pair{0.0f,0.0f},"nonfinite flow position rejected before integer conversion");
    world.map.tile_size=0;
    check(flow.direction(world.map,8,8)==std::pair{0.0f,0.0f},"zero tile size cannot divide flow coordinates");
    world.map.tile_size=16;
    world.map.width=std::numeric_limits<int>::max(); world.map.height=2;
    try { sc_path(world.map,0,1,10); check(false,"oversized navigation dimensions must fail"); } catch(const std::invalid_argument&) {}
    world.map.width=world.map.height=16;
    auto rng=world.rng; sc_emit(&world,0,0,20,0xffffffff,10,1);
    check(world.rng==rng,"visual effects do not alter gameplay RNG");
    ScInputBuffer input; ScDeviceInput sample; sample.mouse_buttons=1; sample.text[0]='a'; input.push(sample);
    sample={}; sample.mouse_buttons=0; sample.text[0]='b'; input.push(sample); input.consume(&world);
    check(world.input.mouse_pressed==1&&world.input.mouse_released==1,"mouse tap buffered between ticks");
    check(world.input.text[0]=='a'&&world.input.text[1]=='b',"text commits retain order");
    input.consume(&world); check(!world.input.text[0]&&!world.input.mouse_pressed,"transient input consumed once");
    std::printf("systems: %d failures\n",failures); return failures?1:0;
}
