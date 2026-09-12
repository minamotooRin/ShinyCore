#include "shiny/net.h"

#include <enet/enet.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t id; bool closing; } ScNetPeer;
struct ScNet {
    ENetHost *host;
    ScNetPeer peers[SC_NET_MAX_PEERS];
    uint32_t next_id;
    char error[SC_NET_ERROR_MAX];
};

/* The module has one owner thread, including ENet initialization/teardown. */
static unsigned instances;

static bool fail(ScNet *net, const char *message) {
    if (net) snprintf(net->error, sizeof(net->error), "%s", message);
    return false;
}

/* ENet normally aborts on allocation failure; its callers also check NULL. */
static void no_memory(void) { }

static bool acquire(void) {
    if (!instances) {
        ENetCallbacks callbacks = { malloc, free, no_memory };
        if (enet_initialize_with_callbacks(ENET_VERSION, &callbacks) < 0)
            return false;
    }
    if (instances == UINT_MAX) return false;
    ++instances;
    return true;
}

static void release(void) {
    if (--instances == 0) enet_deinitialize();
}

static ScNet *open_host(const ENetAddress *address, unsigned max_peers,
                        char error[SC_NET_ERROR_MAX]) {
    if (!acquire()) {
        if (error) snprintf(error, SC_NET_ERROR_MAX, "cannot initialize ENet");
        return NULL;
    }
    ScNet *net = calloc(1, sizeof(*net));
    if (net) net->host = enet_host_create(address, max_peers, 2, 0, 0);
    if (!net || !net->host) {
        free(net);
        release();
        if (error) snprintf(error, SC_NET_ERROR_MAX, "cannot allocate or bind UDP endpoint");
        return NULL;
    }
    net->next_id = 1;
    net->host->maximumPacketSize = SC_NET_MAX_PAYLOAD;
    net->host->maximumWaitingData = SC_NET_MAX_PAYLOAD * SC_NET_MAX_QUEUED;
    if (error) error[0] = '\0';
    return net;
}

ScNet *sc_net_host(const char *bind_ip, uint16_t port, unsigned max_peers,
                   char error[SC_NET_ERROR_MAX]) {
    ENetAddress address = { ENET_HOST_ANY, port };
    if (max_peers < 1 || max_peers > SC_NET_MAX_PEERS) {
        if (error) snprintf(error, SC_NET_ERROR_MAX, "peer capacity must be 1..%d", SC_NET_MAX_PEERS);
        return NULL;
    }
    if (bind_ip && enet_address_set_host_ip(&address, bind_ip) < 0) {
        if (error) snprintf(error, SC_NET_ERROR_MAX, "bind address must be numeric IPv4");
        return NULL;
    }
    return open_host(&address, max_peers, error);
}

ScNet *sc_net_join(const char *ip, uint16_t port, char error[SC_NET_ERROR_MAX]) {
    ENetAddress remote = { 0, port }, local = { ENET_HOST_ANY, 0 };
    if (!ip || !port || enet_address_set_host_ip(&remote, ip) < 0 ||
        remote.host == ENET_HOST_ANY || remote.host == ENET_HOST_BROADCAST) {
        if (error) snprintf(error, SC_NET_ERROR_MAX, "join requires a numeric IPv4 address and nonzero port");
        return NULL;
    }
    ScNet *net = open_host(&local, 1, error);
    if (!net) return NULL;
    ENetPeer *peer = enet_host_connect(net->host, &remote, 2, 0);
    if (!peer) {
        sc_net_close(net);
        if (error) snprintf(error, SC_NET_ERROR_MAX, "cannot allocate connection");
        return NULL;
    }
    /* ENet checks these thresholds on its exponential retry schedule. */
    enet_peer_timeout(peer, 32, 3000, 10000);
    enet_host_flush(net->host);
    return net;
}

void sc_net_close(ScNet *net) {
    if (!net) return;
    enet_host_destroy(net->host);
    free(net);
    release();
}

static ENetPeer *find_peer(ScNet *net, uint32_t id) {
    if (net && id) {
        for (size_t i = 0; i < net->host->peerCount; ++i) {
            if (net->peers[i].id == id && !net->peers[i].closing &&
                net->host->peers[i].state == ENET_PEER_STATE_CONNECTED)
                return &net->host->peers[i];
        }
    }
    fail(net, "peer is not connected");
    return NULL;
}

int sc_net_poll(ScNet *net, ScNetEvent *event, int timeout_ms) {
    if (event) memset(event, 0, sizeof(*event));
    if (!net || !event || timeout_ms < 0 || timeout_ms > 1000) {
        fail(net, "poll requires an event and timeout in 0..1000 ms");
        return -1;
    }
    net->error[0] = '\0';
    ENetEvent incoming;
    int result = enet_host_service(net->host, &incoming, (enet_uint32)timeout_ms);
    if (result <= 0) {
        if (result < 0) fail(net, "UDP service failed");
        return result < 0 ? -1 : 0;
    }
    size_t slot = (size_t)(incoming.peer - net->host->peers);
    ScNetPeer *peer = &net->peers[slot];
    event->peer = peer->id;
    switch (incoming.type) {
    case ENET_EVENT_TYPE_CONNECT:
        if (!net->next_id || incoming.peer->channelCount != 2) {
            enet_peer_disconnect_now(incoming.peer, 0);
            fail(net, !net->next_id ? "peer ID capacity exhausted" : "peer must support two channels");
            return -1;
        }
        peer->id = net->next_id++;
        peer->closing = false;
        enet_peer_timeout(incoming.peer, 32, 3000, 10000);
        event->type = SC_NET_CONNECT;
        event->peer = peer->id;
        return 1;
    case ENET_EVENT_TYPE_RECEIVE:
        if (!peer->id || incoming.channelID > SC_NET_STATE ||
            incoming.packet->dataLength > SC_NET_MAX_PAYLOAD) {
            enet_packet_destroy(incoming.packet);
            fail(net, "invalid incoming message");
            return -1;
        }
        event->type = SC_NET_RECEIVE;
        event->channel = (ScNetChannel)incoming.channelID;
        event->size = incoming.packet->dataLength;
        if (event->size) memcpy(event->data, incoming.packet->data, event->size);
        enet_packet_destroy(incoming.packet);
        return 1;
    case ENET_EVENT_TYPE_DISCONNECT:
        event->type = SC_NET_DISCONNECT;
        event->reason = incoming.data;
        memset(peer, 0, sizeof(*peer));
        return 1;
    default:
        return 0;
    }
}

static bool can_queue(ScNet *net, ENetPeer *peer) {
    size_t queued = enet_list_size(&peer->outgoingCommands) +
                    enet_list_size(&peer->outgoingSendReliableCommands) +
                    enet_list_size(&peer->sentReliableCommands);
    /* A 1200-byte message takes at most three fragments at ENet's min MTU. */
    return queued <= SC_NET_MAX_QUEUED - 3 || fail(net, "peer send queue is full; poll to service traffic");
}

bool sc_net_send(ScNet *net, uint32_t id, ScNetChannel channel,
                 const void *data, size_t size) {
    if (!net) return false;
    net->error[0] = '\0';
    if ((channel != SC_NET_CONTROL && channel != SC_NET_STATE) ||
        size > SC_NET_MAX_PAYLOAD || (!data && size))
        return fail(net, "invalid channel, payload, or message exceeds 1200 bytes");

    ENetPeer *targets[SC_NET_MAX_PEERS];
    size_t count = 0;
    if (id) {
        ENetPeer *peer = find_peer(net, id);
        if (!peer) return false;
        targets[count++] = peer;
    } else {
        for (size_t i = 0; i < net->host->peerCount; ++i) {
            if (net->peers[i].id && !net->peers[i].closing &&
                net->host->peers[i].state == ENET_PEER_STATE_CONNECTED)
                targets[count++] = &net->host->peers[i];
        }
        if (!count) return fail(net, "no connected peers");
    }
    for (size_t i = 0; i < count; ++i)
        if (!can_queue(net, targets[i])) return false;

    enet_uint32 flags = channel == SC_NET_CONTROL ? ENET_PACKET_FLAG_RELIABLE :
                                                   ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
    ENetPacket *packet = enet_packet_create(data, size, flags);
    if (!packet) return fail(net, "cannot allocate message");
    for (size_t i = 0; i < count; ++i) {
        if (enet_peer_send(targets[i], (enet_uint8)channel, packet) < 0) {
            if (!packet->referenceCount) enet_packet_destroy(packet);
            return fail(net, "cannot queue message; broadcast may be partial");
        }
    }
    return true;
}

void sc_net_flush(ScNet *net) {
    if (net) enet_host_flush(net->host);
}

bool sc_net_disconnect(ScNet *net, uint32_t id, uint32_t reason) {
    ENetPeer *peer = find_peer(net, id);
    if (!peer) return false;
    net->error[0] = '\0';
    net->peers[(size_t)(peer - net->host->peers)].closing = true;
    enet_peer_disconnect_later(peer, reason);
    return true;
}

uint16_t sc_net_port(const ScNet *net) {
    return net ? net->host->address.port : 0;
}

int sc_net_rtt(ScNet *net, uint32_t id) {
    ENetPeer *peer = find_peer(net, id);
    if (!peer) return -1;
    net->error[0] = '\0';
    return peer->roundTripTime > INT_MAX ? INT_MAX : (int)peer->roundTripTime;
}

const char *sc_net_error(const ScNet *net) {
    return net ? net->error : "network endpoint is null";
}
