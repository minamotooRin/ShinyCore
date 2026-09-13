#include "shiny/script.h"

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
        " sc.message('hello'); sc.camera(1,2);"
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
        "return {init=function() for i=1,257 do sc.spawn({}) end end}", "capacity exhausted");
    CHECK(open_source(script, world, "return {update=function() while true do end end}"));
    CHECK(!sc_script_update(script)); CHECK(strstr(script->error, "instruction budget exceeded"));
    CHECK(strstr(script->error, "main.lua")); sc_script_close(script);
    CHECK(open_source(script, world, "return {draw=function() sc.spawn({}) end}"));
    CHECK(!sc_script_draw(script, 0)); CHECK(strstr(script->error, "forbidden in draw")); sc_script_close(script);
    CHECK(open_source(script, world,
        "return {draw=function() for i=1,513 do sc.rect(0,0,1,1,'#ffffff') end end}"));
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

int main(void) {
    snprintf(project, sizeof project, "tests/.script-test-%ld", static_cast<long>(test_pid()));
    CHECK(test_mkdir(project) == 0);
    auto world = std::make_unique<ScWorld>();
    ScScript script;
    test_paths(); test_scene_validation(&script, world.get()); test_api(&script, world.get()); test_limits(&script, world.get());
    test_ownership(world.get());
    char path[SC_PATH_MAX + 16];
    snprintf(path, sizeof path, "%s/main.lua", project);
    CHECK(remove(path) == 0); CHECK(test_rmdir(project) == 0);
    puts("script: configuration, API, sandbox limits, and callback contracts passed");
    return 0;
}
