#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <map>

inline constexpr unsigned SC_NET_MAX_PEERS = 32;
inline constexpr std::size_t SC_NET_MAX_PAYLOAD = 1200;
inline constexpr std::size_t SC_NET_MAX_QUEUED = 256;
inline constexpr std::size_t SC_NET_ERROR_MAX = 160;

enum class ScNetChannel { Control = 0, State = 1 };
enum class ScNetEventType { Connect, Receive, Disconnect };
struct ScNetEvent {
    ScNetEventType type{};
    std::uint32_t peer{};
    ScNetChannel channel{};
    std::uint32_t reason{};
    std::size_t size{};
    std::array<std::uint8_t, SC_NET_MAX_PAYLOAD> data{};
};

// All endpoints and their lifetimes belong to one owner thread. This boundary
// owns ENet resources; it has no dependency on a world, window, or Lua runtime.
class ScNet final {
public:
    // Numeric IPv4 only. Empty bind_ip listens on all interfaces; host port 0
    // requests a free port. Join is asynchronous: poll both endpoints.
    [[nodiscard]] static std::expected<std::unique_ptr<ScNet>, std::string>
        host(std::string_view bind_ip, std::uint16_t port, unsigned max_peers);
    [[nodiscard]] static std::expected<std::unique_ptr<ScNet>, std::string>
        join(std::string_view ip, std::uint16_t port);
    ~ScNet(); // Immediate cleanup drops pending messages.
    ScNet(const ScNet&) = delete;
    ScNet& operator=(const ScNet&) = delete;
    ScNet(ScNet&&) = delete;
    ScNet& operator=(ScNet&&) = delete;

    // Empty optional means no event; timeout must be 0..1000 ms. Events own
    // their data. Local peer IDs never repeat during an endpoint's lifetime
    // and expire after disconnect. Failed joins emit Disconnect with peer 0.
    [[nodiscard]] std::expected<std::optional<ScNetEvent>, std::string>
        poll(int timeout_ms = 0);
    // Control: reliable ordered; State: unreliable sequenced. Binary payloads
    // are 0..1200 bytes. Peer 0 broadcasts and fails with no established peers.
    // Success means queued. Bounded outgoing queues apply explicit backpressure;
    // allocation failure during broadcast can leave a partially queued send.
    [[nodiscard]] std::expected<void, std::string>
        send(std::uint32_t peer, ScNetChannel channel, std::span<const std::uint8_t> data);
    void flush() noexcept;
    // Drains queued messages before disconnect; new sends fail immediately.
    [[nodiscard]] std::expected<void, std::string>
        disconnect(std::uint32_t peer, std::uint32_t reason = 0);
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::expected<int, std::string> rtt(std::uint32_t peer) const;

private:
    struct Impl;
    explicit ScNet(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

// Application lifetime; room VMs borrow named bindings without owning sockets.
struct ScNetSessions {
    struct Entry { std::unique_ptr<ScNet> net; std::uint64_t generation{}; };
    std::map<std::string,Entry,std::less<>> entries;
    std::uint64_t next_generation{1};
};
