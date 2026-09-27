#pragma once
#include "shiny/core.h"
#include "shiny/lighting.h"
#include <expected>

struct ScLightPoint { float x{},y{},distance{}; };
struct ScLightBounds { float x{},y{},w{},h{}; };

// CPU presentation data only. No solver objects, Lua, graphics or world mutation.
// Storage is sized at construction and reused for every frame and light.
class ScOcclusion final {
public:
    static constexpr std::size_t default_capacity=ScLighting::occluder_capacity, curve_vertices=16, radial_rays=128;
    explicit ScOcclusion(std::size_t capacity=default_capacity);
    void clear() noexcept { count_=required_=0; }
    std::expected<void,const char*> collect(const ScWorld&,ScLightBounds,std::span<const ScOccluderOverride> modes={},float alpha=1);
    float distance(float x,float y,float dx,float dy,float radius,ScEntityId ignore=0) const;
    // Borrowed until the next outline call. Ordered counterclockwise, closing point repeated.
    std::span<const ScLightPoint> outline(float x,float y,float radius,ScEntityId ignore=0,bool shadows=true);
    std::size_t size() const noexcept { return count_; }
    std::size_t required() const noexcept { return required_; }
    std::size_t capacity() const noexcept { return polygons_.size(); }
private:
    struct Point { float x{},y{}; };
    struct Polygon {
        std::array<Point,18> points{};
        ScEntityId entity{};
        ScLightBounds bounds{};
        std::size_t count{};
    };
    std::vector<Polygon> polygons_;
    std::vector<double> angles_;
    std::vector<ScLightPoint> outline_;
    std::size_t count_{},required_{};
    void append(Polygon,ScLightBounds);
    static Polygon shape(float x,float y,float w,float h,int kind,
                         const float* vertices,int count);
};
