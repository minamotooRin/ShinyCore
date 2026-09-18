#pragma once
#include <filesystem>
#include <string_view>
inline bool sc_relative_path(std::string_view path) {
    if(path.empty()||path.front()=='/') return false;
    std::size_t start=0;
    for(std::size_t i=0;i<=path.size();++i) {
        const unsigned char c=i<path.size()?static_cast<unsigned char>(path[i]):0;
        if(i<path.size()&&(c=='\\'||c==':'||c<32||c==127)) return false;
        if(i==path.size()||c=='/') {
            auto part=path.substr(start,i-start);
            if(part.empty()||part=="."||part=="..") return false;
            start=i+1;
        }
    }
    return true;
}
inline std::filesystem::path sc_path(std::string_view utf8) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()),utf8.size()));
}
