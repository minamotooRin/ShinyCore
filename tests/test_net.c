#include "shiny/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These checks run in Release builds and use actual UDP loopback sockets. */
static unsigned checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static int poll_event(ScNet *net, ScNetEvent *event) {
    int result = sc_net_poll(net, event, 1);
    if (result < 0) fprintf(stderr, "network: %s\n", sc_net_error(net));
    CHECK(result >= 0);
    return result;
}

static ScNet *host(unsigned capacity) {
    char error[SC_NET_ERROR_MAX];
    ScNet *net = sc_net_host("127.0.0.1", 0, capacity, error);
    if (!net) fprintf(stderr, "host: %s\n", error);
    CHECK(net != NULL);
    CHECK(error[0] == '\0');
    CHECK(sc_net_port(net) != 0);
    return net;
}

static ScNet *connect_peer(ScNet *server, uint32_t *server_id, uint32_t *client_id) {
    char error[SC_NET_ERROR_MAX];
    ScNet *client = sc_net_join("127.0.0.1", sc_net_port(server), error);
    CHECK(client != NULL);
    CHECK(error[0] == '\0');
    CHECK(sc_net_port(client) != 0);
    *server_id = *client_id = 0;
    for (int step = 0; step < 3000 && (!*server_id || !*client_id); ++step) {
        ScNetEvent event;
        if (poll_event(server, &event)) {
            CHECK(event.type == SC_NET_CONNECT);
            *server_id = event.peer;
        }
        if (poll_event(client, &event)) {
            CHECK(event.type == SC_NET_CONNECT);
            *client_id = event.peer;
        }
    }
    CHECK(*server_id != 0 && *client_id != 0);
    CHECK(sc_net_rtt(client, *client_id) >= 0);
    return client;
}

static void test_invalid_and_lifetime(void) {
    char error[SC_NET_ERROR_MAX];
    CHECK(sc_net_host(NULL, 0, 0, error) == NULL && error[0]);
    CHECK(sc_net_host(NULL, 0, SC_NET_MAX_PEERS + 1, error) == NULL && error[0]);
    CHECK(sc_net_host("localhost", 0, 1, error) == NULL && error[0]);
    CHECK(sc_net_host("256.1.2.3", 0, 1, error) == NULL && error[0]);
    CHECK(sc_net_join(NULL, 1234, error) == NULL && error[0]);
    CHECK(sc_net_join("localhost", 1234, error) == NULL && error[0]);
    CHECK(sc_net_join("127.0.0.1", 0, error) == NULL && error[0]);
    CHECK(sc_net_join("0.0.0.0", 1234, error) == NULL && error[0]);
    CHECK(sc_net_join("255.255.255.255", 1234, error) == NULL && error[0]);
    CHECK(sc_net_port(NULL) == 0);
    CHECK(sc_net_rtt(NULL, 1) == -1);
    CHECK(!sc_net_send(NULL, 1, SC_NET_CONTROL, "a", 1));
    CHECK(!sc_net_disconnect(NULL, 1, 0));
    CHECK(sc_net_error(NULL)[0]);
    sc_net_close(NULL);
    sc_net_flush(NULL);

    ScNet *net = host(1);
    ScNetEvent event;
    CHECK(sc_net_poll(NULL, &event, 0) == -1);
    CHECK(sc_net_poll(net, NULL, 0) == -1);
    CHECK(sc_net_poll(net, &event, -1) == -1);
    CHECK(sc_net_poll(net, &event, 1001) == -1);
    CHECK(sc_net_poll(net, &event, 0) == 0);
    CHECK(event.type == 0 && event.size == 0 && event.peer == 0);
    CHECK(!sc_net_send(net, 0, SC_NET_CONTROL, "a", 1));
    CHECK(strstr(sc_net_error(net), "no connected peers") != NULL);
    CHECK(!sc_net_disconnect(net, 0, 0));
    CHECK(sc_net_rtt(net, 0) == -1);
    uint16_t port = sc_net_port(net);
    CHECK(sc_net_host("127.0.0.1", port, 1, error) == NULL && error[0]);
    sc_net_close(net);
    net = sc_net_host("127.0.0.1", port, 1, error);
    CHECK(net != NULL && sc_net_port(net) == port);
    sc_net_close(net);
    net = sc_net_host(NULL, 0, SC_NET_MAX_PEERS, error);
    CHECK(net != NULL);
    sc_net_close(net);
}

static void test_messages_and_disconnect(void) {
    ScNet *server = host(2);
    uint32_t first_id, first_server, second_id, second_server;
    ScNet *first = connect_peer(server, &first_id, &first_server);
    ScNet *second = connect_peer(server, &second_id, &second_server);
    CHECK(first_id != second_id);
    CHECK(!sc_net_send(first, first_server, (ScNetChannel)2, "a", 1));
    CHECK(!sc_net_send(first, first_server, SC_NET_CONTROL, NULL, 1));
    unsigned char payload[SC_NET_MAX_PAYLOAD + 1];
    memset(payload, 0xab, sizeof(payload));
    CHECK(!sc_net_send(first, first_server, SC_NET_CONTROL, payload, sizeof(payload)));
    CHECK(!sc_net_send(first, UINT32_MAX, SC_NET_CONTROL, payload, 1));
    CHECK(sc_net_send(first, first_server, SC_NET_CONTROL, payload, SC_NET_MAX_PAYLOAD));
    CHECK(sc_net_send(first, first_server, SC_NET_CONTROL, NULL, 0));
    for (unsigned i = 0; i < 64; ++i) {
        unsigned char binary[] = { (unsigned char)i, 0, 0xff };
        CHECK(sc_net_send(first, first_server, SC_NET_CONTROL, binary, sizeof(binary)));
    }
    sc_net_flush(first);
    unsigned received = 0;
    ScNetEvent saved = {0};
    for (int step = 0; step < 3000 && received < 66; ++step) {
        ScNetEvent event;
        CHECK(!poll_event(first, &event));
        CHECK(!poll_event(second, &event));
        if (!poll_event(server, &event)) continue;
        CHECK(event.type == SC_NET_RECEIVE && event.peer == first_id);
        CHECK(event.channel == SC_NET_CONTROL);
        if (received == 0) {
            CHECK(event.size == SC_NET_MAX_PAYLOAD);
            CHECK(memcmp(event.data, payload, event.size) == 0);
            saved = event;
        } else if (received == 1) {
            CHECK(event.size == 0);
        } else {
            CHECK(event.size == 3 && event.data[0] == received - 2);
            CHECK(event.data[1] == 0 && event.data[2] == 0xff);
        }
        ++received;
    }
    CHECK(received == 66);
    CHECK(saved.size == SC_NET_MAX_PAYLOAD && saved.data[0] == 0xab);

    CHECK(sc_net_send(server, first_id, SC_NET_CONTROL, "private", 7));
    unsigned first_received = 0;
    for (int step = 0; step < 100; ++step) {
        ScNetEvent event;
        CHECK(!poll_event(server, &event));
        CHECK(!poll_event(second, &event));
        if (poll_event(first, &event)) {
            CHECK(event.type == SC_NET_RECEIVE && event.peer == first_server);
            CHECK(event.size == 7 && memcmp(event.data, "private", 7) == 0);
            ++first_received;
        }
    }
    CHECK(first_received == 1);

    CHECK(sc_net_send(server, 0, SC_NET_CONTROL, "all", 3));
    CHECK(sc_net_send(server, 0, SC_NET_STATE, payload, SC_NET_MAX_PAYLOAD));
    unsigned totals[2] = {0};
    ScNet *clients[] = { first, second };
    for (int step = 0; step < 3000 && (totals[0] != 3 || totals[1] != 3); ++step) {
        ScNetEvent event;
        CHECK(!poll_event(server, &event));
        for (unsigned i = 0; i < 2; ++i) {
            if (!poll_event(clients[i], &event)) continue;
            CHECK(event.type == SC_NET_RECEIVE);
            if (event.channel == SC_NET_CONTROL) {
                CHECK(event.size == 3 && memcmp(event.data, "all", 3) == 0);
                CHECK(!(totals[i] & 1));
                totals[i] |= 1;
            } else {
                CHECK(event.channel == SC_NET_STATE && event.size == SC_NET_MAX_PAYLOAD);
                CHECK(memcmp(event.data, payload, event.size) == 0);
                CHECK(!(totals[i] & 2));
                totals[i] |= 2;
            }
        }
    }
    CHECK(totals[0] == 3 && totals[1] == 3);

    for (unsigned char sequence = 0; sequence < 16; ++sequence)
        CHECK(sc_net_send(server, first_id, SC_NET_STATE, &sequence, 1));
    int latest = -1;
    for (int step = 0; step < 3000 && latest < 15; ++step) {
        ScNetEvent event;
        CHECK(!poll_event(server, &event));
        CHECK(!poll_event(second, &event));
        if (poll_event(first, &event)) {
            CHECK(event.type == SC_NET_RECEIVE && event.channel == SC_NET_STATE);
            CHECK(event.size == 1 && event.data[0] > latest);
            latest = event.data[0];
        }
    }
    CHECK(latest == 15); /* Gaps are allowed; stale state is never delivered. */

    unsigned queued = 0;
    while (sc_net_send(first, first_server, SC_NET_CONTROL, "q", 1)) {
        ++queued;
        CHECK(queued <= SC_NET_MAX_QUEUED);
    }
    CHECK(queued > 0 && strstr(sc_net_error(first), "queue is full"));
    unsigned drained = 0;
    for (int step = 0; step < 3000 && drained < queued; ++step) {
        ScNetEvent event;
        CHECK(!poll_event(first, &event));
        if (poll_event(server, &event)) {
            CHECK(event.type == SC_NET_RECEIVE && event.peer == first_id);
            CHECK(event.size == 1 && event.data[0] == 'q');
            ++drained;
        }
    }
    CHECK(drained == queued);
    /* Service the final acknowledgements, then exercise queue recovery. */
    for (int step = 0; step < 10; ++step) {
        ScNetEvent event;
        CHECK(!poll_event(server, &event));
        CHECK(!poll_event(first, &event));
    }
    CHECK(sc_net_send(first, first_server, SC_NET_CONTROL, "bye", 3));
    CHECK(sc_net_disconnect(first, first_server, 77));
    CHECK(!sc_net_disconnect(first, first_server, 77));
    CHECK(!sc_net_send(first, first_server, SC_NET_CONTROL, "late", 4));
    CHECK(sc_net_rtt(first, first_server) == -1);
    bool goodbye = false, first_closed = false, server_closed = false;
    for (int step = 0; step < 3000 && (!first_closed || !server_closed); ++step) {
        ScNetEvent event;
        if (poll_event(server, &event)) {
            CHECK(event.peer == first_id);
            if (event.type == SC_NET_RECEIVE) {
                CHECK(!goodbye && event.size == 3 && memcmp(event.data, "bye", 3) == 0);
                goodbye = true;
            } else {
                CHECK(goodbye && event.type == SC_NET_DISCONNECT && event.reason == 77);
                server_closed = true;
            }
        }
        if (poll_event(first, &event)) {
            CHECK(event.type == SC_NET_DISCONNECT && event.peer == first_server);
            first_closed = true;
        }
    }
    CHECK(goodbye && first_closed && server_closed);
    CHECK(!sc_net_send(server, first_id, SC_NET_CONTROL, "stale", 5));
    CHECK(!sc_net_disconnect(server, first_id, 0));
    CHECK(sc_net_rtt(server, first_id) == -1);
    sc_net_close(first);

    uint32_t replacement_id, replacement_server;
    first = connect_peer(server, &replacement_id, &replacement_server);
    CHECK(replacement_id != first_id && replacement_id != second_id);
    CHECK(!sc_net_send(server, first_id, SC_NET_CONTROL, "stale", 5));
    CHECK(sc_net_send(server, second_id, SC_NET_CONTROL, "alive", 5));
    bool alive = false;
    for (int step = 0; step < 3000 && !alive; ++step) {
        ScNetEvent event;
        CHECK(!poll_event(server, &event));
        if (poll_event(second, &event)) {
            CHECK(event.type == SC_NET_RECEIVE && event.peer == second_server);
            CHECK(event.size == 5 && memcmp(event.data, "alive", 5) == 0);
            alive = true;
        }
    }
    CHECK(alive);
    sc_net_close(server);
    sc_net_close(first);
    sc_net_close(second);
}

static void test_capacity_and_join_timeout(void) {
    ScNet *server = host(1);
    uint32_t server_id, client_id;
    ScNet *first = connect_peer(server, &server_id, &client_id);
    char error[SC_NET_ERROR_MAX];
    ScNet *excess = sc_net_join("127.0.0.1", sc_net_port(server), error);
    CHECK(excess != NULL);
    CHECK(!sc_net_send(excess, 0, SC_NET_CONTROL, "early", 5));
    for (int step = 0; step < 50; ++step) {
        ScNetEvent event;
        CHECK(!poll_event(server, &event));
        CHECK(!poll_event(first, &event));
        CHECK(!poll_event(excess, &event));
    }
    sc_net_close(excess);
    sc_net_close(first);
    uint16_t unavailable = sc_net_port(server);
    sc_net_close(server);

    ScNet *joining = sc_net_join("127.0.0.1", unavailable, error);
    CHECK(joining != NULL);
    bool failed = false;
    /* The 10s threshold is checked at ENet's next exponential retry. */
    for (int step = 0; step < 24 && !failed; ++step) {
        ScNetEvent event;
        int result = sc_net_poll(joining, &event, 1000);
        CHECK(result >= 0);
        if (result) {
            CHECK(event.type == SC_NET_DISCONNECT && event.peer == 0);
            failed = true;
        }
    }
    CHECK(failed);
    CHECK(!sc_net_send(joining, 0, SC_NET_CONTROL, "late", 4));
    sc_net_close(joining);
}

int main(void) {
    test_invalid_and_lifetime();
    test_messages_and_disconnect();
    test_capacity_and_join_timeout();
    printf("network: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
