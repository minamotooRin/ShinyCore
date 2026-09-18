#pragma once
#include <expected>
#include <concepts>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

// JSON-shaped, ordered authored data. No VM pointers or native handles.
struct ScValue {
    using Array = std::vector<ScValue>;
    using Object = std::map<std::string, ScValue, std::less<>>;
    std::variant<std::monostate, bool, double, std::string, Array, Object> data;
    ScValue() = default;
    template<class T> requires (std::same_as<T,bool> || std::same_as<T,double> || std::same_as<T,std::string> || std::same_as<T,Array> || std::same_as<T,Object>)
    explicit ScValue(T value) : data(std::move(value)) {}
    const ScValue* get(std::string_view key) const;
    std::string text(std::string fallback = {}) const;
    double number(double fallback = 0) const;
    bool boolean(bool fallback = false) const;
};
template<class T> using ScResult = std::expected<T, std::string>;
inline constexpr std::size_t SC_STATE_BYTES = 256 * 1024;
ScResult<ScValue> sc_json_read(std::string_view json, std::size_t limit = SC_STATE_BYTES,int max_depth=16);
ScResult<ScValue> sc_json_file(const std::string& path, std::size_t limit = SC_STATE_BYTES,int max_depth=16);
ScResult<ScValue> sc_state_validate(const ScValue& value);
std::string sc_json_write(const ScValue& value);
ScResult<void> sc_atomic_write(const std::string& path, std::string_view bytes);
std::string sc_user_data_directory();
std::uint64_t sc_data_hash(const ScValue& value);
