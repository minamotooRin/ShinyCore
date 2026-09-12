#define _POSIX_C_SOURCE 200809L
#include "shiny/net_lua.h"
#include "shiny/net.h"
#include "shiny/script.h"

#include <lauxlib.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#define test_pid _getpid
#define test_mkdir(path) _mkdir(path)
#define test_rmdir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define test_pid getpid
#define test_mkdir(path) mkdir(path, 0700)
#define test_rmdir(path) rmdir(path)
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

static bool allow_mutation = true;

static int mutation_guard(lua_State *L) {
    if (!allow_mutation) return luaL_error(L, "network mutation forbidden in draw");
    return 0;
}

static int pause_one_ms(lua_State *L) {
    (void)L;
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
#endif
    return 0;
}

static void register_api(lua_State *L) {
    luaL_openlibs(L);
    lua_newtable(L);
    int top = lua_gettop(L);
    sc_net_lua_register(L, mutation_guard);
    CHECK(lua_gettop(L) == top);
    lua_setglobal(L, "sc");
    lua_pushcfunction(L, pause_one_ms);
    lua_setglobal(L, "pause_one_ms");
}

static lua_State *new_vm(void) {
    lua_State *L = luaL_newstate();
    CHECK(L != NULL);
    register_api(L);
    return L;
}

static void run(lua_State *L, const char *source) {
    if (luaL_dostring(L, source) != LUA_OK) {
        fprintf(stderr, "Lua failure: %s\n", lua_tostring(L, -1));
        exit(1);
    }
    CHECK(lua_gettop(L) == 0);
}

static uint16_t read_port(lua_State *L) {
    lua_getglobal(L, "port");
    lua_Integer port = lua_tointeger(L, -1);
    CHECK(port > 0 && port <= UINT16_MAX);
    lua_pop(L, 1);
    return (uint16_t)port;
}

static void check_bind(uint16_t port, bool available) {
    char error[SC_NET_ERROR_MAX];
    ScNet *net = sc_net_host("127.0.0.1", port, 1, error);
    CHECK((net != NULL) == available);
    if (!net) CHECK(error[0] != '\0');
    sc_net_close(net);
}

static void test_arguments_and_limits(void) {
    lua_State *L = new_vm();
    run(L,
        "assert(sc.net.available == true)\n"
        "local function bad(f, ...) assert(not pcall(f, ...)) end\n"
        "bad(sc.net.host); bad(sc.net.host, '127.0.0.1');\n"
        "for _, ip in ipairs({'localhost', '127.1', '1.2.3.256', '1..2.3', '1.2.3.4.', '1.2.3.4\\0', '1234.1.2.3', ''}) do\n"
        " bad(sc.net.host, ip, 0); bad(sc.net.join, ip, 1234)\n"
        "end\n"
        "bad(sc.net.host, 123, 0); bad(sc.net.host, {}, 0)\n"
        "for _, port in ipairs({-1, 65536, 1.5, '1234', 0/0, math.huge}) do bad(sc.net.host, '127.0.0.1', port) end\n"
        "bad(sc.net.join, '127.0.0.1', 0); bad(sc.net.join, '127.0.0.1', 1, 1)\n"
        "for _, cap in ipairs({0, 33, '4', 2.5}) do bad(sc.net.host, '127.0.0.1', 0, cap) end\n"
        "h=assert(sc.net.host('127.0.0.1', 0)); port=h:port()\n"
        "assert(type(port)=='number' and port>0); assert(getmetatable(h)=='network session')\n"
        "bad(setmetatable, h, {}); bad(h.poll, h, 0); bad(h.send, {}, 1, 'x')\n"
        "bad(h.send, h, -1, 'x'); bad(h.send, h, 4294967296, 'x'); bad(h.send, h, '1', 'x')\n"
        "bad(h.send, h, 0, 12); bad(h.send, h, 0, string.rep('x',1201))\n"
        "bad(h.send, h, 0, '', 'udp'); bad(h.send, h, 0, '', 'state\\0'); bad(h.send, h, 0, '', 1)\n"
        "bad(h.disconnect, h, 0); bad(h.disconnect, h, 1, -1); bad(h.disconnect, h, 1, 4294967296)\n"
        "bad(h.rtt, h, 0); bad(h.port, h, 2); bad(h.close, h, 2); bad(h.flush, h, 2)\n"
        "local event, err=h:poll(); assert(event==nil and err==nil)\n"
        "local ok; ok,err=h:send(0,''); assert(ok==nil and type(err)=='string')\n"
        "ok,err=h:rtt(1); assert(ok==nil and type(err)=='string')\n"
        "ok,err=h:disconnect(1); assert(ok==nil and type(err)=='string')\n"
        "ok,err=sc.net.host('127.0.0.1', port); assert(ok==nil and type(err)=='string')\n"
        "local sessions={h}; for i=2,4 do sessions[i]=assert(sc.net.host('127.0.0.1',0)) end\n"
        "ok,err=sc.net.host('127.0.0.1',0); assert(ok==nil and err:find('limit'))\n"
        "sessions[2]:close(); sessions[2]:close(); sessions[2]=assert(sc.net.host('127.0.0.1',0))\n"
        "for _,s in ipairs(sessions) do s:close() end\n"
        "for _,f in ipairs({h.poll,h.port,h.flush}) do ok,err=f(h); assert(ok==nil and err:find('closed')) end\n"
        "ok,err=h:send(0,''); assert(ok==nil and err:find('closed'))\n"
        "ok,err=h:rtt(1); assert(ok==nil and err:find('closed'))\n"
        "ok,err=h:disconnect(1); assert(ok==nil and err:find('closed'))\n");
    lua_close(L);
}

static void test_loopback(void) {
    lua_State *L = new_vm();
    run(L,
        "local host=assert(sc.net.host('127.0.0.1',0,2))\n"
        "local client=assert(sc.net.join('127.0.0.1',host:port()))\n"
        "local hp,cp; local received={}; local replies={}; local disconnected=false\n"
        "local function pump()\n"
        " for _,s in ipairs({host,client}) do\n"
        "  while true do local e,err=s:poll(); assert(not err,err); if not e then break end\n"
        "   if e.type=='connect' then assert(e.peer>0 and e.data==nil and e.reason==nil); if s==host then hp=e.peer else cp=e.peer end\n"
        "   elseif e.type=='receive' then assert(type(e.data)=='string' and e.reason==nil); if s==host then received[#received+1]=e else replies[#replies+1]=e end\n"
        "   elseif e.type=='disconnect' then assert(e.data==nil); if s==client then assert(e.peer==cp and e.reason==42); disconnected=true end\n"
        "   else error('unexpected event type') end\n"
        "  end\n"
        " end\n"
        "end\n"
        "for i=1,3000 do pump(); if hp and cp then break end; pause_one_ms() end\n"
        "assert(hp and cp,'connection timed out')\n"
        "assert(host:rtt(hp)>=0 and client:rtt(cp)>=0)\n"
        "local binary=string.char(0,1,255)..string.rep('a',1197)\n"
        "assert(client:send(cp,binary)); assert(client:send(cp,'')); assert(client:send(cp,'third')); assert(client:flush())\n"
        "for i=1,3000 do pump(); if #received==3 then break end; pause_one_ms() end\n"
        "assert(#received==3 and received[1].data==binary and received[2].data=='' and received[3].data=='third')\n"
        "for _,e in ipairs(received) do assert(e.peer==hp and e.channel=='reliable') end\n"
        "assert(host:send(0,'state\\0reply','state')); assert(host:flush())\n"
        "for i=1,3000 do pump(); if #replies>0 then break end; pause_one_ms() end\n"
        "assert(#replies==1 and replies[1].peer==cp and replies[1].channel=='state' and replies[1].data=='state\\0reply')\n"
        "assert(host:disconnect(hp,42)); local ok,err=host:send(hp,'late'); assert(ok==nil and err)\n"
        "for i=1,3000 do pump(); if disconnected then break end; pause_one_ms() end\n"
        "assert(disconnected,'disconnect timed out'); host:close(); client:close()\n");
    lua_close(L);
}

static void test_guard(void) {
    lua_State *L = new_vm();
    run(L, "h=assert(sc.net.host('127.0.0.1',0)); port=h:port()");
    allow_mutation = false;
    run(L,
        "local function blocked(f,...) local ok,err=pcall(f,...); assert(not ok and err:find('draw')) end\n"
        "blocked(sc.net.host,'127.0.0.1',0); blocked(sc.net.join,'127.0.0.1',port)\n"
        "blocked(h.poll,h); blocked(h.send,h,0,'x'); blocked(h.flush,h); blocked(h.disconnect,h,1); blocked(h.close,h)\n"
        "assert(h:port()==port); local rtt,err=h:rtt(1); assert(rtt==nil and type(err)=='string')\n");
    uint16_t port = read_port(L);
    check_bind(port, false);
    /* C cleanup must run even when authored mutation is forbidden. */
    lua_close(L);
    check_bind(port, true);
    allow_mutation = true;
}

static void test_gc(void) {
    lua_State *L = new_vm();
    run(L, "h=assert(sc.net.host('127.0.0.1',0)); port=h:port()");
    uint16_t port = read_port(L);
    check_bind(port, false);
    run(L, "h=nil; collectgarbage('collect')");
    check_bind(port, true);
    run(L,
        "local hosts={}; for i=1,4 do hosts[i]=assert(sc.net.host('127.0.0.1',0)) end\n"
        "hosts=nil; collectgarbage('collect')\n"
        "for i=1,4 do hosts=assert(sc.net.host('127.0.0.1',0)); hosts:close() end\n"
        "h=assert(sc.net.host('127.0.0.1',port))\n");
    check_bind(port, false);
    lua_close(L);
    check_bind(port, true);
}

typedef struct { bool armed; size_t remaining; } FailingAllocator;

static void *failing_alloc(void *user, void *pointer, size_t old, size_t size) {
    FailingAllocator *allocator = user;
    if (!size) { free(pointer); return NULL; }
    if (!pointer) old = 0;
    if (allocator->armed && size > old) {
        if (!allocator->remaining) return NULL;
        --allocator->remaining;
    }
    return realloc(pointer, size);
}

static void test_allocation_cleanup(void) {
    char error[SC_NET_ERROR_MAX];
    ScNet *probe = sc_net_host("127.0.0.1", 0, 1, error);
    CHECK(probe != NULL);
    uint16_t port = sc_net_port(probe);
    sc_net_close(probe);
    unsigned failures = 0, successes = 0;
    for (size_t threshold = 0; threshold < 32; ++threshold) {
        FailingAllocator allocator = {false, threshold};
        lua_State *L = lua_newstate(failing_alloc, &allocator);
        CHECK(L != NULL);
        register_api(L);
        char source[256];
        snprintf(source, sizeof source,
            "h=assert(sc.net.host('127.0.0.1',%u)); buffer=string.rep('x',65536)", (unsigned)port);
        CHECK(luaL_loadstring(L, source) == LUA_OK);
        allocator.armed = true;
        int status = lua_pcall(L, 0, 0, 0);
        allocator.armed = false;
        CHECK(status == LUA_ERRMEM || status == LUA_OK);
        if (status == LUA_ERRMEM) ++failures; else ++successes;
        lua_close(L);
        check_bind(port, true);
    }
    CHECK(failures > 0 && successes > 0);
}

static void write_scene(const char *path, const char *source) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    size_t size = strlen(source);
    CHECK(fwrite(source, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void test_script_integration(void) {
    char directory[80], path[96];
    snprintf(directory, sizeof directory, ".net-script-test-%ld", (long)test_pid());
    snprintf(path, sizeof path, "%s/main.lua", directory);
    CHECK(test_mkdir(directory) == 0);
    ScWorld *world = malloc(sizeof *world);
    CHECK(world != NULL);
    ScScript script;
    write_scene(path,
        "local h; return {\n"
        " init=function() h=assert(sc.net.host('127.0.0.1',0)); port=h:port() end,\n"
        " update=function() assert(h:flush()) end,\n"
        " draw=function()\n"
        "  local ok,err=pcall(h.poll,h); assert(not ok and err:find('forbidden in draw'))\n"
        "  ok,err=pcall(sc.net.host,'127.0.0.1',0); assert(not ok and err:find('forbidden in draw'))\n"
        "  assert(h:port()==port)\n"
        " end}\n");
    sc_world_init(world, 42);
    CHECK(sc_script_open(&script, world, directory, "main.lua"));
    uint16_t port = read_port(script.lua);
    check_bind(port, false);
    CHECK(sc_script_draw(&script, 0));
    CHECK(sc_script_update(&script));
    sc_script_close(&script);
    CHECK(script.lua == NULL && script.memory_used == 0);
    check_bind(port, true);

    write_scene(path,
        "local h; return {\n"
        " init=function() h=assert(sc.net.host('127.0.0.1',0)); port=h:port() end,\n"
        " update=function() assert(h:flush()) end,\n"
        " draw=function() h:poll() end}\n");
    sc_world_init(world, 42);
    CHECK(sc_script_open(&script, world, directory, "main.lua"));
    port = read_port(script.lua);
    CHECK(!sc_script_draw(&script, 0));
    CHECK(strstr(script.error, "forbidden in draw") != NULL);
    CHECK(sc_script_update(&script)); /* Failed draw also restores mutable phase. */
    sc_script_close(&script);
    CHECK(script.lua == NULL && script.memory_used == 0);
    check_bind(port, true);

    char source[256];
    snprintf(source, sizeof source,
        "return {init=function() h=assert(sc.net.host('127.0.0.1',%u)); error('init failed') end}",
        (unsigned)port);
    write_scene(path, source);
    sc_world_init(world, 42);
    CHECK(!sc_script_open(&script, world, directory, "main.lua"));
    CHECK(strstr(script.error, "init failed") != NULL);
    CHECK(script.lua == NULL && script.memory_used == 0);
    check_bind(port, true);
    free(world);
    CHECK(remove(path) == 0);
    CHECK(test_rmdir(directory) == 0);
}

int main(void) {
    test_arguments_and_limits();
    test_loopback();
    test_guard();
    test_gc();
    test_allocation_cleanup();
    test_script_integration();
    puts("Lua networking: binary loopback, validation, guards, capacity and lifecycle passed");
    return 0;
}
