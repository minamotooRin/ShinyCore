#include "shiny/net_lua.h"
#include "script_api.h"
#include "shiny/net.h"
#include "shiny/script_data.h"

extern "C" {
#include <lauxlib.h>
}
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace {
constexpr auto SESSION_TYPE = "ShinyCore.network.session";

struct NetContext { unsigned live = 0; ScNetSessions* application{}; };
struct NetSession {
    std::unique_ptr<ScNet> owned;
    NetContext* context{};
    char name[64]{};
    std::uint64_t generation{};

    ScNetSessions::Entry* entry() const noexcept {
        if(!*name||!context->application) return nullptr;
        auto found=context->application->entries.find(name);
        return found!=context->application->entries.end()&&found->second.generation==generation?&found->second:nullptr;
    }
    ScNet* get() const noexcept {
        if(owned) return owned.get();
        auto* binding=entry();
        return binding?binding->net.get():nullptr;
    }
    void close(bool explicit_close=false) noexcept {
        if(owned) { owned.reset(); --context->live; }
        if(explicit_close&&entry()) context->application->entries.erase(name);
        name[0]=0;
    }
    ~NetSession() { close(); }
};
constexpr char context_key = 0;

/* Lua uses longjmp, so every expected/string/exception scope must end before
 * returning to Lua. Operations and accept callbacks here must never call Lua. */
template<class Operation, class Accept>
bool native_call(char (&error)[SC_NET_ERROR_MAX], Operation operation, Accept accept) noexcept {
    static_assert(std::is_trivially_destructible_v<Operation>);
    static_assert(std::is_trivially_destructible_v<Accept>);
    try {
        auto result = operation();
        if (!result) {
            std::snprintf(error, sizeof error, "%s", result.error().c_str());
            return false;
        }
        accept(result);
        return true;
    } catch (const std::exception &exception) {
        std::snprintf(error, sizeof error, "network operation failed: %s", exception.what());
    } catch (...) {
        std::snprintf(error, sizeof error, "network operation failed");
    }
    return false;
}

template<class Operation>
bool native_call(char (&error)[SC_NET_ERROR_MAX], Operation operation) noexcept {
    return native_call(error, operation, [](const auto &) {});
}

static void arg_count(lua_State *L, int minimum, int maximum) {
    int count = lua_gettop(L);
    if (count < minimum || count > maximum)
        luaL_error(L, "expected %d to %d arguments, got %d", minimum, maximum, count);
}

static lua_Integer integer_at(lua_State *L, int index, lua_Integer min, lua_Integer max) {
    int valid = 0;
    lua_Integer value = lua_tointegerx(L, index, &valid);
    if (lua_type(L, index) != LUA_TNUMBER || !valid || value < min || value > max)
        luaL_argerror(L, index, "integer outside allowed range");
    return value;
}

static const char *ipv4_at(lua_State *L, int index) {
    if (lua_type(L, index) != LUA_TSTRING)
        luaL_argerror(L, index, "expected numeric IPv4 string");
    size_t size;
    const char *address = lua_tolstring(L, index, &size);
    unsigned parts = 0, digits = 0, value = 0;
    for (size_t i = 0; i <= size; ++i) {
        unsigned char byte = (unsigned char)address[i];
        if (i == size || byte == '.') {
            if (!digits || value > 255 || ++parts > 4)
                luaL_argerror(L, index, "expected numeric IPv4 string");
            digits = value = 0;
        } else if (byte >= '0' && byte <= '9' && ++digits <= 3) {
            value = value * 10 + (unsigned)(byte - '0');
        } else {
            luaL_argerror(L, index, "expected numeric IPv4 string");
        }
    }
    if (parts != 4) luaL_argerror(L, index, "expected numeric IPv4 string");
    return address;
}

static void require_mutable(lua_State *L) {
    if (!lua_isnil(L, lua_upvalueindex(2))) {
        lua_pushvalue(L, lua_upvalueindex(2));
        lua_call(L, 0, 0);
    }
}

static int failure(lua_State *L, const char *message) {
    lua_pushnil(L);
    lua_pushstring(L, message);
    return 2;
}

static NetSession *session_at(lua_State *L) {
    return static_cast<NetSession *>(luaL_checkudata(L, 1, SESSION_TYPE));
}

static int session_gc(lua_State *L) {
    std::destroy_at(session_at(L));
    return 0;
}

static int create_session(lua_State *L, bool hosting) {
    arg_count(L, 2, hosting ? 3 : 2);
    require_mutable(L);
    const char *address = ipv4_at(L, 1);
    uint16_t port = (uint16_t)integer_at(L, 2, hosting ? 0 : 1, UINT16_MAX);
    unsigned peers = hosting && !lua_isnoneornil(L, 3)
        ? (unsigned)integer_at(L, 3, 1, SC_NET_MAX_PEERS) : 8;
    auto *context = static_cast<NetContext *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (context->live+(context->application?context->application->entries.size():0) >= SC_NET_LUA_MAX_SESSIONS)
        return failure(L, "network session limit reached (4 per VM)");

    /* Finish every Lua allocation before acquiring the native socket. The
     * already-finalizable userdata owns the result even if later Lua code OOMs. */
    auto *session = std::construct_at(static_cast<NetSession *>(lua_newuserdatauv(L, sizeof(NetSession), 1)));
    session->context = context;
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setiuservalue(L, -2, 1);
    luaL_setmetatable(L, SESSION_TYPE);
    char error[SC_NET_ERROR_MAX]{};
    bool success = native_call(error,
        [&] { return hosting ? ScNet::host(address, port, peers) : ScNet::join(address, port); },
        [&](auto &result) { session->owned = std::move(*result); });
    if (!success) return failure(L, error);
    ++context->live;
    return 1;
}

static int net_host(lua_State *L) { return create_session(L, true); }
static int net_join(lua_State *L) { return create_session(L, false); }

static int session_close(lua_State *L) {
    arg_count(L, 1, 1);
    require_mutable(L);
    session_at(L)->close(true);
    return 0;
}

static int session_poll(lua_State *L) {
    arg_count(L, 1, 1);
    require_mutable(L);
    NetSession *session = session_at(L);
    auto* binding=session->entry();
    if (!binding && !session->get()) return failure(L, "network session is closed");
    ScNetEvent event{};
    static_assert(std::is_trivially_destructible_v<ScNetEvent>);
    bool status = false;
    char error[SC_NET_ERROR_MAX]{};
    bool success = native_call(error, [&] { return binding?binding->poll():session->get()->poll(); },
        [&](auto &result) { if (*result) { event = **result; status = true; } });
    if (!success) return failure(L, error);
    if (!status) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 5);
    lua_pushstring(L, event.type == ScNetEventType::Connect ? "connect" :
                      event.type == ScNetEventType::Receive ? "receive" : "disconnect");
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, (lua_Integer)event.peer);
    lua_setfield(L, -2, "peer");
    if (event.type == ScNetEventType::Receive) {
        lua_pushlstring(L, reinterpret_cast<const char *>(event.data.data()), event.size);
        lua_setfield(L, -2, "data");
        lua_pushstring(L, event.channel == ScNetChannel::Control ? "reliable" : "state");
        lua_setfield(L, -2, "channel");
    } else if (event.type == ScNetEventType::Disconnect) {
        lua_pushinteger(L, (lua_Integer)event.reason);
        lua_setfield(L, -2, "reason");
    }
    return 1;
}

static int session_send(lua_State *L) {
    arg_count(L, 3, 4);
    require_mutable(L);
    NetSession *session = session_at(L);
    uint32_t peer = (uint32_t)integer_at(L, 2, 0, UINT32_MAX);
    if (lua_type(L, 3) != LUA_TSTRING) return luaL_argerror(L, 3, "expected binary string");
    size_t size;
    const char *data = lua_tolstring(L, 3, &size);
    if (size > SC_NET_MAX_PAYLOAD) return luaL_argerror(L, 3, "message exceeds 1200 bytes");
    ScNetChannel channel = ScNetChannel::Control;
    if (!lua_isnoneornil(L, 4)) {
        size_t length;
        if (lua_type(L, 4) != LUA_TSTRING) return luaL_argerror(L, 4, "expected 'reliable' or 'state'");
        const char *name = lua_tolstring(L, 4, &length);
        if (length == 5 && memcmp(name, "state", 5) == 0) channel = ScNetChannel::State;
        else if (length != 8 || memcmp(name, "reliable", 8) != 0)
            return luaL_argerror(L, 4, "expected 'reliable' or 'state'");
    }
    auto* binding=session->entry();
    if (!binding && !session->get()) return failure(L, "network session is closed");
    char error[SC_NET_ERROR_MAX]{};
    if (!native_call(error, [&] {
            auto bytes=std::span{reinterpret_cast<const std::uint8_t *>(data), size};
            return binding?binding->send(peer,channel,bytes):session->get()->send(peer,channel,bytes);
        })) return failure(L, error);
    lua_pushboolean(L, true);
    return 1;
}

static int session_flush(lua_State *L) {
    arg_count(L, 1, 1);
    require_mutable(L);
    NetSession *session = session_at(L);
    if (!session->get()) return failure(L, "network session is closed");
    session->get()->flush();
    lua_pushboolean(L, true);
    return 1;
}

static int session_disconnect(lua_State *L) {
    arg_count(L, 2, 3);
    require_mutable(L);
    NetSession *session = session_at(L);
    uint32_t peer = (uint32_t)integer_at(L, 2, 1, UINT32_MAX);
    uint32_t reason = lua_isnoneornil(L, 3) ? 0
        : (uint32_t)integer_at(L, 3, 0, UINT32_MAX);
    if (!session->get()) return failure(L, "network session is closed");
    char error[SC_NET_ERROR_MAX]{};
    if (!native_call(error, [&] { return session->get()->disconnect(peer, reason); }))
        return failure(L, error);
    lua_pushboolean(L, true);
    return 1;
}

static int session_port(lua_State *L) {
    arg_count(L, 1, 1);
    NetSession *session = session_at(L);
    if (!session->get()) return failure(L, "network session is closed");
    lua_pushinteger(L, session->get()->port());
    return 1;
}

static int session_rtt(lua_State *L) {
    arg_count(L, 2, 2);
    NetSession *session = session_at(L);
    uint32_t peer = (uint32_t)integer_at(L, 2, 1, UINT32_MAX);
    if (!session->get()) return failure(L, "network session is closed");
    int rtt = 0;
    char error[SC_NET_ERROR_MAX]{};
    if (!native_call(error, [&] { return session->get()->rtt(peer); },
        [&](auto &result) { rtt = *result; })) return failure(L, error);
    lua_pushinteger(L, rtt);
    return 1;
}

static const char* session_name(lua_State* L,int index) {
    if(lua_type(L,index)!=LUA_TSTRING) luaL_argerror(L,index,"expected session name string");
    size_t size=0; const char* name=lua_tolstring(L,index,&size);
    if(size==0||size>=64||std::memchr(name,0,size)) luaL_argerror(L,index,"session name requires 1..63 bytes");
    return name;
}
static int session_stats(lua_State* L) {
    arg_count(L,1,1);
    auto* entry=session_at(L)->entry();
    if(!entry) return failure(L,"stats requires a live named application session");
    lua_createtable(L,0,11);
    const auto field=[&](const char* name,std::size_t value) {
        lua_pushinteger(L,static_cast<lua_Integer>(value)); lua_setfield(L,-2,name);
    };
    field("queued",entry->queued); field("readable",entry->readable);
    field("receive_capacity",SC_NET_RECEIVE_CAPACITY);
    field("service_budget",SC_NET_TICK_MESSAGES);
    field("receive_tick_budget",SC_NET_TICK_MESSAGES);
    field("send_remaining",SC_NET_TICK_MESSAGES-entry->sent_messages);
    field("send_bytes_remaining",SC_NET_TICK_BYTES-entry->sent_bytes);
    field("state_bytes",entry->state_bytes); field("state_capacity",SC_NET_STATE_BYTES);
    lua_pushboolean(L,entry->net!=nullptr); lua_setfield(L,-2,"open");
    if(entry->fault[0]) { lua_pushstring(L,entry->fault.data()); lua_setfield(L,-2,"error"); }
    return 1;
}
static int session_state(lua_State* L) {
    arg_count(L,1,2);
    const bool writing=lua_gettop(L)==2;
    if(writing) require_mutable(L);
    auto* entry=session_at(L)->entry();
    if(!entry) return failure(L,"state requires a live named application session");
    if(!writing) { sc_lua_push(L,entry->state); return 1; }
    char error[SC_NET_ERROR_MAX]{};
    if(!native_call(error,[&]() -> std::expected<void,std::string> {
        if(lua_isnil(L,2)) { entry->state=ScValue{}; entry->state_bytes=0; return {}; }
        auto value=sc_lua_read(L,2);
        if(!value) return std::unexpected(value.error());
        auto checked=sc_state_validate(*value);
        if(!checked) return std::unexpected(checked.error());
        auto bytes=sc_json_write(*checked).size();
        if(bytes>SC_NET_STATE_BYTES) return std::unexpected("session state exceeds 64 KiB");
        entry->state=std::move(*checked);
        entry->state_bytes=bytes;
        return {};
    })) return failure(L,error);
    lua_pushboolean(L,true); return 1;
}
static int session_persist(lua_State* L) {
    arg_count(L,2,2); require_mutable(L);
    auto* session=session_at(L); const char* name=session_name(L,2);
    auto* application=session->context->application;
    if(!application) return failure(L,"application sessions unavailable in this host");
    if(!session->owned) return failure(L,"only a live local session can be persisted");
    if(application->entries.size()>=SC_NET_LUA_MAX_SESSIONS) return failure(L,"application session capacity exhausted");
    if(application->entries.contains(name)) return failure(L,"session name already bound");
    if(application->next_generation==UINT64_MAX) return failure(L,"application session generations exhausted");
    {
        // Allocate the entry before transferring ownership; exceptions preserve the socket.
        auto [entry,inserted]=application->entries.try_emplace(name);
        (void)inserted;
        entry->second.net=std::move(session->owned);
        entry->second.generation=application->next_generation++;
        session->generation=entry->second.generation;
        std::snprintf(session->name,sizeof session->name,"%s",name);
        --session->context->live;
    }
    lua_pushboolean(L,true); return 1;
}
static int net_bind(lua_State* L) {
    arg_count(L,1,1); const char* name=session_name(L,1);
    auto* context=static_cast<NetContext*>(lua_touserdata(L,lua_upvalueindex(1)));
    if(!context->application) return failure(L,"application sessions unavailable in this host");
    auto found=context->application->entries.find(name);
    if(found==context->application->entries.end()) return failure(L,"unknown application session");
    auto* session=std::construct_at(static_cast<NetSession*>(lua_newuserdatauv(L,sizeof(NetSession),1)));
    session->context=context; session->generation=found->second.generation;
    std::snprintf(session->name,sizeof session->name,"%s",name);
    lua_pushvalue(L,lua_upvalueindex(1)); lua_setiuservalue(L,-2,1); luaL_setmetatable(L,SESSION_TYPE);
    return 1;
}

static int net_time(lua_State* L) {
    arg_count(L,0,0);
    auto* context=static_cast<NetContext*>(lua_touserdata(L,lua_upvalueindex(1)));
    if(!context->application) return failure(L,"application network clock unavailable in this host");
    lua_pushnumber(L,context->application->time());
    return 1;
}
static int net_token(lua_State* L) {
    arg_count(L,0,0); require_mutable(L);
    auto* context=static_cast<NetContext*>(lua_touserdata(L,lua_upvalueindex(1)));
    if(!context->application) return failure(L,"application token service unavailable in this host");
    ScNetToken token{};
    char error[SC_NET_ERROR_MAX]{};
    if(!native_call(error,[&] { return context->application->token(); },
                         [&](auto& result) { token=*result; })) return failure(L,error);
    lua_pushlstring(L,token.data(),32); return 1;
}

const ScValue peers_default{8.0}, reason_default{0.0}, channel_default{std::string("reliable")};
constexpr ScLuaParameter name_parameters[]={{"name","string",true,"Exact string, 1..63 bytes without NUL. Case-sensitive application-local name."}};
const ScLuaParameter host_parameters[]={
    {"bind_ipv4","string",true,"Numeric dotted IPv4 only; 0.0.0.0 binds all interfaces, 127.0.0.1 loopback."},
    {"port","integer",true,"Local port; zero asks the OS for a free port.",nullptr,0,65535},
    {"max_peers","integer|nil",false,"Omitted/nil uses 8.",&peers_default,1,SC_NET_MAX_PEERS}};
constexpr ScLuaParameter join_parameters[]={
    {"ipv4","string",true,"Numeric dotted IPv4 only; no DNS."},
    {"port","integer",true,"Remote port.",nullptr,1,65535}};
constexpr ScLuaParameter peer_parameters[]={{"peer","integer",true,"Live endpoint-local peer ID, not an entity handle.",nullptr,1,UINT32_MAX}};
const ScLuaParameter send_parameters[]={
    {"peer","integer",true,"Zero broadcasts to established peers; fails when none exist.",nullptr,0,UINT32_MAX},
    {"data","string",true,"Binary string, 0..1200 bytes including NUL; never a Lua table."},
    {"channel","ScNetChannel|nil",false,"Omitted/nil uses reliable. State is unreliable sequenced on a separate channel.",&channel_default}};
const ScLuaParameter disconnect_parameters[]={
    peer_parameters[0],{"reason","integer|nil",false,"Omitted/nil uses zero.",&reason_default,0,UINT32_MAX}};
constexpr ScLuaParameter state_parameters[]={{"value","table<string,ScData>|nil",false,"Omitted reads a copy; explicit nil clears; a plain object atomically replaces protocol state."}};
constexpr const char* network_mutation="Network side effects require load/init/update outside check mode and candidate initialization; forbidden in draw/ui_update";
constexpr ScLuaContract token_contract{{},"string|nil",ScLuaPhases::mutate,"64 OS-random token attempts per application fixed update; each success is 32 lowercase hex characters","string|nil","network"};
constexpr ScLuaContract time_contract{{},"number|nil",ScLuaPhases::read,nullptr,"string|nil","network"};
constexpr ScLuaContract bind_contract{name_parameters,"ScNetSession|nil",ScLuaPhases::read,"Borrowed bindings do not consume additional session slots","string|nil","network"};
const ScLuaContract host_contract{host_parameters,"ScNetSession|nil",ScLuaPhases::mutate,"4 combined live local/named sessions per VM context; host peers 1..32","string|nil","network"};
constexpr ScLuaContract join_contract{join_parameters,"ScNetSession|nil",ScLuaPhases::mutate,"4 combined live local/named sessions per VM context","string|nil","network"};
constexpr ScLuaContract state_contract{state_parameters,"table<string,ScData>|boolean|nil",ScLuaPhases::read,"Named sessions only; 64 KiB JSON, depth 16; excluded from checkpoints/automatic trace","string|nil","network","value",ScLuaPhases::mutate,{},ScLuaMutationWhen::present};
constexpr ScLuaContract stats_contract{{},"ScNetStats|nil",ScLuaPhases::read,"Named sessions only; terminal entries remain inspectable until explicit close","string|nil","network"};
constexpr ScLuaContract persist_contract{name_parameters,"boolean|nil",ScLuaPhases::mutate,"At most 4 named sessions; duplicate names fail without transferring socket ownership","string|nil","network"};
constexpr ScLuaContract poll_contract{{},"ScNetEvent|nil",ScLuaPhases::mutate,"Named receive FIFO 256; at most 64 readable events per fixed update. Nil alone means empty; nil,error means failure","string|nil","network"};
const ScLuaContract send_contract{send_parameters,"boolean|nil",ScLuaPhases::mutate,"Named session: 64 successful sends / 65536 bytes per tick; broadcast charges one payload. Transport: 256 queued commands per peer","string|nil","network"};
constexpr ScLuaContract flush_contract{{},"boolean|nil",ScLuaPhases::mutate,network_mutation,"string|nil","network"};
const ScLuaContract disconnect_contract{disconnect_parameters,"boolean|nil",ScLuaPhases::mutate,network_mutation,"string|nil","network"};
constexpr ScLuaContract close_contract{{},nullptr,ScLuaPhases::mutate,network_mutation,nullptr,"network"};
constexpr ScLuaContract port_contract{{},"integer|nil",ScLuaPhases::read,"Local bound UDP port, 1..65535","string|nil","network"};
constexpr ScLuaContract rtt_contract{peer_parameters,"integer|nil",ScLuaPhases::read,"Nonnegative round-trip estimate in milliseconds","string|nil","network"};

static const ScLuaApi constructors[] = {
    {"token",sc_lua_guard<net_token>,"sc.net.token() -> token|nil,error","Issue 128 OS-random bits as 32 lowercase hex characters; 64 attempts per application fixed update. Independent of gameplay RNG. Forbidden in draw/ui_update, check mode and candidate initialization.",&token_contract},
    {"time",sc_lua_guard<net_time>,"sc.net.time() -> seconds|nil,error","Read monotonic application seconds sampled at the last fixed-update boundary; room changes do not reset it. Not replay time.",&time_contract},
    {"bind",sc_lua_guard<net_bind>,"sc.net.bind(name) -> session|nil,error","Borrow an application session by name, including during candidate initialization.",&bind_contract},
    {"host", sc_lua_guard<net_host>, "sc.net.host(bind_ipv4, port[, max_peers=8]) -> session|nil, error", "Bind numeric IPv4; port 0 selects a free port. Maximum 4 combined local/named sessions per VM context and 32 peers per host; forbidden in draw/ui_update, check mode and candidate initialization.",&host_contract},
    {"join", sc_lua_guard<net_join>, "sc.net.join(ipv4, port) -> session|nil, error", "Start an asynchronous IPv4 connection; poll both endpoints until connect. Forbidden in draw/ui_update, check mode and candidate initialization.",&join_contract},
    {NULL, NULL, NULL, NULL}
};

static const ScLuaApi methods[] = {
    {"state",sc_lua_guard<session_state>,"session:state([object_or_nil]) -> object|true|nil,error","Read a copy or atomically replace named session protocol state (64 KiB JSON, depth 16). Nil clears it; excluded from saves and automatic trace. Writes forbidden in draw/ui_update, check mode and candidate initialization.",&state_contract},
    {"stats",sc_lua_guard<session_stats>,"session:stats() -> stats|nil,error","Read named application session queue, remaining tick budgets and terminal error; unavailable on local sessions.",&stats_contract},
    {"persist",sc_lua_guard<session_persist>,"session:persist(name) -> true|nil,error","Transfer socket ownership to the application; room teardown no longer closes it. Mutating operation; forbidden in draw/ui_update, check mode and candidate initialization.",&persist_contract},
    {"poll", sc_lua_guard<session_poll>, "session:poll() -> event|nil, error", "Read one event without waiting. Named sessions expose at most 64 queued events per fixed update; local sessions service transport directly. Forbidden in draw/ui_update, check mode and candidate initialization.",&poll_contract},
    {"send", sc_lua_guard<session_send>, "session:send(peer, data[, channel='reliable']) -> true|nil, error", "Queue 0..1200 binary bytes; reliable is ordered, state is unreliable sequenced. Peer 0 broadcasts; forbidden in draw/ui_update, check mode and candidate initialization.",&send_contract},
    {"flush", sc_lua_guard<session_flush>, "session:flush() -> true|nil, error", "Send queued outgoing packets without waiting; forbidden in draw/ui_update, check mode and candidate initialization.",&flush_contract},
    {"disconnect", sc_lua_guard<session_disconnect>, "session:disconnect(peer[, reason=0]) -> true|nil, error", "Begin graceful disconnect; keep polling for disconnect. Forbidden in draw/ui_update, check mode and candidate initialization.",&disconnect_contract},
    {"close", sc_lua_guard<session_close>, "session:close()", "Immediately release the socket and pending packets; idempotent. Local sessions also close on collection; named sessions survive VM close. Forbidden in draw/ui_update, check mode and candidate initialization.",&close_contract},
    {"port", sc_lua_guard<session_port>, "session:port() -> integer|nil, error", "Read the local UDP port; a closed session returns nil and an error.",&port_contract},
    {"rtt", sc_lua_guard<session_rtt>, "session:rtt(peer) -> integer|nil, error", "Read round-trip milliseconds for a connected local peer ID.",&rtt_contract},
    {NULL, NULL, NULL, NULL}
};

static void add_functions(lua_State *L, const ScLuaApi *entries, int context,
                          lua_CFunction guard) {
    for (const ScLuaApi *entry = entries; entry->name; ++entry) {
        lua_pushvalue(L, context);
        if (guard) lua_pushcfunction(L, guard); else lua_pushnil(L);
        lua_pushcclosure(L, entry->function, 2);
        lua_setfield(L, -2, entry->name);
    }
}

} // namespace

void sc_net_lua_register(lua_State *L, lua_CFunction guard,ScNetSessions* application) {
    int sc = lua_absindex(L, -1);
    luaL_checktype(L, sc, LUA_TTABLE);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &context_key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        auto* owner=std::construct_at(static_cast<NetContext *>(lua_newuserdatauv(L, sizeof(NetContext), 0)));
        owner->application=application;
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &context_key);
    }
    int context = lua_gettop(L);
    luaL_newmetatable(L, SESSION_TYPE);
    lua_pushcfunction(L, sc_lua_guard<session_gc>);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "network session");
    lua_setfield(L, -2, "__metatable");
    lua_newtable(L);
    add_functions(L, methods, context, guard);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "available");
    add_functions(L, constructors, context, guard);
    lua_setfield(L, sc, "net");
    lua_pop(L, 1);
}

void sc_net_lua_describe(void) {
    sc_api_describe(constructors,"sc.net."); sc_api_describe(methods,"ScNetSession:");
}
ScValue sc_net_lua_contracts() {
    using V=ScValue; V::Array events,stats;
    auto field=[](const char* name,const char* type,bool required,const char* description) {
        return V::Object{{"name",V{std::string(name)}},{"type",V{std::string(type)}},
            {"required",V{required}},{"readonly",V{true}},{"description",V{std::string(description)}}};
    };
    events.emplace_back(field("type","'connect'|'receive'|'disconnect'",true,"Event discriminator; no uniform ordering across channels."));
    auto peer=field("peer","integer",true,"Endpoint-local connection ID. Failed connection attempts may disconnect with peer zero.");
    peer.emplace("minimum",V{0.0});peer.emplace("maximum",V{double(UINT32_MAX)});events.emplace_back(std::move(peer));
    auto data=field("data","string",false,"Receive only; binary-safe, including NUL.");
    data.emplace("maximum_bytes",V{double(SC_NET_MAX_PAYLOAD)});events.emplace_back(std::move(data));
    events.emplace_back(field("channel","ScNetChannel",false,"Receive only."));
    auto reason=field("reason","integer",false,"Disconnect only; application reason or zero for transport loss.");
    reason.emplace("minimum",V{0.0});reason.emplace("maximum",V{double(UINT32_MAX)});events.emplace_back(std::move(reason));
    auto number=[&](const char* name,double minimum,double maximum,const char* description) {
        auto out=field(name,"integer",true,description);
        out.emplace("minimum",V{minimum});out.emplace("maximum",V{maximum});stats.emplace_back(std::move(out));
    };
    number("queued",0,SC_NET_RECEIVE_CAPACITY,"Buffered events, including the readable prefix.");
    number("readable",0,SC_NET_TICK_MESSAGES,"Events remaining in the current fixed-update batch.");
    number("receive_capacity",SC_NET_RECEIVE_CAPACITY,SC_NET_RECEIVE_CAPACITY,"Allocated FIFO event capacity.");
    number("service_budget",SC_NET_TICK_MESSAGES,SC_NET_TICK_MESSAGES,"Maximum transport events read per host service iteration.");
    number("receive_tick_budget",SC_NET_TICK_MESSAGES,SC_NET_TICK_MESSAGES,"Maximum events published per fixed update.");
    number("send_remaining",0,SC_NET_TICK_MESSAGES,"Successful sends remaining this tick; failed calls do not consume budget.");
    number("send_bytes_remaining",0,SC_NET_TICK_BYTES,"Application payload bytes remaining this tick.");
    number("state_bytes",0,SC_NET_STATE_BYTES,"Serialized protocol object size; zero when cleared.");
    number("state_capacity",SC_NET_STATE_BYTES,SC_NET_STATE_BYTES,"Protocol-state JSON byte limit.");
    stats.emplace_back(field("open","boolean",true,"False for a retained terminal entry; stats/state remain readable until close."));
    stats.emplace_back(field("error","string",false,"Terminal queue/transport diagnostic, omitted when healthy."));
    V::Object types;
    types.emplace("ScNetEvent",V::Object{{"fields",V{std::move(events)}},{"constraints",V{V::Array{
        V{std::string("Connect: type/peer. Receive: type/peer/data/channel. Disconnect: type/peer/reason.")}}}}});
    types.emplace("ScNetStats",V::Object{{"fields",V{std::move(stats)}},{"constraints",V{V::Array{
        V{std::string("Copied snapshot for named sessions only; queued/readable are cleared on terminal failure. Binding generation prevents same-name replacements reviving old handles.")}}}}});
    return V{std::move(types)};
}
