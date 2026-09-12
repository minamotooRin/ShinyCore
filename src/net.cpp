#include "shiny/net.h"

#include <enet/enet.h>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <utility>

namespace {
// ENet globals share the same owner thread as every endpoint. The lease outlives
// its native host so Winsock teardown always follows the last socket's cleanup.
class EnetRuntime {
public:
    static std::expected<EnetRuntime, std::string> acquire() {
        if (!instances_) {
            // ENet normally aborts on OOM, although callers also check nullptr.
            ENetCallbacks callbacks{std::malloc, std::free, [] {}};
            if (enet_initialize_with_callbacks(ENET_VERSION, &callbacks) < 0)
                return std::unexpected("cannot initialize ENet");
        }
        if (instances_ == UINT_MAX) return std::unexpected("endpoint capacity exhausted");
        ++instances_;
        return EnetRuntime{};
    }
    ~EnetRuntime() { if (active_ && --instances_ == 0) enet_deinitialize(); }
    EnetRuntime(EnetRuntime&& other) noexcept : active_(std::exchange(other.active_, false)) {}
    EnetRuntime(const EnetRuntime&) = delete;
    EnetRuntime& operator=(const EnetRuntime&) = delete;
    EnetRuntime& operator=(EnetRuntime&&) = delete;
private:
    EnetRuntime() = default;
    inline static unsigned instances_{};
    bool active_{true};
};

using NativeHost = std::unique_ptr<ENetHost, decltype(&enet_host_destroy)>;
using NativePacket = std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)>;

std::expected<ENetAddress, std::string> address(std::string_view ip,
                                             std::uint16_t port, bool allow_any) {
    ENetAddress result{ENET_HOST_ANY, port};
    if (ip.empty() && allow_any) return result;
    if (ip.empty() || ip.size() > 15 || ip.contains('\0'))
        return std::unexpected("address must be numeric IPv4");
    std::array<char, 16> terminated{};
    std::ranges::copy(ip, terminated.begin());
    if (enet_address_set_host_ip(&result, terminated.data()) < 0 ||
        (!allow_any && (result.host == ENET_HOST_ANY || result.host == ENET_HOST_BROADCAST)))
        return std::unexpected("address must be numeric IPv4");
    return result;
}

bool can_queue(ENetPeer& peer) {
    const auto queued = enet_list_size(&peer.outgoingCommands) +
                        enet_list_size(&peer.outgoingSendReliableCommands) +
                        enet_list_size(&peer.sentReliableCommands);
    // A 1200-byte message takes at most three fragments at ENet's min MTU.
    return queued <= SC_NET_MAX_QUEUED - 3;
}
} // namespace

struct ScNet::Impl {
    struct Peer { std::uint32_t id{}; bool closing{}; };
    EnetRuntime runtime;
    NativeHost host;
    std::array<Peer, SC_NET_MAX_PEERS> peers{};
    std::uint32_t next_id{1};

    Impl(EnetRuntime lease, NativeHost native) : runtime(std::move(lease)), host(std::move(native)) {}

    static std::expected<std::unique_ptr<Impl>, std::string>
    create(const ENetAddress& local, unsigned capacity) {
        auto lease = EnetRuntime::acquire();
        if (!lease) return std::unexpected(std::move(lease.error()));
        NativeHost native{enet_host_create(&local, capacity, 2, 0, 0), enet_host_destroy};
        if (!native) return std::unexpected("cannot allocate or bind UDP endpoint");
        native->maximumPacketSize = SC_NET_MAX_PAYLOAD;
        native->maximumWaitingData = SC_NET_MAX_PAYLOAD * SC_NET_MAX_QUEUED;
        return std::make_unique<Impl>(std::move(*lease), std::move(native));
    }

    ENetPeer* find_peer(std::uint32_t id) const noexcept {
        if (id) {
            for (std::size_t i = 0; i < host->peerCount; ++i)
                if (peers[i].id == id && !peers[i].closing &&
                    host->peers[i].state == ENET_PEER_STATE_CONNECTED)
                    return &host->peers[i];
        }
        return nullptr;
    }
};

ScNet::ScNet(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ScNet::~ScNet() = default;

std::expected<std::unique_ptr<ScNet>, std::string>
ScNet::host(std::string_view bind_ip, std::uint16_t port, unsigned max_peers) {
    try {
        if (max_peers < 1 || max_peers > SC_NET_MAX_PEERS)
            return std::unexpected("peer capacity must be 1..32");
        auto local = address(bind_ip, port, true);
        if (!local) return std::unexpected(std::move(local.error()));
        auto impl = Impl::create(*local, max_peers);
        if (!impl) return std::unexpected(std::move(impl.error()));
        return std::unique_ptr<ScNet>(new ScNet(std::move(*impl)));
    } catch (const std::bad_alloc&) {
        return std::unexpected("cannot allocate endpoint");
    }
}

std::expected<std::unique_ptr<ScNet>, std::string>
ScNet::join(std::string_view ip, std::uint16_t port) {
    if (!port) return std::unexpected("join requires a nonzero port");
    auto remote = address(ip, port, false);
    if (!remote) return std::unexpected(std::move(remote.error()));
    auto result = host({}, 0, 1);
    if (!result) return result;
    auto& native = (*result)->impl_->host;
    auto* peer = enet_host_connect(native.get(), &*remote, 2, 0);
    if (!peer) return std::unexpected("cannot allocate connection");
    // ENet checks these thresholds on its exponential retry schedule.
    enet_peer_timeout(peer, 32, 3000, 10000);
    enet_host_flush(native.get());
    return result;
}

std::expected<std::optional<ScNetEvent>, std::string> ScNet::poll(int timeout_ms) {
    if (timeout_ms < 0 || timeout_ms > 1000)
        return std::unexpected("poll timeout must be in 0..1000 ms");
    ENetEvent incoming{};
    const int result = enet_host_service(impl_->host.get(), &incoming,
                                         static_cast<enet_uint32>(timeout_ms));
    if (result < 0) return std::unexpected("UDP service failed");
    if (!result) return std::nullopt;
    auto& peer = impl_->peers[static_cast<std::size_t>(incoming.peer - impl_->host->peers)];
    ScNetEvent event{.peer = peer.id};
    switch (incoming.type) {
    case ENET_EVENT_TYPE_CONNECT:
        if (!impl_->next_id || incoming.peer->channelCount != 2) {
            enet_peer_disconnect_now(incoming.peer, 0);
            return std::unexpected(!impl_->next_id ? "peer ID capacity exhausted" :
                                                    "peer must support two channels");
        }
        peer = {.id = impl_->next_id++};
        enet_peer_timeout(incoming.peer, 32, 3000, 10000);
        event.type = ScNetEventType::Connect;
        event.peer = peer.id;
        return event;
    case ENET_EVENT_TYPE_RECEIVE: {
        NativePacket packet{incoming.packet, enet_packet_destroy};
        if (!peer.id || incoming.channelID > static_cast<unsigned>(ScNetChannel::State) ||
            packet->dataLength > SC_NET_MAX_PAYLOAD)
            return std::unexpected("invalid incoming message");
        event.type = ScNetEventType::Receive;
        event.channel = static_cast<ScNetChannel>(incoming.channelID);
        event.size = packet->dataLength;
        if (event.size) std::copy_n(packet->data, event.size, event.data.begin());
        return event;
    }
    case ENET_EVENT_TYPE_DISCONNECT:
        event.type = ScNetEventType::Disconnect;
        event.reason = incoming.data;
        peer = {};
        return event;
    default:
        return std::nullopt;
    }
}

std::expected<void, std::string> ScNet::send(std::uint32_t id, ScNetChannel channel,
                                           std::span<const std::uint8_t> data) {
    if ((channel != ScNetChannel::Control && channel != ScNetChannel::State) ||
        data.size() > SC_NET_MAX_PAYLOAD)
        return std::unexpected("invalid channel or message exceeds 1200 bytes");
    std::array<ENetPeer*, SC_NET_MAX_PEERS> targets{};
    std::size_t count{};
    if (id) {
        auto* peer = impl_->find_peer(id);
        if (!peer) return std::unexpected("peer is not connected");
        targets[count++] = peer;
    } else {
        for (std::size_t i = 0; i < impl_->host->peerCount; ++i)
            if (auto* peer = impl_->find_peer(impl_->peers[i].id)) targets[count++] = peer;
        if (!count) return std::unexpected("no connected peers");
    }
    const auto connected = std::span{targets}.first(count);
    for (auto* peer : connected)
        if (!can_queue(*peer))
            return std::unexpected("peer send queue is full; poll to service traffic");
    const enet_uint32 flags = channel == ScNetChannel::Control ? ENET_PACKET_FLAG_RELIABLE :
                                                               ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
    NativePacket packet{enet_packet_create(data.data(), data.size(), flags), enet_packet_destroy};
    if (!packet) return std::unexpected("cannot allocate message");
    auto* message = packet.get();
    for (auto* peer : connected) {
        if (enet_peer_send(peer, static_cast<enet_uint8>(channel), message) < 0)
            return std::unexpected("cannot queue message; broadcast may be partial");
        // ENet owns the shared packet after the first successful enqueue.
        if (packet) (void)packet.release();
    }
    return {};
}

void ScNet::flush() noexcept { enet_host_flush(impl_->host.get()); }

std::expected<void, std::string> ScNet::disconnect(std::uint32_t id, std::uint32_t reason) {
    auto* peer = impl_->find_peer(id);
    if (!peer) return std::unexpected("peer is not connected");
    impl_->peers[static_cast<std::size_t>(peer - impl_->host->peers)].closing = true;
    enet_peer_disconnect_later(peer, reason);
    return {};
}

std::uint16_t ScNet::port() const noexcept { return impl_->host->address.port; }

std::expected<int, std::string> ScNet::rtt(std::uint32_t id) const {
    const auto* peer = impl_->find_peer(id);
    if (!peer) return std::unexpected("peer is not connected");
    return static_cast<int>(std::min<enet_uint32>(peer->roundTripTime, INT_MAX));
}
