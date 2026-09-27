#include "shiny/script.h"
#include "../src/render/draw_order.h"
#include "../src/render/image_grid.h"
#include <lua.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define test_pid _getpid
#define test_mkdir(path) _mkdir(path)
#define test_rmdir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define test_pid getpid
#define test_mkdir(path) mkdir(path, 0700)
#define test_rmdir(path) rmdir(path)
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

static char project[SC_PATH_MAX];

static_assert(std::is_default_constructible_v<ScScript>);
static_assert(!std::is_copy_constructible_v<ScScript>);
static_assert(!std::is_move_constructible_v<ScScript>);
static_assert(std::is_nothrow_destructible_v<ScScript>);

static void fixture(const char *source) {
    char path[SC_PATH_MAX + 16];
    snprintf(path, sizeof path, "%s/main.lua", project);
    FILE *file = fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(fwrite(source, 1, strlen(source), file) == strlen(source));
    CHECK(fclose(file) == 0);
}

static bool open_source(ScScript *script, ScWorld *world, const char *source) {
    fixture(source);
    sc_world_init(world, 42);
    return sc_script_open(script, world, project, "main.lua");
}

static void expect_bad(ScScript *script, ScWorld *world, const char *source, const char *message) {
    CHECK(!open_source(script, world, source));
    if (!strstr(script->error, message)) {
        fprintf(stderr, "Expected '%s', got: %s\n", message, script->error); exit(1);
    }
    CHECK(script->lua == nullptr);
    CHECK(script->memory_used == 0);
}

static void test_paths(void) {
    CHECK(sc_script_validate_path("main.lua"));
    CHECK(sc_script_validate_path("rooms/quiet-well.lua"));
    CHECK(sc_script_validate_path("assets/picture.v2.png"));
    const char *bad[] = {nullptr, "", "/tmp/main.lua", "../main.lua", "rooms/../main.lua", "./main.lua",
                         "rooms//main.lua", "rooms/", "C:/main.lua", "C:\\main.lua", "x\ny.lua", "a/./b.lua"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; ++i) CHECK(!sc_script_validate_path(bad[i]));
}

static void test_scene_validation(ScScript *script, ScWorld *world) {
    expect_bad(script, world, "return 4", "scene must return a table");
    expect_bad(script, world, "return {widht=384}", "unknown scene field 'widht'");
    expect_bad(script, world, "return {width=63}", "expected integer");
    expect_bad(script, world, "return {height=100.5}", "expected integer");
    expect_bad(script, world, "return {seed=99}", "unknown scene field 'seed'");
    expect_bad(script, world, "return {ambient=0/0}", "outside allowed range");
    expect_bad(script, world, "return {map={rows={'..','...'}}}", "equal byte length");
    expect_bad(script, world, "return {map={rows={'x'}}}", "permits only");
    expect_bad(script, world, "return {map={rows={}}}", "at least one row");
    expect_bad(script, world, "return {map={rows={'.'},tile_szie=4}}", "unknown map field");
    expect_bad(script, world, "return {entities={{w=-1}}}", "outside allowed range");
    expect_bad(script, world, "return {entities={{grounded=true}}}", "unknown entity field");
    expect_bad(script, world, "return {entities={{sprite='../asset.png'}}}", "project-relative");
    expect_bad(script, world, "return {entities={{frame_w=8}}}", "both be positive");
    expect_bad(script, world, "return {entities={{color='#abcdxx'}}}", "hexadecimal");
    expect_bad(script, world, "return {entities={[1]={},[3]={}}}", "dense array");
    expect_bad(script, world, "return {update=false}", "must be a function");
    expect_bad(script, world, "return setmetatable({}, {__index={width=320}})", "plain data table");
    expect_bad(script, world, "return {['width'..string.char(0)..'bad']=384}", "NUL byte");
    CHECK(open_source(script, world, "return {}"));
    CHECK(world->rng == 42);
    CHECK(world->view_width == 384 && world->view_height == 216);
    CHECK(sc_script_update(script)); CHECK(sc_script_draw(script, 0.5f));
    sc_script_close(script); CHECK(script->memory_used == 0);
}

static void test_api(ScScript *script, ScWorld *world) {
    CHECK(open_source(script, world,
        "local id; return {title='Api test', width=320, height=180, gravity=200, ambient=.5, "
        "map={tile_size=8,rows={'....','..=.','####'},color='#123456'},"
        "entities={{tag='initial',x=4,y=8,dynamic=true,color='#ff001188'}},"
        "init=function(...) "
        " assert(select('#',...)==0); assert(io==nil and os==nil and package==nil and debug==nil);"
        " assert(load==nil and dofile==nil and loadfile==nil and type(require)=='function');"
        " assert(math.random==nil and math.randomseed==nil);"
        " id=sc.find('initial'); assert(id and sc.find('missing')==nil);"
        " local e=sc.get(id); assert(e.color=='#ff001188' and e.gravity==1 and e.w==8);"
        " e.x=300; assert(sc.get(id).x==4);"
        " assert(not pcall(sc.set,id,{x=30,garbage=2})); assert(sc.get(id).x==4);"
        " assert(not pcall(sc.set,id,{x=30,w=-1})); assert(sc.get(id).x==4);"
        " assert(not pcall(sc.set,id,{id=id}));"
        " assert(not pcall(sc.set,id,setmetatable({}, {__index=function() sc.destroy(id) end})));"
        " assert(sc.get(id).x==4);"
        " local other=sc.spawn({x=5,y=9}); assert(sc.overlap(id,other));"
        " assert(sc.destroy(other)); assert(not pcall(sc.get,other));"
        " assert(not pcall(sc.destroy,other)); assert(not pcall(sc.down,'jupm'));"
        " assert(sc.tile(2,1)=='=' and sc.tile(-1,0)=='#');"
        " assert(sc.tile(1,0,'#')=='#'); assert(not pcall(sc.tile,9,0,'#'));"
        " assert(not pcall(sc.scene,'../outside.lua')); sc.scene('rooms/next.lua');"
        " sc.message('hello'); sc.camera.set{x=1,y=2};"
        " sc.emit(1,2,4,'#ffffffff',3,.4); sc.tone(440,.1,.3);"
        " local n=sc.random(1,6); assert(math.type(n)=='integer' and n>=1 and n<=6);"
        " assert(sc.random(3,3)==3); local f=sc.random(); assert(f>=0 and f<1);"
        " assert(not pcall(sc.random,10,1)); assert(not pcall(sc.rect,1,2,3,4,'#ffffff'));"
        "end,"
        "update=function(dt) assert(math.abs(dt-1/60)<1e-8); "
        " assert(sc.tick()==0 and sc.time()==0); assert(sc.down('right') and sc.pressed('right'));"
        " sc.set(id,{vx=60,vy=-10,glow=24}); end,"
        "draw=function(alpha) assert(alpha==.5);"
        " assert(not pcall(sc.set,id,{x=100})); assert(not pcall(sc.random));"
        " sc.rect(1,2,3,4,'#ffffff',true); sc.circle(5,6,7,'#abcdef');"
        " sc.text('test',8,9,10,'#00ff00'); end}"));
    CHECK(world->view_width == 320 && world->view_height == 180);
    CHECK(world->map.color == 0x123456ffu && world->map.width == 4 && world->map.height == 3);
    CHECK(world->map.tiles[1] == '#');
    CHECK(world->tone_count == 1 && fabsf(world->tones[0].frequency - 440) < 0.001f);
    CHECK(!strcmp(script->pending_scene, "rooms/next.lua"));
    CHECK(!strcmp(world->message, "hello"));
    sc_input(world, SC_RIGHT);
    CHECK(sc_script_update(script));
    ScEntity *entity = sc_entity(world, sc_find(world, "initial"));
    CHECK(entity && entity->vx == 60 && entity->glow == 24);
    CHECK(sc_script_draw(script, .5f));
    CHECK(entity->x == 4 && world->draw_count == 3);
    CHECK(world->draws[0].screen && world->draws[0].kind == SC_DRAW_RECT);
    CHECK(world->draws[1].w == 7 && world->draws[2].h == 10);
    CHECK(sc_script_draw(script, .5f)); CHECK(world->draw_count == 3);
    CHECK(!sc_script_draw(script, NAN));
    sc_script_close(script); CHECK(script->memory_used == 0);
}

static void test_limits(ScScript *script, ScWorld *world) {
    expect_bad(script, world, "while true do end", "instruction budget exceeded");
    expect_bad(script, world, "return {init=function() while true do end end}", "instruction budget exceeded");
    expect_bad(script, world,
        "while true do pcall(function() while true do end end) end", "instruction budget exceeded");
    expect_bad(script, world,
        "while true do xpcall(function() while true do end end, function(e) return e end) end",
        "instruction budget exceeded");
    expect_bad(script, world, "local s=string.rep('x',20*1024*1024); return {}", "memory");
    expect_bad(script, world,
        "return {init=function() for i=1,4097 do sc.spawn({}) end end}", "capacity exhausted");
    CHECK(open_source(script, world, "return {update=function() while true do end end}"));
    CHECK(!sc_script_update(script)); CHECK(strstr(script->error, "instruction budget exceeded"));
    CHECK(strstr(script->error, "main.lua")); sc_script_close(script);
    CHECK(open_source(script, world, "return {draw=function() sc.spawn({}) end}"));
    CHECK(!sc_script_draw(script, 0)); CHECK(strstr(script->error, "forbidden in draw")); sc_script_close(script);
    CHECK(open_source(script, world,
        "return {draw=function() for i=1,4097 do sc.rect(0,0,1,1,'#ffffff') end end}"));
    CHECK(!sc_script_draw(script, 0)); CHECK(strstr(script->error, "capacity exhausted")); sc_script_close(script);
    expect_bad(script, world,
        "local t=setmetatable({}, {__gc=function() while true do end end}); return {}", "__gc finalizers are disabled");
    CHECK(open_source(script, world,
        "local mt={}; local obj=setmetatable({},mt); mt.__gc=function() while true do end end; "
        "assert(not pcall(setmetatable,obj,mt)); return {}"));
    sc_script_close(script);
    CHECK(script->memory_used == 0);
}

static void test_ownership(ScWorld *world) {
    {
        ScScript scoped;
        CHECK(scoped.lua == nullptr && scoped.memory_used == 0);
        CHECK(open_source(&scoped, world, "return {}"));
        CHECK(scoped.lua != nullptr && scoped.memory_used > 0);
        // Reopen with aliased path arguments: the old VM is released first.
        CHECK(sc_script_open(&scoped, world, scoped.root, scoped.entry));
        CHECK(scoped.lua != nullptr && scoped.memory_used > 0);
        // Destructor owns the successful VM; no explicit close on this path.
    }
    {
        auto scoped = std::make_unique<ScScript>();
        CHECK(open_source(scoped.get(), world, "return {}"));
        CHECK(!sc_script_open(scoped.get(), world, project, "../invalid.lua"));
        CHECK(scoped->lua == nullptr && scoped->memory_used == 0);
        sc_script_close(scoped.get());
        sc_script_close(scoped.get());
    }
}

static void test_batch_storage(ScScript* script,ScWorld* world) {
    CHECK(open_source(script,world,R"(
local ids={}
return {init=function()
    for i=1,2000 do ids[i]=sc.spawn({x=i}) end
end,update=function()
    local edits={}
    for i,id in ipairs(ids) do edits[i]={id=id,patch={y=i}} end
    sc.set_many(edits)
    edits[2000]={id=ids[1],patch={x=-1}}
    assert(not pcall(sc.set_many,edits))
    assert(sc.get(ids[1]).x==1 and sc.get(ids[2000]).y==2000)
end}
    )"));
    script->batch_entities.resize(1);
    auto* storage=script->batch_entities.data();
    auto capacity=script->batch_entities.capacity();
    CHECK(capacity>=world->entities.size());
    CHECK(sc_script_update(script));
    script->batch_seen.set();
    CHECK(sc_script_update(script));
    CHECK(script->batch_entities.data()==storage && script->batch_entities.capacity()==capacity);
}

struct AllocationFailure { lua_Alloc original; void* user; int failures{}; };
static void* fail_large_allocation(void* user,void* memory,size_t old_size,size_t new_size) {
    auto* failure=static_cast<AllocationFailure*>(user);
    if(new_size>=4096&&new_size>old_size) { ++failure->failures; return nullptr; }
    return failure->original(failure->user,memory,old_size,new_size);
}
static void test_projectile_result_allocation(ScScript* script,ScWorld* world) {
    CHECK(open_source(script,world,R"(
batch={}; local spec={terrain=false,mask=0}; for i=1,4096 do batch[i]=spec end
return {init=function() sc.projectiles.configure(4096) end}
)"));
    auto* L=script->lua; const int top=lua_gettop(L);
    auto prepare=[&] {
        lua_getglobal(L,"sc"); lua_getfield(L,-1,"projectiles"); lua_getfield(L,-1,"spawn"); lua_getglobal(L,"batch");
    };
    prepare();
    AllocationFailure failure{}; failure.original=lua_getallocf(L,&failure.user);
    auto* storage=script->projectile_batch.data();
    lua_setallocf(L,fail_large_allocation,&failure);
    const int result=lua_pcall(L,1,1,0);
    lua_setallocf(L,failure.original,failure.user);
    CHECK(result==LUA_ERRMEM&&failure.failures>0);
    CHECK(world->projectiles->count==0&&world->projectiles->next_id==1);
    CHECK(script->projectile_batch.data()==storage);
    lua_settop(L,top); prepare();
    CHECK(lua_pcall(L,1,1,0)==LUA_OK&&lua_rawlen(L,-1)==4096);
    CHECK(world->projectiles->count==4096&&world->projectiles->next_id==4097);
    CHECK(script->projectile_batch.data()==storage);
    lua_settop(L,top); sc_script_close(script);
    CHECK(script->memory_used==0&&script->projectile_batch.capacity()==0);
}

static void test_ui_callback(ScScript* script,ScWorld* world) {
    CHECK(open_source(script,world,R"(
local calls=0
return {ui_update=function(dt)
    assert(dt==.125)
    assert(sc.input.key_pressed('a'))
    assert(not pcall(sc.spawn,{}))
    assert(not pcall(sc.random))
    assert(not pcall(sc.state.set,'bad',1))
    assert(not pcall(sc.rect,0,0,2,2,'#FFFFFF'))
    sc.input.focus_text(10,20)
    sc.input.clipboard('UI copy')
    calls=calls+1
end, update=function() assert(calls==2) end}
)"));
    ScDeviceInput input; input.keys.set(65); input.key_pressed.set(65);
    world->input.keys.set(66); world->held=4; world->pressed=2; world->released=1;
    const auto tick=world->tick;
    const auto rng=world->rng;
    CHECK(sc_script_ui_update(script,.125f,input));
    CHECK(sc_script_ui_update(script,.125f,input));
    CHECK(world->input.keys.test(66)&&!world->input.keys.test(65));
    CHECK(world->held==4&&world->pressed==2&&world->released==1);
    CHECK(world->tick==tick&&world->rng==rng&&world->text_focus&&world->clipboard_write);
    CHECK(sc_script_update(script));
    CHECK(world->text_focus); // Gameplay preserves the preceding UI callback's text focus.
    CHECK(!sc_script_ui_update(script,-1,input));
    CHECK(!sc_script_ui_update(script,.1f,input)); // Assertion fails; input must still restore.
    CHECK(script->phase==0&&world->input.keys.test(66));
    CHECK(open_source(script,world,"return {}"));
    world->text_focus=true;
    CHECK(sc_script_ui_update(script,.125f,input)&&world->text_focus);
    CHECK(open_source(script,world,"return {ui_update=function() while true do end end}"));
    CHECK(!sc_script_ui_update(script,.125f,input));
    CHECK(script->phase==0&&std::strstr(script->error,"instruction"));
}

static void test_image_regions(ScScript* script,ScWorld* world) {
    CHECK(open_source(script,world,R"(
return {draw=function()
    for _,options in ipairs({{source_w=8},{source_x=1},{source_w=40,source_h=8},
        {flip_x=1},{layer=.5},{layer=32768},{unknown=true},setmetatable({}, {}),
        {slice=false},{slice={left=-1}},{slice={left=0/0}},{slice={right=math.huge}},
        {slice={left=16,right=16}},{slice={top=16}},{slice={typo=1}},
        {slice=setmetatable({}, {})},{source_w=8,source_h=8,slice={left=5,right=3}}}) do
        assert(not pcall(sc.image,"atlas",1,2,16,16,options))
    end
    sc.image("atlas",1,2,16,16,{source_x=8,source_y=4,source_w=16,source_h=8,
        flip_x=true,flip_y=true,diagonal=true,color="#12345678",screen=true,layer=-2,
        slice={left=2,right=3,top=1,bottom=4}})
    sc.image("atlas",2,3,4,5,false)
end})"));
    ScResource resource; resource.name="atlas"; resource.type="image"; resource.path="atlas.png";
    resource.image_width=32; resource.image_height=16; world->resources.push_back(resource);
    CHECK(sc_script_draw(script,0)); CHECK(world->draw_count==2);
    const auto& image=world->draws[0];
    CHECK(image.kind==SC_DRAW_IMAGE&&image.source_x==8&&image.source_y==4);
    CHECK(image.source_w==16&&image.source_h==8&&image.flip_x&&image.flip_y&&image.diagonal);
    CHECK(image.color==0x12345678&&image.screen);
    CHECK(image.slice_left==2&&image.slice_right==3&&image.slice_top==1&&image.slice_bottom==4);
    ScDraw panel; panel.w=30; panel.h=20;
    panel.slice_left=2; panel.slice_right=3; panel.slice_top=1; panel.slice_bottom=4;
    auto grid=sc_image_grid(panel,12,10);
    CHECK(grid.x.cells==3&&grid.y.cells==3);
    CHECK(grid.x.position[1]==2&&grid.x.position[2]==27&&grid.y.position[1]==1&&grid.y.position[2]==16);
    CHECK(std::abs(grid.x.uv[1]-1.f/6)<1e-6f&&grid.x.uv[2]==.75f);
    panel.w=3; panel.h=2; grid=sc_image_grid(panel,12,10);
    CHECK(std::abs(grid.x.position[1]-1.2f)<1e-6f&&std::abs(grid.x.position[2]-1.2f)<1e-6f);
    CHECK(std::abs(grid.y.position[1]-.4f)<1e-6f&&std::abs(grid.y.position[2]-.4f)<1e-6f);
    panel.w=30; panel.h=20; panel.diagonal=true; panel.flip_x=true; grid=sc_image_grid(panel,12,10);
    CHECK(grid.x.position[1]==4&&grid.x.position[2]==29&&grid.y.position[1]==2&&grid.y.position[2]==17);
    CHECK(std::abs(grid.x.uv[1]-.4f)<1e-6f&&std::abs(grid.y.uv[1]-1.f/6)<1e-6f);
    grid=sc_image_grid(ScDraw{},12,10); CHECK(grid.x.cells==1&&grid.y.cells==1);

    CHECK(image.layered&&image.layer==-2);
    CHECK(world->draws[1].source_w==0&&!world->draws[1].screen&&world->draws[1].color==0xffffffff);
    world->entities[0].alive=world->entities[1].alive=true;
    world->entities[0].layer=0; world->entities[1].layer=2;
    world->layers.resize(2); world->layers[0].order=-1; world->layers[1].order=2;
    world->draws[2]=world->draws[3]=image;
    world->draws[2].layer=world->draws[3].layer=0; world->draw_count=4;
    std::array<ScSceneItem,7> order{};
    auto count=sc_scene_order(*world,order); CHECK(count&&*count==7);
    CHECK(order[0].kind==ScSceneKind::image&&order[0].index==0);
    CHECK(order[1].kind==ScSceneKind::map&&order[1].index==0);
    CHECK(order[2].kind==ScSceneKind::image&&order[2].index==2);
    CHECK(order[3].kind==ScSceneKind::image&&order[3].index==3);
    CHECK(order[4].kind==ScSceneKind::entity&&order[4].index==0);
    CHECK(order[5].kind==ScSceneKind::map&&order[6].kind==ScSceneKind::entity);
    CHECK(!sc_scene_order(*world,std::span{order}.first(6)));
    sc_script_close(script);
    CHECK(open_source(script,world,"return {draw=function() sc.clip(0,0,20,20); sc.image('atlas',0,0,8,8,{layer=0}); sc.clip() end}"));
    world->resources.push_back(resource);
    CHECK(!sc_script_draw(script,0)); CHECK(std::strstr(script->error,"cannot be nested inside UI clips"));
    sc_script_close(script);
}

static void test_presentation(ScScript* script,ScWorld* world) {
    CHECK(open_source(script,world,R"(
local id,step=0,0
local function near(a,b) assert(math.abs(a-b)<.002,tostring(a).." != "..tostring(b)) end
return {gravity=0,init=function()
    assert(not sc.presentation.stats().reserved)
    assert(not pcall(sc.presentation.interpolate,1))
    sc.presentation.interpolate(true)
    sc.projectiles.configure(4) -- Reserving presentation before the optional batch pool is supported.
    assert(sc.presentation.stats().projectiles==4)
    id=sc.spawn{x=10,y=20,w=8,h=8}
    sc.camera.set{bounds=false,pixel_snap=false}
end,update=function()
    step=step+1
    if step==1 then sc.set(id,{x=30});sc.camera.set{x=20}
    elseif step==2 then
        sc.set(id,{x=300});sc.presentation.snap(id)
        sc.camera.set{x=200};sc.presentation.snap_camera()
    elseif step==3 then sc.presentation.interpolate(false);sc.set(id,{x=400})
    end
    near(sc.presentation.pose(id).x,sc.get(id).x)
end,draw=function(alpha)
    assert(not pcall(sc.presentation.interpolate,false))
    assert(not pcall(sc.presentation.snap,id))
    assert(not pcall(sc.presentation.snap_camera))
    local p=sc.presentation.pose(id);local c=sc.camera.read()
    if step==1 then
        near(p.x,10+20*alpha);near(sc.get(id).x,30)
        near(c.anchor_x,20*alpha);near(c.x,20)
        local screen=sc.camera.to_screen(p.x,p.y)
        near(screen.x,10);near(screen.y,20)
        local back=sc.camera.to_world(screen.x,screen.y);near(back.x,p.x)
    elseif step==2 then near(p.x,300);near(c.anchor_x,200)
    elseif step==3 then near(p.x,400);assert(not sc.presentation.stats().enabled)
    end
end})"));
    for(int tick=0;tick<3;++tick) {
        CHECK(sc_script_update(script)); sc_step(world);
        const auto hash=sc_state_hash(world);
        for(float alpha:{0.f,.25f,.5f,1.f}) {
            if(!sc_script_draw(script,alpha)) { std::fprintf(stderr,"%s\n",script->error); CHECK(false); }
            CHECK(sc_state_hash(world)==hash);
        }
    }
    CHECK(open_source(script,world,R"(return {update=function()
        assert(not sc.presentation.stats().reserved)
        assert(not pcall(sc.presentation.interpolate,true))
        sc.presentation.interpolate(false)
        assert(not pcall(sc.presentation.pose,1))
        assert(not pcall(sc.presentation.snap,1))
    end})"));
    CHECK(sc_script_update(script));
}

static void test_navigation_lifetime() {
    auto world=std::make_unique<ScWorld>(); ScScript script;
    CHECK(open_source(&script,world.get(),R"(local step,field=0,nil
return {init=function() sc.navigation.region(-16,-16,{"...."},8) end,
update=function()
    step=step+1
    if step==1 then field=sc.navigation.flow(3,0)
    else
        assert(not pcall(sc.navigation.flow,3,0))
        assert(select(3,sc.navigation.direction(field,-12,-12))=='ok')
        sc.navigation.region()
        assert(not pcall(sc.navigation.direction,field,-12,-12))
        assert(not pcall(sc.navigation.flow,0,0))
    end
end})"));
    script.flow_generations[0]=0x07fffffeu;
    CHECK(sc_script_update(&script)); CHECK(script.flow_generations[0]==0x07ffffffu);
    CHECK(sc_script_update(&script)); CHECK(script.flow_generations[0]==0x07ffffffu);
    CHECK(open_source(&script,world.get(),R"(return {init=function()
        sc.navigation.region(0,0,{".."},8)
    end})"));
    CHECK(script.navigation_region!=nullptr);
    CHECK(open_source(&script,world.get(),"return {}"));
    CHECK(script.navigation_region==nullptr);
}

int main(int argc,char** argv) {
    const bool navigation_only=argc==2&&!std::strcmp(argv[1],"--navigation");
    const bool images_only=argc==2&&!std::strcmp(argv[1],"--images");
    const bool ui_only=argc==2&&!std::strcmp(argv[1],"--ui");
    CHECK(argc==1||navigation_only||ui_only||images_only);
    snprintf(project, sizeof project, "tests/.script-test-%ld", static_cast<long>(test_pid()));
    CHECK(test_mkdir(project) == 0);
    auto world = std::make_unique<ScWorld>();
    ScScript script;
    if(!ui_only&&!images_only) test_navigation_lifetime();
    if(ui_only) test_ui_callback(&script,world.get());
    if(images_only) test_image_regions(&script,world.get());
    if(!navigation_only&&!ui_only&&!images_only) {
    test_presentation(&script,world.get());
    test_image_regions(&script,world.get());
    test_paths(); test_scene_validation(&script, world.get()); test_api(&script, world.get()); test_limits(&script, world.get());
    test_ownership(world.get());
    test_batch_storage(&script,world.get());
    test_projectile_result_allocation(&script,world.get());
    test_ui_callback(&script,world.get());
    }
    char path[SC_PATH_MAX + 16];
    snprintf(path, sizeof path, "%s/main.lua", project);
    CHECK(remove(path) == 0); CHECK(test_rmdir(project) == 0);
    puts(images_only ? "script: image regions, nine-slice geometry and atomic argument rejection passed" :
         ui_only ? "script: UI callback phases, input restoration, text focus and instruction budget passed" :
         navigation_only ? "script: navigation generation exhaustion and VM lifetime passed" :
         "script: configuration, API, sandbox limits, and callback contracts passed");
    return 0;
}
