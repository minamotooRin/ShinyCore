#pragma once
#include "shiny/state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
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
inline constexpr std::size_t SC_NET_RECEIVE_CAPACITY = 256;
inline constexpr std::size_t SC_NET_TICK_MESSAGES = 64;
inline constexpr std::size_t SC_NET_TICK_BYTES = 64 * 1024;
inline constexpr std::size_t SC_NET_STATE_BYTES = 64 * 1024;
inline constexpr unsigned SC_NET_TICK_TOKENS = 64;
using ScNetToken = std::array<char,33>; // 128 bits, lowercase hex plus NUL.

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
    using TokenSource = ScResult<ScNetToken>(*)();
    explicit ScNetSessions(TokenSource source=nullptr) noexcept;
    struct Entry {
        std::unique_ptr<ScNet> net;
        std::uint64_t generation{};
        // Allocated with the named session, never expanded during service/update.
        std::array<ScNetEvent, SC_NET_RECEIVE_CAPACITY> events{};
        std::size_t head{}, queued{}, readable{};
        std::size_t sent_messages{}, sent_bytes{};
        std::array<char, SC_NET_ERROR_MAX> fault{};
        // Explicit application protocol data; never included in room checkpoints.
        ScValue state;
        std::size_t state_bytes{};
        void service() noexcept;
        void begin_tick() noexcept;
        [[nodiscard]] std::expected<std::optional<ScNetEvent>, std::string> poll();
        [[nodiscard]] std::expected<void, std::string> send(
            std::uint32_t peer, ScNetChannel channel, std::span<const std::uint8_t> data);
    private:
        void fail(const char* message) noexcept;
    };
    std::map<std::string,Entry,std::less<>> entries;
    std::uint64_t next_generation{1};
    // Sampled only at fixed-update boundaries; room changes never reset the epoch.
    [[nodiscard]] double time() const noexcept { return time_; }
    [[nodiscard]] ScResult<ScNetToken> token();
    [[nodiscard]] unsigned tokens_remaining() const noexcept { return SC_NET_TICK_TOKENS-token_attempts_; }
    void service() noexcept;
    void begin_tick() noexcept;
private:
    const std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
    double time_{};
    TokenSource token_source_;
    unsigned token_attempts_{};
};
