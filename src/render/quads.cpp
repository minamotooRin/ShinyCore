#include "quads.h"
#include <rlgl.h>
#include <utility>
#ifdef SC_HAS_ADVANCED_RENDER
#include "materials.h"
#endif

void ScQuadBatch::finish() noexcept {
    if(!active_) return;
    rlSetTexture(0);
    if(material_) EndShaderMode();
    EndBlendMode(); active_=false;
}
void ScQuadBatch::draw(unsigned int image,Rectangle uv,Rectangle destination,Color tint,std::uint64_t material,
                       bool additive,bool flip_x,bool flip_y,bool diagonal) {
#ifdef SC_HAS_ADVANCED_RENDER
    if(material&&(!materials_||!materials_->ready(material))) return;
#else
    if(material) return;
#endif
    if(!active_||image_!=image||material_!=material||additive_!=additive) {
        finish();
        BeginBlendMode(additive?BLEND_ADDITIVE:BLEND_ALPHA);
        // Texture/mode changes may flush rlgl and clear auxiliary texture slots.
        // Establish them before installing the material's samplers.
        rlBegin(RL_QUADS); rlEnd(); rlSetTexture(image);
#ifdef SC_HAS_ADVANCED_RENDER
        if(material&&!materials_->begin(material,texture_)) { rlSetTexture(0); EndBlendMode(); return; }
#endif
        rlSetTexture(image);
        image_=image; material_=material; additive_=additive; active_=true;
    }
    const bool flushed=rlCheckRenderBatchLimit(4);
#ifdef SC_HAS_ADVANCED_RENDER
    if(flushed&&material_&&!materials_->begin(material_,texture_)) { finish(); return; }
#else
    (void)flushed;
#endif
    rlSetTexture(image); rlBegin(RL_QUADS); rlColor4ub(tint.r,tint.g,tint.b,tint.a);
    constexpr float corners[4][2]={{0,0},{0,1},{1,1},{1,0}};
    for(const auto& corner:corners) {
        float u=corner[0],v=corner[1];
        if(flip_x) u=1-u;
        if(flip_y) v=1-v;
        if(diagonal) std::swap(u,v);
        rlTexCoord2f(uv.x+u*uv.width,uv.y+v*uv.height);
        rlVertex2f(destination.x+corner[0]*destination.width,destination.y+corner[1]*destination.height);
    }
    rlEnd();
}
