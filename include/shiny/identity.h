#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

enum class ScIdentityStatus { absent, unloaded, active, deleted };
struct ScIdentityRecord {
    char name[128]{};
    std::uint64_t entity{};
    ScIdentityStatus status{ScIdentityStatus::absent};
};
bool sc_identity_name_valid(std::string_view) noexcept;
const char* sc_identity_status_name(ScIdentityStatus) noexcept;

// Fixed storage and open addressing. Inactive records retain deletion/load state
// until the room ends; exhaustion never grows memory during simulation.
class ScIdentities final {
    std::vector<ScIdentityRecord> slots_;
    std::size_t capacity_{}, count_{};
    std::size_t slot(std::string_view) const noexcept;
public:
    explicit ScIdentities(std::size_t capacity);
    ScIdentities(const ScIdentities&)=delete;
    ScIdentities& operator=(const ScIdentities&)=delete;
    const ScIdentityRecord* find(std::string_view) const noexcept;
    std::expected<void,const char*> declare(std::string_view);
    std::expected<void,const char*> bind(std::string_view,std::uint64_t entity);
    std::expected<void,const char*> erase(std::string_view);
    void release(std::string_view,std::uint64_t entity,bool deleted) noexcept;
    std::size_t size() const noexcept { return count_; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::span<const ScIdentityRecord> records() const noexcept { return slots_; }
};
