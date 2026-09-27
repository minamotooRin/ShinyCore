#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <raylib.h>
class ScScript;
struct ScValue;
// Owned by the graphics backend; absent from builds without devtools.
class ScDebugPanel final {
    int section_{};
    std::size_t offset_{},tree_{};
    std::string named_tree_;
public:
    using Text=void(*)(const ScScript&,std::string_view,int,int,Color);
    ScValue configure(const ScValue&);
    void draw(ScScript&,float frame_interval,bool cycle,bool previous,bool next,bool next_tree,Text);
};
