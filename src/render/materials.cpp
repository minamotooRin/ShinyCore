#include "materials.h"
#include "shader.h"
#include "external/glad.h"
#include <rlgl.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
GLenum uniform_type(ScUniformType type) {
    constexpr GLenum types[]={GL_FLOAT,GL_FLOAT_VEC2,GL_FLOAT_VEC3,GL_FLOAT_VEC4,GL_INT,GL_BOOL,GL_SAMPLER_2D};
    return types[static_cast<size_t>(type)];
}
}
struct ScGpuMaterials::Program {
    Program()=default;
    Program(const Program&)=delete;
    Program& operator=(const Program&)=delete;
    ScGpuShader gpu;
    ScMaterialId material{};
    bool postprocess{};
    int scene_location{-1},resolution_location{-1};
    std::array<int,32> uniforms{};
    std::array<ScUniform,32> values{};
    size_t count{};
    void compile(const ScMaterial& source) {
        gpu.compile(source.source.c_str()); const auto id=gpu.get().id;
        uniforms.fill(-1);
        GLint active=0; glGetProgramiv(id,GL_ACTIVE_UNIFORMS,&active);
        for(GLint i=0;i<active;++i) {
            char name[256]{}; GLsizei length=0; GLint size=0; GLenum type=0;
            glGetActiveUniform(id,static_cast<GLuint>(i),sizeof name,&length,&size,&type,name);
            if(!std::strcmp(name,"mvp")) continue;
            if(!std::strcmp(name,"texture0")&&type==GL_SAMPLER_2D&&size==1) continue;
            if(!std::strcmp(name,"colDiffuse")&&type==GL_FLOAT_VEC4&&size==1) continue;
            if(source.postprocess&&size==1) {
                if(!std::strcmp(name,"sc_scene")&&type==GL_SAMPLER_2D) { scene_location=glGetUniformLocation(id,name); continue; }
                if(!std::strcmp(name,"sc_resolution")&&type==GL_FLOAT_VEC2) { resolution_location=glGetUniformLocation(id,name); continue; }
            }
            size_t at=0; while(at<source.uniform_count&&std::strcmp(source.uniforms[at].name,name)) ++at;
            if(at==source.uniform_count||size!=1||type!=uniform_type(source.uniforms[at].type))
                throw std::runtime_error(std::string("undeclared or mismatched shader uniform: ")+name);
            uniforms[at]=glGetUniformLocation(id,name);
        }
        for(size_t i=0;i<source.uniform_count;++i)
            if(uniforms[i]<0) throw std::runtime_error(std::string("uniform missing or optimized out: ")+source.uniforms[i].name);
        material=source.id; count=source.uniform_count; postprocess=source.postprocess;
    }
};
ScGpuMaterials::ScGpuMaterials()=default;
ScGpuMaterials::~ScGpuMaterials()=default;
ScGpuMaterials::ScGpuMaterials(ScGpuMaterials&&) noexcept=default;
ScGpuMaterials& ScGpuMaterials::operator=(ScGpuMaterials&&) noexcept=default;
void ScGpuMaterials::clear() noexcept {
    for(auto& program:programs_) program.reset();
    attempted_ids_.fill(0); attempted_revisions_.fill(0);
}
ScResult<void> ScGpuMaterials::sync(ScMaterials* materials,bool require_all) {
    if(!materials) { clear(); return {}; }
    size_t slot=0;
    for(auto& material:materials->entries()) {
        auto& program=programs_[slot];
        if(!material.id) { program.reset(); attempted_ids_[slot]=attempted_revisions_[slot]=0; ++slot; continue; }
        if(attempted_ids_[slot]!=material.id||attempted_revisions_[slot]!=material.revision) {
            if(program&&program->material!=material.id) program.reset();
            attempted_ids_[slot]=material.id; attempted_revisions_[slot]=material.revision;
            material.attempted_revision=material.revision;
            try {
                auto candidate=std::make_unique<Program>(); candidate->compile(material);
                program=std::move(candidate); material.compiled_revision=material.revision; material.error.clear();
            } catch(const std::exception& error) {
                material.error=material.shader+": "+error.what();
                if(require_all) return std::unexpected(material.error);
                std::fprintf(stderr,"[shiny] material: %s\n",material.error.c_str());
            }
        }
        if(program) program->values=material.uniforms;
        else if(require_all) return std::unexpected(material.error);
        ++slot;
    }
    return {};
}
bool ScGpuMaterials::ready(ScMaterialId id) const noexcept {
    const auto slot=id&65535;
    return slot<programs_.size()&&programs_[slot]&&programs_[slot]->material==id;
}
bool ScGpuMaterials::begin(ScMaterialId id,Texture2D* (*texture)(const char*),Texture2D scene) {
    if(!ready(id)) return false;
    const auto slot=id&65535;
    auto& program=*programs_[slot]; Shader shader=program.gpu.get();
    if(program.postprocess&&!scene.id) return false;
    BeginShaderMode(shader);
    for(size_t i=0;i<program.count;++i) {
        const auto& value=program.values[i];
        switch(value.type) {
        case ScUniformType::texture:
            if(auto* asset=texture(value.texture)) SetShaderValueTexture(shader,program.uniforms[i],*asset);
            else { EndShaderMode(); return false; }
            break;
        case ScUniformType::integer: case ScUniformType::boolean:
            SetShaderValue(shader,program.uniforms[i],&value.integer,SHADER_UNIFORM_INT); break;
        default: SetShaderValue(shader,program.uniforms[i],value.values.data(),static_cast<int>(value.type)); break;
        }
    }
    if(program.scene_location>=0) SetShaderValueTexture(shader,program.scene_location,scene);
    if(program.resolution_location>=0) {
        const float size[]={static_cast<float>(scene.width),static_cast<float>(scene.height)};
        SetShaderValue(shader,program.resolution_location,size,SHADER_UNIFORM_VEC2);
    }
    return true;
}
