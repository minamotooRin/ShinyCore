#include "postprocess.h"
#include "color_target.h"
#include <rlgl.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

ScPostProcess::ScPostProcess()=default;
ScPostProcess::~ScPostProcess()=default;
ScPostProcess::ScPostProcess(ScPostProcess&&) noexcept=default;
ScPostProcess& ScPostProcess::operator=(ScPostProcess&&) noexcept=default;
void ScPostProcess::clear() noexcept {
    for(auto& target:targets_) target.reset();
    width_=height_=0; failed_revision_=0; allocation_error_.clear();
}
void ScPostProcess::report(ScPostChain& post) const noexcept {
    post.target_count=0;
    for(const auto& target:targets_) if(target) ++post.target_count;
    post.allocated_bytes=std::uint64_t(width_)*std::uint64_t(height_)*4*post.target_count;
}
ScResult<void> ScPostProcess::prepare(ScPostChain& post,int width,int height) {
    if(!post.count) { clear(); report(post); return {}; }
    if(failed_revision_==post.revision) { report(post); return std::unexpected(allocation_error_); }
    try {
        const auto count=std::min(post.count,size_t(2));
        if(width<1||height<1||width>8192||height>8192) throw std::runtime_error("postprocess dimensions must be 1..8192");
        if(std::uint64_t(width)*std::uint64_t(height)*4*count>post.budget_bytes) throw std::runtime_error("postprocess target budget exceeded");
        const bool same_size=width_==width&&height_==height;
        std::array<std::unique_ptr<ScColorTarget>,2> candidates;
        for(size_t i=0;i<count;++i) if(!same_size||!targets_[i]) {
            candidates[i]=std::make_unique<ScColorTarget>(); candidates[i]->open(width,height);
        }
        for(size_t i=0;i<count;++i) if(candidates[i]) targets_[i]=std::move(candidates[i]);
        for(size_t i=count;i<targets_.size();++i) targets_[i].reset();
        width_=width; height_=height; failed_revision_=0; allocation_error_.clear(); report(post); return {};
    } catch(const std::exception& error) {
        failed_revision_=post.revision; allocation_error_=error.what(); report(post);
        return std::unexpected(allocation_error_);
    }
}
Texture2D ScPostProcess::apply(Texture2D scene,ScMaterials* materials,ScGpuMaterials& programs,Texture2D* (*texture)(const char*)) {
    if(!materials) { clear(); return scene; }
    auto& post=materials->post;
    auto fail=[&](const std::string& error) {
        if(post.error!=error) std::fprintf(stderr,"[shiny] postprocess: %s\n",error.c_str());
        post.error=error; post.presented=false; return scene;
    };
    auto prepared=prepare(post,scene.width,scene.height);
    if(!prepared) return fail(prepared.error());
    if(!post.count) return scene;
    for(size_t i=0;i<post.count;++i)
        if(!programs.ready(post.passes[i])) return fail("postprocess pass "+std::to_string(i+1)+" has no valid GPU program");
    Texture2D input=scene;
    for(size_t i=0;i<post.count;++i) {
        auto& target=targets_[i%2]->value;
        BeginTextureMode(target); ClearBackground(BLANK);
        rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD); BeginBlendMode(BLEND_CUSTOM);
        if(!programs.begin(post.passes[i],texture,scene)) {
            EndBlendMode(); EndTextureMode(); return fail("postprocess pass could not bind its resources");
        }
        DrawTextureRec(input,{0,0,static_cast<float>(input.width),-static_cast<float>(input.height)},{0,0},WHITE);
        EndBlendMode(); EndShaderMode(); EndTextureMode();
        input=target.texture;
    }
    post.presented=true; post.error.clear(); return input;
}
