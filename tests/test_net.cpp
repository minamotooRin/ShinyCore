#include "shiny/net.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <set>

namespace {
unsigned checks{};
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
    std::exit(EXIT_FAILURE); } } while (false)

std::span<const std::uint8_t> bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::unique_ptr<ScNet> host(unsigned capacity = 2) {
    auto result = ScNet::host("127.0.0.1", 0, capacity);
    CHECK(result.has_value());
    CHECK((*result)->port() != 0);
    return std::move(*result);
}

std::optional<ScNetEvent> poll(ScNet& net) {
    auto result = net.poll(1);
    if (!result) std::fprintf(stderr, "network: %s\n", result.error().c_str());
    CHECK(result.has_value());
    return *result;
}

struct Connection {
    std::unique_ptr<ScNet> client;
    std::uint32_t server_id{}, client_id{};
};

Connection connect(ScNet& server) {
    auto result = ScNet::join("127.0.0.1", server.port());
    CHECK(result.has_value());
    Connection connection{std::move(*result)};
    CHECK(!connection.client->send(0, ScNetChannel::Control, bytes("early")));
    for (int i = 0; i < 3000 && (!connection.server_id || !connection.client_id); ++i) {
        if (auto event = poll(server)) {
            CHECK(event->type == ScNetEventType::Connect);
            connection.server_id = event->peer;
        }
        if (auto event = poll(*connection.client)) {
            CHECK(event->type == ScNetEventType::Connect);
            connection.client_id = event->peer;
        }
    }
    CHECK(connection.server_id && connection.client_id);
    CHECK(connection.client->rtt(connection.client_id).has_value());
    return connection;
}

void test_arguments_and_raii() {
    CHECK(!ScNet::host({}, 0, 0));
    CHECK(!ScNet::host({}, 0, SC_NET_MAX_PEERS + 1));
    CHECK(!ScNet::host("localhost", 0, 1));
    CHECK(!ScNet::host("256.1.2.3", 0, 1));
    CHECK(!ScNet::host(std::string_view{"127.0.0.1\0bad", 13}, 0, 1));
    CHECK(!ScNet::join("localhost", 1));
    CHECK(!ScNet::join("127.0.0.1", 0));
    CHECK(!ScNet::join("0.0.0.0", 1));
    CHECK(!ScNet::join("255.255.255.255", 1));
    std::uint16_t port{};
    {
        auto net = host();
        port = net->port();
        CHECK(!net->poll(-1));
        CHECK(!net->poll(1001));
        auto empty = net->poll();
        CHECK(empty && !*empty);
        CHECK(!net->send(0, ScNetChannel::Control, bytes("nobody")));
        CHECK(!net->disconnect(0));
        CHECK(!net->rtt(0));
        CHECK(!ScNet::host("127.0.0.1", port, 1));
    }
    auto rebound = ScNet::host("127.0.0.1", port, 1);
    CHECK(rebound.has_value());
    CHECK(ScNet::host({}, 0, SC_NET_MAX_PEERS).has_value());
}

void test_messages() {
    auto server = host();
    auto first = connect(*server), second = connect(*server);
    CHECK(first.server_id != second.server_id);
    auto& client = *first.client;
    std::array<std::uint8_t, SC_NET_MAX_PAYLOAD + 1> payload{};
    payload.fill(0xab);
    CHECK(!client.send(first.client_id, ScNetChannel::Control, payload));
    CHECK(!client.send(first.client_id, static_cast<ScNetChannel>(9), bytes("x")));
    CHECK(!client.send(UINT32_MAX, ScNetChannel::Control, bytes("x")));
    CHECK(client.send(first.client_id, ScNetChannel::Control, std::span{payload}.first(SC_NET_MAX_PAYLOAD)));
    CHECK(client.send(first.client_id, ScNetChannel::Control, {}));
    for (unsigned i = 0; i < 64; ++i) {
        const std::array data{static_cast<std::uint8_t>(i), std::uint8_t{0}, std::uint8_t{255}};
        CHECK(client.send(first.client_id, ScNetChannel::Control, data));
    }
    client.flush();
    unsigned received{};
    ScNetEvent saved{};
    for (int i = 0; i < 3000 && received < 66; ++i) {
        CHECK(!poll(client)); CHECK(!poll(*second.client));
        auto event = poll(*server);
        if (!event) continue;
        CHECK(event->type == ScNetEventType::Receive && event->peer == first.server_id);
        CHECK(event->channel == ScNetChannel::Control);
        if (received == 0) {
            CHECK(event->size == SC_NET_MAX_PAYLOAD);
            CHECK(std::ranges::equal(event->data, std::span{payload}.first(SC_NET_MAX_PAYLOAD)));
            saved = *event;
        } else if (received == 1) CHECK(event->size == 0);
        else CHECK(event->size == 3 && event->data[0] == received - 2 && event->data[1] == 0 && event->data[2] == 255);
        ++received;
    }
    CHECK(received == 66 && saved.data[0] == 0xab);
    CHECK(server->send(first.server_id, ScNetChannel::Control, bytes("private")));
    unsigned private_received{};
    for (int i = 0; i < 100; ++i) {
        CHECK(!poll(*server)); CHECK(!poll(*second.client));
        if (auto event = poll(client)) {
            CHECK(event->size == 7 && std::ranges::equal(std::span{event->data}.first(7), bytes("private")));
            ++private_received;
        }
    }
    CHECK(private_received == 1);
    CHECK(server->send(0, ScNetChannel::Control, bytes("all")));
    CHECK(server->send(0, ScNetChannel::State, std::span{payload}.first(SC_NET_MAX_PAYLOAD)));
    std::array<unsigned, 2> totals{};
    const std::array peers{&client, second.client.get()};
    for (int i = 0; i < 3000 && (totals[0] != 3 || totals[1] != 3); ++i) {
        CHECK(!poll(*server));
        for (unsigned j = 0; j < peers.size(); ++j) {
            if (auto event = poll(*peers[j])) {
                CHECK(event->type == ScNetEventType::Receive);
                const unsigned bit = event->channel == ScNetChannel::Control ? 1 : 2;
                CHECK(!(totals[j] & bit));
                CHECK(bit == 1 ? event->size == 3 : event->size == SC_NET_MAX_PAYLOAD);
                totals[j] |= bit;
            }
        }
    }
    CHECK(totals[0] == 3 && totals[1] == 3);
    for (std::uint8_t seq = 1; seq <= 16; ++seq)
        CHECK(server->send(first.server_id, ScNetChannel::State, {&seq, 1}));
    unsigned latest{};
    for (int i = 0; i < 3000 && latest < 16; ++i) {
        CHECK(!poll(*server));
        if (auto event = poll(client)) {
            CHECK(event->channel == ScNetChannel::State && event->size == 1);
            CHECK(event->data[0] > latest);
            latest = event->data[0];
        }
    }
    CHECK(latest == 16);
    unsigned queued{};
    for (;;) {
        auto result = client.send(first.client_id, ScNetChannel::Control, bytes("q"));
        if (!result) { CHECK(result.error().find("queue is full") != std::string::npos); break; }
        CHECK(++queued <= SC_NET_MAX_QUEUED);
    }
    CHECK(queued > 0);
    unsigned drained{};
    for (int i = 0; i < 3000 && drained < queued; ++i) {
        CHECK(!poll(client));
        if (auto event = poll(*server)) {
            CHECK(event->peer == first.server_id && event->size == 1 && event->data[0] == 'q');
            ++drained;
        }
    }
    CHECK(drained == queued);
    for (int i = 0; i < 20; ++i) { CHECK(!poll(*server)); CHECK(!poll(client)); }
    CHECK(client.send(first.client_id, ScNetChannel::Control, bytes("bye")));
    CHECK(client.disconnect(first.client_id, 77));
    CHECK(!client.send(first.client_id, ScNetChannel::Control, bytes("late")));
    CHECK(!client.rtt(first.client_id));
    bool goodbye{}, server_closed{}, client_closed{};
    for (int i = 0; i < 3000 && (!server_closed || !client_closed); ++i) {
        if (auto event = poll(*server)) {
            CHECK(event->peer == first.server_id);
            if (event->type == ScNetEventType::Receive) {
                CHECK(!goodbye && event->size == 3); goodbye = true;
            } else {
                CHECK(goodbye && event->type == ScNetEventType::Disconnect && event->reason == 77);
                server_closed = true;
            }
        }
        if (auto event = poll(client)) {
            CHECK(event->type == ScNetEventType::Disconnect); client_closed = true;
        }
    }
    CHECK(goodbye && server_closed && client_closed);
    CHECK(!server->send(first.server_id, ScNetChannel::Control, bytes("stale")));
    CHECK(!server->disconnect(first.server_id));
    auto replacement = connect(*server);
    CHECK(replacement.server_id != first.server_id && replacement.server_id != second.server_id);
    CHECK(!server->rtt(first.server_id));
    CHECK(server->send(second.server_id, ScNetChannel::Control, bytes("alive")));
}

void test_capacity_and_timeout() {
    auto server = host(1);
    auto first = connect(*server);
    auto excess = ScNet::join("127.0.0.1", server->port());
    CHECK(excess.has_value());
    for (int i = 0; i < 50; ++i) {
        CHECK(!poll(*server)); CHECK(!poll(*first.client)); CHECK(!poll(**excess));
    }
    // An unserviced bound endpoint cannot answer the new client's handshake.
    auto silent = host(1);
    auto pending = ScNet::join("127.0.0.1", silent->port());
    CHECK(pending.has_value());
    bool expired{};
    for (int i = 0; i < 24 && !expired; ++i) {
        auto result = (*pending)->poll(1000);
        CHECK(result.has_value());
        if (*result) {
            CHECK((**result).type == ScNetEventType::Disconnect && (**result).peer == 0);
            expired = true;
        }
    }
    CHECK(expired);
    CHECK(!(*pending)->send(0, ScNetChannel::Control, bytes("late")));
}

void test_application_queue() {
    ScNetSessions application;
    auto& entry=application.entries["coop"];
    entry.net=host();
    auto connection=connect(*entry.net);
    auto& client=*connection.client;
    const auto await_count=[&](std::size_t count) {
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(entry.queued<count && entry.net && std::chrono::steady_clock::now()<deadline) {
            application.service();
            CHECK(!poll(client));
        }
        CHECK(entry.net && entry.queued==count);
    };
    const auto send_range=[&](unsigned first,unsigned count) {
        for(unsigned i=first;i<first+count;++i) {
            auto text=std::to_string(i);
            CHECK(client.send(connection.client_id,ScNetChannel::Control,bytes(text)));
        }
        client.flush();
    };
    // Service while simulation is stopped: no event leaks before a tick boundary.
    send_range(0,80); await_count(80);
    auto none=entry.poll(); CHECK(none && !*none);
    application.begin_tick();
    CHECK(entry.readable==64);
    send_range(80,4); await_count(84);
    for(unsigned i=0;i<64;++i) {
        auto event=entry.poll(); CHECK(event && *event);
        auto& value=**event;
        CHECK(value.peer==connection.server_id && value.type==ScNetEventType::Receive);
        CHECK(std::string_view(reinterpret_cast<const char*>(value.data.data()),value.size)==std::to_string(i));
    }
    none=entry.poll(); CHECK(none && !*none && entry.queued==20);
    application.begin_tick();
    for(unsigned i=64;i<84;++i) {
        auto event=entry.poll(); CHECK(event && *event);
        CHECK(std::string_view(reinterpret_cast<const char*>((**event).data.data()),(**event).size)==std::to_string(i));
    }
    CHECK(entry.queued==0);
    CHECK(!entry.send(UINT32_MAX,ScNetChannel::Control,bytes("invalid")));
    CHECK(entry.sent_messages==0);
    for(unsigned i=0;i<64;++i) CHECK(entry.send(connection.server_id,ScNetChannel::Control,bytes("ok")));
    CHECK(!entry.send(connection.server_id,ScNetChannel::Control,bytes("over budget")));
    application.begin_tick();
    std::array<std::uint8_t,1200> large{};
    for(unsigned i=0;i<54;++i) CHECK(entry.send(connection.server_id,ScNetChannel::Control,large));
    CHECK(!entry.send(connection.server_id,ScNetChannel::Control,large));
    CHECK(entry.sent_bytes==64800);
    // Drain outgoing messages before filling the application receive ring.
    entry.net->flush();
    unsigned received=0;
    for(int i=0;i<3000 && received<118;++i) {
        application.service();
        if(auto event=poll(client)) { CHECK(event->type==ScNetEventType::Receive); ++received; }
    }
    CHECK(received==118);
    // Ring wrap and bounded failure; reliable messages are never silently skipped.
    for(unsigned batch=0;batch<4;++batch) {
        send_range(batch*64,64); await_count((batch+1)*64);
    }
    auto port=entry.net->port();
    send_range(256,1);
    for(int i=0;i<3000 && entry.net;++i) { application.service(); (void)poll(client); }
    CHECK(!entry.net && entry.fault[0] && entry.queued==0);
    auto failure=entry.poll();
    CHECK(!failure && failure.error().find("queue exhausted")!=std::string::npos);
    CHECK(!entry.send(0,ScNetChannel::Control,bytes("closed")));
    CHECK(ScNet::host("127.0.0.1",port,1).has_value());
}

void test_tokens() {
    ScNetSessions application;
    std::set<std::string> tokens;
    for(unsigned i=0;i<SC_NET_TICK_TOKENS;++i) {
        auto token=application.token(); CHECK(token.has_value());
        CHECK(token->back()==0);
        std::string text(token->data()); CHECK(text.size()==32);
        CHECK(text.find_first_not_of("0123456789abcdef")==std::string::npos);
        CHECK(tokens.insert(text).second);
    }
    CHECK(application.tokens_remaining()==0 && !application.token());
    application.service(); CHECK(!application.token());
    application.begin_tick();
    CHECK(application.tokens_remaining()==64 && application.token().has_value());
    ScNetSessions failing([]() -> ScResult<ScNetToken> { return std::unexpected("injected entropy failure"); });
    auto failed=failing.token();
    CHECK(!failed && failed.error()=="injected entropy failure");
    CHECK(failing.tokens_remaining()==63);
}
} // namespace

int main() {
    test_arguments_and_raii();
    test_messages();
    test_application_queue();
    test_tokens();
    test_capacity_and_timeout();
    std::printf("C++23 network: %u checks passed\n", checks);
}
