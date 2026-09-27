#include "shader.h"
#include "external/glad.h"
#include <stdexcept>
#include <string>
#include <utility>

namespace {
constexpr const char* vertex_source=R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
void main() {
    fragTexCoord=vertexTexCoord; fragColor=vertexColor;
    gl_Position=mvp*vec4(vertexPosition,1.0);
})";
struct Stage {
    GLuint id{};
    Stage(const Stage&)=delete;
    Stage& operator=(const Stage&)=delete;
    explicit Stage(GLenum type):id(glCreateShader(type)) { if(!id) throw std::runtime_error("cannot create shader stage"); }
    ~Stage() { glDeleteShader(id); }
    void compile(const char* source) {
        glShaderSource(id,1,&source,nullptr); glCompileShader(id);
        GLint success=0; glGetShaderiv(id,GL_COMPILE_STATUS,&success);
        if(!success) {
            char log[1536]{}; glGetShaderInfoLog(id,sizeof log,nullptr,log);
            throw std::runtime_error(std::string("shader compilation: ")+log);
        }
    }
};
}
ScGpuShader::~ScGpuShader() { if(id_) glDeleteProgram(id_); }
ScGpuShader::ScGpuShader(ScGpuShader&& other) noexcept:id_(std::exchange(other.id_,0)),locations_(other.locations_) {}
ScGpuShader& ScGpuShader::operator=(ScGpuShader&& other) noexcept {
    if(this!=&other) { if(id_) glDeleteProgram(id_); id_=std::exchange(other.id_,0); locations_=other.locations_; }
    return *this;
}
int ScGpuShader::uniform(const char* name) const { return glGetUniformLocation(id_,name); }
void ScGpuShader::compile(const char* fragment_source) {
    Stage vertex(GL_VERTEX_SHADER),fragment(GL_FRAGMENT_SHADER);
    vertex.compile(vertex_source); fragment.compile(fragment_source);
    ScGpuShader candidate;
    candidate.id_=glCreateProgram(); if(!candidate.id_) throw std::runtime_error("cannot create shader program");
    glAttachShader(candidate.id_,vertex.id); glAttachShader(candidate.id_,fragment.id);
    glBindAttribLocation(candidate.id_,0,"vertexPosition"); glBindAttribLocation(candidate.id_,1,"vertexTexCoord"); glBindAttribLocation(candidate.id_,3,"vertexColor");
    glLinkProgram(candidate.id_);
    GLint success=0; glGetProgramiv(candidate.id_,GL_LINK_STATUS,&success);
    if(!success) {
        char log[1536]{}; glGetProgramInfoLog(candidate.id_,sizeof log,nullptr,log);
        throw std::runtime_error(std::string("shader link: ")+log);
    }
    candidate.locations_.fill(-1);
    candidate.locations_[SHADER_LOC_VERTEX_POSITION]=0; candidate.locations_[SHADER_LOC_VERTEX_TEXCOORD01]=1; candidate.locations_[SHADER_LOC_VERTEX_COLOR]=3;
    candidate.locations_[SHADER_LOC_MATRIX_MVP]=candidate.uniform("mvp");
    candidate.locations_[SHADER_LOC_COLOR_DIFFUSE]=candidate.uniform("colDiffuse");
    candidate.locations_[SHADER_LOC_MAP_DIFFUSE]=candidate.uniform("texture0");
    *this=std::move(candidate);
}
