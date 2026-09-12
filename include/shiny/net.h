#ifndef SHINY_NET_H
#define SHINY_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SC_NET_MAX_PEERS 32
#define SC_NET_MAX_PAYLOAD 1200
#define SC_NET_ERROR_MAX 160
#define SC_NET_MAX_QUEUED 256

typedef struct ScNet ScNet;
typedef enum { SC_NET_CONTROL = 0, SC_NET_STATE = 1 } ScNetChannel;
typedef enum { SC_NET_CONNECT = 1, SC_NET_RECEIVE, SC_NET_DISCONNECT } ScNetEventType;
typedef struct {
    ScNetEventType type;
    uint32_t peer;
    ScNetChannel channel;
    uint32_t reason;
    size_t size;
    unsigned char data[SC_NET_MAX_PAYLOAD];
} ScNetEvent;

/* All instances and their lifetime operations belong to one owner thread.
 * IPv4 addresses must be numeric; host NULL binds all interfaces. Port 0 asks
 * the OS for a free host port. A join is asynchronous: poll both endpoints.
 * On failure, constructors return NULL and fill the optional error buffer. */
ScNet *sc_net_host(const char *bind_ip, uint16_t port, unsigned max_peers,
                   char error[SC_NET_ERROR_MAX]);
ScNet *sc_net_join(const char *ip, uint16_t port, char error[SC_NET_ERROR_MAX]);
/* Immediate cleanup, dropping pending messages; NULL is safe. */
void sc_net_close(ScNet *net);

/* -1 error, 0 no event, 1 event; timeout_ms must be in 0..1000. Event data is
 * copied and remains valid after later polls. Peer IDs are local to this
 * endpoint, never reused during its lifetime, and invalid after disconnect.
 * A connection attempt that fails before CONNECT has DISCONNECT peer == 0. */
int sc_net_poll(ScNet *net, ScNetEvent *event, int timeout_ms);
/* CONTROL is reliable ordered; STATE is unreliable sequenced. Both accept
 * binary messages of 0..1200 bytes. Peer 0 broadcasts to established peers
 * and fails if there are none. Success means queued, not acknowledged.
 * Pending commands are bounded by SC_NET_MAX_QUEUED per peer; backpressure
 * fails explicitly. Poll regularly to service traffic. A broadcast may be partially
 * queued if allocation fails midway. */
bool sc_net_send(ScNet *net, uint32_t peer, ScNetChannel channel,
                 const void *data, size_t size);
void sc_net_flush(ScNet *net);
/* Starts a graceful disconnect; keep polling until DISCONNECT. The peer can
 * no longer be sent to immediately after this call. Reason is application data. */
bool sc_net_disconnect(ScNet *net, uint32_t peer, uint32_t reason);
uint16_t sc_net_port(const ScNet *net);
/* Round-trip estimate in milliseconds, or -1 for an invalid/disconnecting peer. */
int sc_net_rtt(ScNet *net, uint32_t peer);
/* Last operation error; constructors use their output buffer. */
const char *sc_net_error(const ScNet *net);

#endif
