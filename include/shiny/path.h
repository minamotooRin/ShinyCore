#pragma once
#include <filesystem>
#include <string_view>
inline std::filesystem::path sc_path(std::string_view utf8) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()),utf8.size()));
}
