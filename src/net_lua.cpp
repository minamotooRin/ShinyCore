#include "shiny/net_lua.h"
#include "shiny/net.h"

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

struct NetContext { unsigned live = 0; };
struct NetSession {
    std::unique_ptr<ScNet> net;
    NetContext *context;

    void close() noexcept {
        if (net) {
            net.reset();
            --context->live;
        }
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
    if (context->live >= SC_NET_LUA_MAX_SESSIONS)
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
        [&](auto &result) { session->net = std::move(*result); });
    if (!success) return failure(L, error);
    ++context->live;
    return 1;
}

static int net_host(lua_State *L) { return create_session(L, true); }
static int net_join(lua_State *L) { return create_session(L, false); }

static int session_close(lua_State *L) {
    arg_count(L, 1, 1);
    require_mutable(L);
    session_at(L)->close();
    return 0;
}

static int session_poll(lua_State *L) {
    arg_count(L, 1, 1);
    require_mutable(L);
    NetSession *session = session_at(L);
    if (!session->net) return failure(L, "network session is closed");
    ScNetEvent event{};
    static_assert(std::is_trivially_destructible_v<ScNetEvent>);
    bool status = false;
    char error[SC_NET_ERROR_MAX]{};
    bool success = native_call(error, [&] { return session->net->poll(); },
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
    if (!session->net) return failure(L, "network session is closed");
    char error[SC_NET_ERROR_MAX]{};
    if (!native_call(error, [&] {
            return session->net->send(peer, channel,
                std::span{reinterpret_cast<const std::uint8_t *>(data), size});
        })) return failure(L, error);
    lua_pushboolean(L, true);
    return 1;
}

static int session_flush(lua_State *L) {
    arg_count(L, 1, 1);
    require_mutable(L);
    NetSession *session = session_at(L);
    if (!session->net) return failure(L, "network session is closed");
    session->net->flush();
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
    if (!session->net) return failure(L, "network session is closed");
    char error[SC_NET_ERROR_MAX]{};
    if (!native_call(error, [&] { return session->net->disconnect(peer, reason); }))
        return failure(L, error);
    lua_pushboolean(L, true);
    return 1;
}

static int session_port(lua_State *L) {
    arg_count(L, 1, 1);
    NetSession *session = session_at(L);
    if (!session->net) return failure(L, "network session is closed");
    lua_pushinteger(L, session->net->port());
    return 1;
}

static int session_rtt(lua_State *L) {
    arg_count(L, 2, 2);
    NetSession *session = session_at(L);
    uint32_t peer = (uint32_t)integer_at(L, 2, 1, UINT32_MAX);
    if (!session->net) return failure(L, "network session is closed");
    int rtt = 0;
    char error[SC_NET_ERROR_MAX]{};
    if (!native_call(error, [&] { return session->net->rtt(peer); },
        [&](auto &result) { rtt = *result; })) return failure(L, error);
    lua_pushinteger(L, rtt);
    return 1;
}

struct NetEntry {
    const char *name;
    lua_CFunction function;
    const char *signature, *description;
};

static const NetEntry constructors[] = {
    {"host", net_host, "sc.net.host(bind_ipv4, port[, max_peers=8]) -> session|nil, error", "Bind numeric IPv4; port 0 selects a free port. Maximum 4 live sessions per VM and 32 peers per host; forbidden in draw."},
    {"join", net_join, "sc.net.join(ipv4, port) -> session|nil, error", "Start an asynchronous IPv4 connection; poll both endpoints until connect. Forbidden in draw."},
    {NULL, NULL, NULL, NULL}
};

static const NetEntry methods[] = {
    {"poll", session_poll, "session:poll() -> event|nil, error", "Service networking without waiting; event type is connect, receive or disconnect, with local peer ID. Forbidden in draw."},
    {"send", session_send, "session:send(peer, data[, channel='reliable']) -> true|nil, error", "Queue 0..1200 binary bytes; reliable is ordered, state is unreliable sequenced. Peer 0 broadcasts; forbidden in draw."},
    {"flush", session_flush, "session:flush() -> true|nil, error", "Send queued outgoing packets without waiting; forbidden in draw."},
    {"disconnect", session_disconnect, "session:disconnect(peer[, reason=0]) -> true|nil, error", "Begin graceful disconnect; keep polling for disconnect. Forbidden in draw."},
    {"close", session_close, "session:close()", "Immediately release the socket and pending packets; idempotent, also performed on collection or VM close. Forbidden in draw."},
    {"port", session_port, "session:port() -> integer|nil, error", "Read the local UDP port; a closed session returns nil and an error."},
    {"rtt", session_rtt, "session:rtt(peer) -> integer|nil, error", "Read round-trip milliseconds for a connected local peer ID."},
    {NULL, NULL, NULL, NULL}
};

static void add_functions(lua_State *L, const NetEntry *entries, int context,
                          lua_CFunction guard) {
    for (const NetEntry *entry = entries; entry->name; ++entry) {
        lua_pushvalue(L, context);
        if (guard) lua_pushcfunction(L, guard); else lua_pushnil(L);
        lua_pushcclosure(L, entry->function, 2);
        lua_setfield(L, -2, entry->name);
    }
}

} // namespace

void sc_net_lua_register(lua_State *L, lua_CFunction guard) {
    int sc = lua_absindex(L, -1);
    luaL_checktype(L, sc, LUA_TTABLE);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &context_key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        std::construct_at(static_cast<NetContext *>(lua_newuserdatauv(L, sizeof(NetContext), 0)));
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &context_key);
    }
    int context = lua_gettop(L);
    luaL_newmetatable(L, SESSION_TYPE);
    lua_pushcfunction(L, session_gc);
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
    const NetEntry *groups[] = {constructors, methods};
    const char *prefixes[] = {"sc.net.", "ScNetSession:"};
    for (size_t group = 0; group < 2; ++group)
        for (const NetEntry *entry = groups[group]; entry->name; ++entry)
            printf(",{\"name\":\"%s%s\",\"signature\":\"%s\",\"description\":\"%s\"}",
                   prefixes[group], entry->name, entry->signature, entry->description);
}
