#include "normals.h"
#include "shader.h"
#include "color_target.h"
#include <stdexcept>

namespace {
constexpr const char* write_source=R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform sampler2D normalMap;
uniform int mapped;
uniform vec4 basis;
out vec4 finalColor;
void main() {
    float alpha=texture(texture0,fragTexCoord).a*fragColor.a;
    vec3 n=vec3(0.0);
    if(mapped!=0) {
        n=texture(normalMap,fragTexCoord).rgb*2.0-1.0;
        float magnitude=length(n);
        n=magnitude>0.001?n/magnitude:vec3(0.0,0.0,1.0);
        n.xy=mat2(basis.xy,basis.zw)*n.xy;
    }
    finalColor=vec4(n*0.5+0.5,alpha);
})";
constexpr const char* light_source=R"(#version 330
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec3 source;
out vec4 finalColor;
void main() {
    vec3 n=texture(texture0,gl_FragCoord.xy/resolution).rgb*2.0-1.0;
    float magnitude=length(n);
    float factor=1.0;
    if(magnitude>0.02) {
        vec2 pixel=vec2(gl_FragCoord.x,resolution.y-gl_FragCoord.y);
        vec3 direction=normalize(vec3(source.xy-pixel,source.z));
        factor=mix(1.0,max(0.0,dot(n/magnitude,direction)),min(magnitude,1.0));
    }
    finalColor=vec4(fragColor.rgb*factor,1.0);
})";
}
struct ScGpuNormals::Data {
    ScColorTarget target;
    ScGpuShader write,light;
    ScNormalMaps maps;
    int mapped{},basis{},normal{},resolution{},source{};
};
ScGpuNormals::ScGpuNormals()=default;
ScGpuNormals::~ScGpuNormals()=default;
ScGpuNormals::ScGpuNormals(ScGpuNormals&&) noexcept=default;
ScGpuNormals& ScGpuNormals::operator=(ScGpuNormals&&) noexcept=default;
void ScGpuNormals::clear() noexcept { data_.reset(); drawing_=false; }
ScResult<void> ScGpuNormals::prepare(ScLighting& settings,int width,int height) {
    if(!settings.normal_maps||!settings.normal_maps->count) { clear(); settings.normal_target_bytes=0; return {}; }
    if(width<1||height<1||std::uint64_t(width)*std::uint64_t(height)*4>ScLighting::normal_budget_bytes)
        return std::unexpected("normal target exceeds 64 MiB color budget");
    try {
        auto candidate=std::make_unique<Data>();
        candidate->maps=*settings.normal_maps;
        candidate->target.open(width,height);
        candidate->write.compile(write_source); candidate->light.compile(light_source);
        candidate->mapped=candidate->write.uniform("mapped"); candidate->basis=candidate->write.uniform("basis");
        candidate->normal=candidate->write.uniform("normalMap"); candidate->resolution=candidate->light.uniform("resolution");
        candidate->source=candidate->light.uniform("source");
        data_=std::move(candidate); settings.normal_target_bytes=std::uint64_t(width)*std::uint64_t(height)*4; return {};
    } catch(const std::exception& error) { return std::unexpected(error.what()); }
}
bool ScGpuNormals::begin(float rotation) {
    rotation_=rotation;
    if(!data_) return false;
    BeginTextureMode(data_->target.value); ClearBackground({128,128,128,255});
    drawing_=true; surface(); return true;
}
void ScGpuNormals::surface(const char* image,Texture2D* (*texture)(const char*),bool flip_x,bool flip_y,bool diagonal,float angle) {
    if(!drawing_) return;
    Texture2D normal{};
    if(image) if(const char* path=data_->maps.find(image)) {
        auto* asset=texture?texture(path):nullptr;
        if(!asset) throw std::runtime_error(std::string("missing prepared normal texture: ")+path);
        normal=*asset;
    }
    rlDrawRenderBatchActive(); // Uniform changes must not alter earlier queued surfaces.
    const auto shader=data_->write.get(); BeginShaderMode(shader);
    const int mapped=normal.id?1:0;
    const auto basis=sc_normal_basis(flip_x,flip_y,diagonal,angle+rotation_);
    SetShaderValue(shader,data_->mapped,&mapped,SHADER_UNIFORM_INT);
    SetShaderValue(shader,data_->basis,basis.data(),SHADER_UNIFORM_VEC4);
    if(mapped) SetShaderValueTexture(shader,data_->normal,normal);
}
void ScGpuNormals::end() { if(drawing_) { EndShaderMode(); EndTextureMode(); drawing_=false; } }
unsigned int ScGpuNormals::light(float x,float y,float height) {
    if(!data_) return 0;
    const auto shader=data_->light.get(); BeginShaderMode(shader);
    const float size[]={static_cast<float>(data_->target.value.texture.width),static_cast<float>(data_->target.value.texture.height)};
    const float source[]={x,y,height};
    SetShaderValue(shader,data_->resolution,size,SHADER_UNIFORM_VEC2);
    SetShaderValue(shader,data_->source,source,SHADER_UNIFORM_VEC3);
    return data_->target.value.texture.id;
}
void ScGpuNormals::end_light() { if(data_) EndShaderMode(); }
