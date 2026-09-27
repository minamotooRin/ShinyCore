#include "shiny/core.h"
#include "shiny/navigation.h"
#include "shiny/projectiles.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
int failures=0;
void check(bool value,const char* message) { if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); ++failures; } }
void navigation_clearance() {
    ScMap map; map.width=9; map.height=7; map.tile_size=8;
    for(int y=0;y<7;++y) if(y!=3) map.navigation_blocked.set(static_cast<std::size_t>(y*9+4));
    const int start=3*9+2,goal=3*9+6;
    check(sc_path(map,start,goal,100,4).status=="ok","circle exactly fitting doorway may pass");
    check(sc_path(map,start,goal,100,4.25f).status=="unreachable","larger body cannot cross narrow doorway");
    check(sc_path(map,0,goal,100,4.25f).visited==0,"radius also excludes map boundaries");
    ScFlowField field; field.build(map,goal,100,6);
    check(field.status=="ok"&&field.direction(map,20,28)==std::pair{0.f,0.f},"shared field excludes disconnected body-sized start");
    map.navigation_blocked.reset(2*9+4); map.navigation_blocked.reset(4*9+4); ++map.navigation_revision;
    check(field.state(map)=="stale","terrain change invalidates body-sized field");
    do { field.refresh(map,1); } while(field.status=="budget_exhausted");
    check(field.status=="ok"&&field.direction(map,20,28).first>0,"refresh retains radius and reopens wide door");
    ScMap resized; resized.width=5; resized.height=3; resized.tile_size=8;
    for(int x=0;x<5;++x) { resized.navigation_blocked.set(x); resized.navigation_blocked.set(10+x); }
    ScFlowField scaled; scaled.build(resized,9,100,5);
    check(scaled.status=="unreachable","small cells exclude a body from the corridor");
    resized.tile_size=16; // Equal grid dimensions and passability revision, different clearance.
    check(scaled.state(resized)=="stale"&&scaled.direction(resized,8,24)==std::pair{0.f,0.f},
        "cell-size changes must not publish the old clearance field");
    scaled.refresh(resized,100);
    check(scaled.status=="ok"&&scaled.direction(resized,8,24).first>0,
        "refresh recomputes body clearance after a cell-size change");
    resized.width=4;
    check(scaled.state(resized)=="stale","different grid dimensions cannot reuse a field");
    resized.width=5;
    // Independent circle/rectangle oracle, including diagonal corners and non-square maps.
    for(float radius:{0.f,4.f,5.f,6.f,11.f,4096.f}) for(int cell=0;cell<63;++cell) {
        const double x=(cell%9+.5)*8,y=(cell/9+.5)*8;
        bool fits=std::min({x,y,72-x,56-y})>=radius&&!map.navigation_blocked[static_cast<std::size_t>(cell)];
        for(int wall=0;wall<63;++wall) if(map.navigation_blocked[static_cast<std::size_t>(wall)]) {
            const double left=wall%9*8,top=wall/9*8;
            const double dx=x-std::clamp(x,left,left+8),dy=y-std::clamp(y,top,top+8);
            if(std::hypot(dx,dy)<radius) fits=false;
        }
        check((sc_path(map,cell,cell,1,radius).status=="ok")==fits,"clearance agrees with circle/rectangle geometry");
    }
    for(float radius:{-1.f,4097.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        try { (void)sc_path(map,start,goal,100,radius); check(false,"invalid path radius rejected"); } catch(const std::invalid_argument&) {}
        try { field.build(map,goal,100,radius); check(false,"invalid flow radius rejected"); } catch(const std::invalid_argument&) {}
        check(field.status=="ok"&&field.direction(map,20,28).first>0,"invalid radius retains native field");
    }
}
void navigation_geometry() {
    ScMap map; map.width=map.height=4; map.tile_size=16;
    ScTerrainShape rectangle; rectangle.x=16; rectangle.w=rectangle.h=16;
    auto blocked=sc_navigation_obstacles(map,std::span{&rectangle,1});
    check(blocked.count()==1&&blocked[1],"aligned rectangle does not block neighboring cells");
    rectangle.x=24; blocked=sc_navigation_obstacles(map,std::span{&rectangle,1});
    check(blocked.count()==2&&blocked[1]&&blocked[2],"pixel offset covers both overlapped cells");
    ScTerrainShape triangle; triangle.vertex_count=3; triangle.vertices={0,0,32,0,0,32};
    blocked=sc_navigation_obstacles(map,std::span{&triangle,1});
    check(blocked.count()==3&&blocked[0]&&blocked[1]&&blocked[4]&&!blocked[5],"SAT excludes empty triangle AABB corner");
    triangle.vertices={32,0,32,32,0,32};
    blocked=sc_navigation_obstacles(map,std::span{&triangle,1});
    check(blocked.count()==3&&!blocked[0]&&blocked[1]&&blocked[4]&&blocked[5],"flipped polygon uses transformed geometry");
    rectangle.x=16; rectangle.y=15.5f; rectangle.one_way=true;
    blocked=sc_navigation_obstacles(map,std::span{&rectangle,1});
    check(blocked.count()==2&&blocked[1]&&blocked[5],"one-way geometry uses one-pixel thickness");
    rectangle.one_way=false; rectangle.x=-8; rectangle.y=0;
    blocked=sc_navigation_obstacles(map,std::span{&rectangle,1});
    check(blocked.count()==1&&blocked[0],"negative geometry clips to grid");
    rectangle.x=1e6; check(sc_navigation_obstacles(map,std::span{&rectangle,1}).none(),"off-map geometry clips before indexing");
    triangle.vertices[0]=std::numeric_limits<float>::infinity();
    try { (void)sc_navigation_obstacles(map,std::span{&triangle,1}); check(false,"nonfinite raster input rejected"); }
    catch(const std::invalid_argument&) {}
}
void navigation_dirty() {
    ScMap map; map.width=64; map.height=8; map.tile_size=8;
    std::vector<ScTerrainShape> before{{0,0,8,8},{248,0,8,8},{504,0,8,8}};
    map.navigation_blocked=sc_navigation_obstacles(map,before);
    auto after=before; after[0].y=8; after[2].y=8;
    auto patch=sc_navigation_patch(map,before,after);
    check(patch.dirty.count()==4&&patch.dirty[0]&&patch.dirty[63]&&patch.dirty[64]&&patch.dirty[127],
        "separated edits exclude unchanged middle cells");
    check(patch.blocked==sc_navigation_obstacles(map,after),"local mask agrees with full rebuild");
    check(map.navigation_blocked[0]&&!map.navigation_blocked[64],"patch preparation does not mutate the map");
    after=before; std::reverse(after.begin(),after.end());
    check(sc_navigation_patch(map,before,after).dirty.none(),"geometry order alone is not a dirty update");
    before.push_back(before[0]); map.navigation_blocked=sc_navigation_obstacles(map,before);
    after=before; after.erase(after.begin()); patch=sc_navigation_patch(map,before,after);
    check(patch.dirty.count()==1&&patch.blocked[0],"remaining overlapping contributor retains blocked cell");
    after[0].x=-800;
    // Region masks must be prepared at the same origin before incremental updates.
    map.navigation_blocked=sc_navigation_obstacles(map,before,-16,-8);
    patch=sc_navigation_patch(map,before,after,-16,-8);
    check(patch.blocked==sc_navigation_obstacles(map,after,-16,-8),"shifted region clips old and new shapes independently");
    ScTerrainShape triangle; triangle.x=40; triangle.y=16; triangle.vertex_count=3;
    triangle.vertices={0,0,16,0,0,16};
    before={triangle,{120,23.5f,16,8,true},{400,40,8,8}};
    for(int i=0;i<24;++i) {
        map.navigation_blocked=sc_navigation_obstacles(map,before,-8,-8);
        after=before;
        after[0].x+=float(i%3-1)*8; after[0].vertices[2]=i%2?8.f:16.f;
        after[1].y+=float(i%4)*.5f;
        if(i%5==0) after.erase(after.begin()+2);
        patch=sc_navigation_patch(map,before,after,-8,-8);
        check(patch.blocked==sc_navigation_obstacles(map,after,-8,-8),"polygon/platform/multi-edit patch matches full oracle");
        check(((patch.blocked^map.navigation_blocked)&~patch.dirty).none(),"no mask changes outside dirty cells");
    }
    const auto previous=map.navigation_blocked;
    after[0].vertices[0]=std::numeric_limits<float>::quiet_NaN();
    try { (void)sc_navigation_patch(map,before,after); check(false,"invalid candidates reject before float sorting"); }
    catch(const std::invalid_argument&) {}
    check(map.navigation_blocked==previous,"failed preparation retains the map");
}
void projectile_terrain() {
    ScWorld world; world.map.width=world.map.height=16; world.map.tile_size=16;
    ScProjectiles pool(8);
    ScTerrainShape wall; wall.x=100; wall.y=40; wall.w=4; wall.h=20;
    world.terrain_shapes.push_back(wall); ++world.terrain_revision;
    ScProjectileSpec shot; shot.x=50; shot.y=50; shot.vx=6000; shot.radius=2;
    auto fire=[&] { pool.clear(); pool.spawn(std::span{&shot,1}); pool.step(world); };
    fire(); check(pool.count==0&&pool.hits.size()==1&&pool.hits[0].target==0,"finite terrain blocks a high-speed projectile");
    check(std::fabs(pool.hits[0].fraction-.48f)<.0001f,"rectangle sweep reports distance");
    world.terrain_shapes[0].x=120; ++world.terrain_revision;
    fire(); check(std::fabs(pool.hits[0].fraction-.68f)<.0001f,"terrain revision refreshes projectile grid");
    wall.vertex_count=3; wall.vertices={0,0,20,20,0,20}; world.terrain_shapes[0]=wall; ++world.terrain_revision;
    fire(); check(pool.count==0&&pool.hits.size()==1,"polygon edge blocks sweep");
    shot.x=118; shot.y=42; shot.vx=0; shot.radius=1;
    fire(); check(pool.count==1&&pool.hits.empty(),"empty polygon AABB corner does not collide");
    shot.x=105; shot.y=55;
    fire(); check(pool.count==0&&pool.hits[0].fraction==0,"initial polygon overlap hits immediately");
    shot.x=80; shot.y=20; shot.vx=shot.vy=2400;
    fire(); check(pool.count==0&&std::fabs(pool.hits[0].fraction-float((20-1/std::sqrt(2.0))/40))<.0001f,"sweep includes round vertex contact");
    ScEntity target; target.x=80; target.y=48; target.w=2; target.h=6; target.solid=true;
    auto before=sc_spawn(&world,&target); target.x=140; sc_spawn(&world,&target);
    shot.x=50; shot.y=50; shot.vx=6000; shot.vy=0; shot.radius=2; shot.piercing=true;
    fire(); check(pool.count==0&&pool.hits.size()==2&&pool.hits[0].target==before&&pool.hits[1].target==0,"terrain stops piercing bullets before targets behind it");
    shot.terrain=false; shot.mask=0;
    fire(); check(pool.count==1&&pool.hits.empty(),"terrain flag disables geometry collision");
    world.terrain_shapes.clear(); ++world.terrain_revision;
    world.map.tiles[3*16+6]='='; shot.terrain=true; shot.x=100; shot.y=55; shot.vx=0; shot.radius=1;
    fire(); check(pool.count==1&&pool.hits.empty(),"ASCII one-way platform uses one-pixel geometry");
    wall={}; wall.w=1e7f; wall.h=128;
    world.terrain_shapes.push_back(wall); ++world.terrain_revision;
    try { fire(); check(false,"terrain grid budget must fail explicitly"); }
    catch(const std::runtime_error&) { check(pool.count==1,"grid failure precedes projectile advancement"); }
    world.terrain_shapes.clear(); ++world.terrain_revision;
    fire(); check(pool.count==1&&pool.hits.empty(),"failed terrain cache can be rebuilt");
}
void projectile_sprites() {
    ScProjectiles pool(8);
    ScProjectileSprite style{0,0,0,16,18,32,36};
    auto bad=style; bad.x=-1;
    try { pool.add_sprite(bad); check(false,"negative sprite crop rejected"); }
    catch(const std::invalid_argument&) {}
    check(pool.add_sprite(style)==1,"failed registration does not consume sprite ID");
    std::array<ScProjectileSpec,3> batch{};
    for(auto& p:batch) { p.terrain=false; p.mask=0; }
    batch[0].sprite=1; batch[1].sprite=2;
    try { pool.spawn(batch); check(false,"unregistered sprite rejected"); }
    catch(const std::invalid_argument&) {}
    check(pool.count==0&&pool.next_id==1,"sprite batch failure is atomic");
    batch[1].sprite=0; batch[0].life=.001f; batch[2].sprite=1;
    const auto ids=pool.spawn(batch);
    const auto* storage=pool.draw_order().data();
    ScWorld world; pool.step(world);
    const auto order=pool.draw_order();
    check(order.size()==2&&pool.ids[order[0]]==ids[1]&&pool.ids[order[1]]==ids[2],"swap removal preserves transparent draw order");
    check(pool.sprite[order[0]]==0&&pool.sprite[order[1]]==1,"swap removal retains sprite association");
    check(order.data()==storage&&pool.draw_order().data()==storage,"draw order reuses reserved storage");
    for(int i=1;i<64;++i) pool.add_sprite(style);
    try { pool.add_sprite(style); check(false,"sprite capacity enforced"); }
    catch(const std::runtime_error&) {}
    pool.clear(); check(pool.draw_order().empty()&&pool.sprite_count==64,"clear retains registered styles");
}
void particle_columns() {
    ScParticles pool(4); std::uint32_t rng=123;
    auto* storage=pool.x.data();
    check(pool.emit(rng,10,20,3,0xff8800ff,0,1).has_value(),"particle batch fits configured storage");
    const auto previous_rng=rng;
    check(!pool.emit(rng,0,0,2,0,1,1)&&pool.count==3&&rng==previous_rng,"overflow changes neither columns nor visual RNG");
    pool.x[0]=11; pool.x[1]=22; pool.x[2]=33;
    pool.life[0]=.001f; pool.life[1]=pool.life[2]=1;
    pool.color[1]=1; pool.color[2]=2;
    pool.step(0,SC_DT);
    check(pool.count==2&&pool.x[0]==22&&pool.x[1]==33&&pool.color[0]==1&&pool.color[1]==2,"expiration compacts columns in stable creation order");
    check(pool.emit(rng,44,20,2,3,0,1).has_value()&&pool.x[2]==44&&pool.color[2]==3,"emission reuses compacted capacity");
    check(pool.x.data()==storage,"particle emit and step never replace column storage");
    try { pool.configure(0); check(false,"cannot reconfigure live particles"); }
    catch(const std::logic_error&) {}
    pool.step(0,2); check(pool.count==0,"all expired particles leave empty active prefix");
    pool.configure(0);
    check(pool.capacity()==0&&pool.x.capacity()==0&&pool.color.capacity()==0,"disabled particles release all column storage");
    const auto disabled_rng=rng;
    check(!pool.emit(rng,0,0,1,0,0,1)&&rng==disabled_rng,"disabled system rejects emission without RNG consumption");
    check(pool.emit(rng,0,0,0,0,0,1).has_value(),"zero emission is allowed when disabled");
}
void projectile_broadphase_edges() {
    ScWorld world; ScEntity target; target.x=50; target.y=12; target.w=target.h=4; target.solid=true;
    auto id=sc_spawn(&world,&target); ScProjectiles pool(4);
    ScProjectileSpec shot; shot.x=0; shot.y=10; shot.radius=2; shot.vx=6000; shot.terrain=false;
    pool.spawn(std::span{&shot,1}); pool.step(world);
    check(pool.hits.size()==1&&pool.hits[0].target==id&&std::fabs(pool.hits[0].fraction-.5f)<.0001f,"broadphase preserves exact tangent sweep");
    shot.y=9.99f; pool.spawn(std::span{&shot,1}); pool.step(world);
    check(pool.hits.empty()&&pool.count==1,"nearby parallel sweep outside radius misses");
}
void projectile_candidate_order() {
    ScWorld world; ScEntity target; target.x=130; target.y=20; target.w=target.h=130; target.solid=true;
    const auto far=sc_spawn(&world,&target);
    target.x=60; target.w=20;
    const auto near=sc_spawn(&world,&target),tie=sc_spawn(&world,&target);
    ScProjectiles pool(1); ScProjectileSpec shot;
    shot.x=0; shot.y=30; shot.vx=18000; shot.radius=1; shot.terrain=false; shot.piercing=true;
    pool.spawn(std::span{&shot,1}); pool.step(world);
    check(pool.hits.size()==3&&pool.hits[0].target==near&&pool.hits[1].target==tie&&pool.hits[2].target==far,"multi-cell dedup retains distance and ID hit order");
    pool.clear(); shot.piercing=false;
    for(int i=0;i<8;++i) sc_spawn(&world,&target);
    pool.spawn(std::span{&shot,1}); pool.step(world);
    check(pool.hits.size()==1&&pool.hits[0].target==near,"nonpiercing dense overlap stores only earliest hit, within result capacity");
}
void projectile_cell_index() {
    ScWorld world; ScProjectiles pool(4);
    ScEntity target; target.x=-32; target.y=-32; target.w=target.h=4; target.solid=true;
    const auto near=sc_spawn(&world,&target);
    target.x+=8192*64; // Same hash bucket, distinct cell coordinates.
    const auto far=sc_spawn(&world,&target);
    ScProjectileSpec shot; shot.y=-30; shot.vx=6000; shot.radius=1; shot.terrain=false;
    auto fire=[&](float x,ScEntityId expected) {
        pool.clear(); shot.x=x; pool.spawn(std::span{&shot,1}); pool.step(world);
        check(pool.hits.size()==1&&pool.hits[0].target==expected,"cell lookup preserves collision identity");
    };
    fire(-100,near); fire(target.x-68,far);
    pool.clear(); shot.x=200; pool.spawn(std::span{&shot,1}); pool.step(world);
    check(pool.hits.empty(),"missing cached cell does not alias a target");
    auto* large=sc_entity(&world,near); large->w=large->h=4096;
    fire(-100,near); // 65 x 65 occupied cells exceed the cache's half-load budget.
    large->w=large->h=4;
    fire(target.x-68,far); // Rebuild the cache after the sorted-grid fallback.
}
void particle_emitters() {
    ScParticles pool(4); std::uint32_t rng=99;
    ScParticleEmitter spec;
    spec.speed_min=spec.speed_max=10; spec.life_min=spec.life_max=1;
    spec.angle_min=spec.angle_max=0; spec.gravity=0;
    spec.curve[0]={0,2,0xff0000ff}; spec.curve[1]={1,6,0x0000ff00};
    auto invalid=spec; invalid.curve[1].time=0;
    check(!pool.define(invalid)&&pool.emitter_count()==0,"invalid curve does not consume template slot");
    invalid=spec; invalid.image=129;
    check(!pool.define(invalid),"invalid emitter texture index rejected");
    invalid.image=1; invalid.w=8192; invalid.h=18; invalid.x=1;
    check(!pool.define(invalid),"overflowing native particle region rejected");
    spec.image=0; spec.w=12; spec.h=18; spec.additive=true;
    auto id=pool.define(spec); check(id&&*id==1,"emitter template registration");
    check(pool.definitions()[0].image==0&&pool.definitions()[0].additive,"texture and blend copied into immutable template");
    check(pool.burst(rng,*id,10,20,2).has_value(),"emitter burst fits");
    check(pool.vx[0]==10&&pool.vy[0]==0&&pool.life[0]==1&&pool.size[0]==2,"fixed ranges and first curve key");
    pool.step(100,.5f);
    check(pool.count==2&&pool.x[0]==15&&pool.y[0]==20&&pool.size[0]==4,"emitter gravity override and size interpolation");
    check(pool.color[0]==0x80008080,"RGBA curve interpolates each channel with rounding");
    auto before=rng;
    check(!pool.burst(rng,*id,0,0,3)&&rng==before&&pool.count==2,"failed emitter burst is atomic");
    check(!pool.burst(rng,2,0,0,1)&&rng==before,"unregistered template rejects without random consumption");
    pool.life[0]=.01f; pool.step(0,.02f);
    check(pool.count==1&&pool.emitter[0]==1&&pool.size[0]>4,"compaction preserves emitter curve association");
    pool.step(0,1); check(pool.count==0,"emitter particles expire");
    for(int i=1;i<64;++i) check(pool.define(spec).has_value(),"template capacity available");
    check(!pool.define(spec)&&pool.emitter_count()==64,"template capacity enforced");
}
}
int main(int argc,char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--navigation") {
        navigation_geometry(); navigation_dirty(); navigation_clearance();
        std::printf("navigation: %d failures\n",failures); return failures?1:0;
    }
    {
        ScWorld world; ScProjectiles pool(64);
        for(int frame=0;frame<300;++frame) {
            if(frame%47==0) pool.clear();
            for(int n=0;n<5&&pool.count<64;++n) {
                ScProjectileSpec shot; shot.terrain=false; shot.mask=0;
                shot.life=SC_DT*static_cast<float>(1+(frame*13+n*7)%20);
                pool.spawn(std::span{&shot,1});
            }
            pool.step(world);
            const auto order=pool.draw_order();
            check(order.size()==pool.count,"draw chain includes every survivor after repeated spawn/expiry/clear");
            std::array<bool,64> seen{}; std::uint64_t previous=0;
            for(auto index:order) {
                check(index<pool.count&&!seen[index]&&pool.ids[index]>previous,"draw chain has unique dense slots in creation order");
                seen[index]=true; previous=pool.ids[index];
            }
        }
    }
    projectile_candidate_order();
    projectile_cell_index();
    projectile_broadphase_edges();
    particle_emitters();
    particle_columns();
    projectile_sprites();
    navigation_clearance();
    navigation_geometry(); navigation_dirty();
    projectile_terrain();
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
    world.map.tiles[15]='#';
    auto blocked=sc_path(world.map,0,15,1024);
    check(blocked.status=="unreachable"&&blocked.visited==0&&blocked.cells.empty(),"blocked goal is an unreachable result");
    ScFlowField closed; closed.build(world.map,15,1);
    check(closed.status=="unreachable"&&closed.visited==0,"blocked flow goal is an unreachable field");
    world.map.tiles[15]='.'; closed.invalidate(); closed.refresh(world.map,1024);
    check(closed.status=="ok","initially blocked flow can be rebuilt after reopening");
    ScFlowField flow; flow.build(world.map,15,1024);
    auto direction=flow.direction(world.map,8,8);
    check(direction.first>0&&std::fabs(direction.second)<.001f,"shared flow heads toward goal");
    ScFlowField partial; partial.build(world.map,15,1);
    check(partial.status=="budget_exhausted"&&partial.visited==1,"flow initial budget enforced");
    check(partial.direction(world.map,8,8)==std::pair{0.f,0.f},"incomplete field does not publish directions");
    while(partial.status=="budget_exhausted") {
        auto previous=partial.visited; partial.refresh(world.map,7);
        check(partial.visited>previous&&partial.visited<=previous+7,"refresh performs bounded additional work");
    }
    for(int i=0;i<128;++i)
        check(partial.direction(world.map,float(i%16*16+8),float(i/16*16+8))==
              flow.direction(world.map,float(i%16*16+8),float(i/16*16+8)),"incremental build matches full BFS");
    world.map.tiles[15]='#'; partial.invalidate();
    check(partial.direction(world.map,8,8)==std::pair{0.f,0.f},"stale field cannot move agents");
    partial.refresh(world.map,1); check(partial.status=="unreachable","blocked goal reported");
    world.map.tiles[15]='.'; partial.invalidate(); partial.refresh(world.map,1024);
    check(partial.status=="ok"&&partial.direction(world.map,8,8)==direction,"reopening goal restores field");
    check(flow.direction(world.map,std::numeric_limits<float>::infinity(),0)==std::pair{0.0f,0.0f},"nonfinite flow position rejected before integer conversion");
    world.map.tile_size=0;
    check(flow.direction(world.map,8,8)==std::pair{0.0f,0.0f},"zero tile size cannot divide flow coordinates");
    world.map.tile_size=16;
    world.map.width=std::numeric_limits<int>::max(); world.map.height=2;
    try { sc_path(world.map,0,1,10); check(false,"oversized navigation dimensions must fail"); } catch(const std::invalid_argument&) {}
    world.map.width=world.map.height=16;
    auto rng=world.rng; check(sc_emit(&world,0,0,20,0xffffffff,10,1).has_value(),"particle emission succeeds");
    check(world.rng==rng,"visual effects do not alter gameplay RNG");
    ScInputBuffer input; ScDeviceInput sample; sample.mouse_buttons=1; sample.text[0]='a'; input.push(sample);
    sample={}; sample.mouse_buttons=0; sample.text[0]='b'; input.push(sample); input.consume(&world);
    check(world.input.mouse_pressed==1&&world.input.mouse_released==1,"mouse tap buffered between ticks");
    check(world.input.text[0]=='a'&&world.input.text[1]=='b',"text commits retain order");
    input.consume(&world); check(!world.input.text[0]&&!world.input.mouse_pressed,"transient input consumed once");
    std::printf("systems: %d failures\n",failures); return failures?1:0;
}
