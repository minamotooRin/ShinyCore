#include "color_target.h"
#include <rlgl.h>
#include <stdexcept>

ScColorTarget::~ScColorTarget() { if(value.id) UnloadRenderTexture(value); }
void ScColorTarget::open(int width,int height) {
    if(value.id||width<1||height<1||width>8192||height>8192) throw std::runtime_error("invalid color target dimensions or lifecycle");
    value.id=rlLoadFramebuffer();
    if(!value.id) throw std::runtime_error("cannot create color framebuffer");
    value.texture={rlLoadTexture(nullptr,width,height,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,1),width,height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    if(!value.texture.id) throw std::runtime_error("cannot allocate color texture");
    rlFramebufferAttach(value.id,value.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
    const bool complete=rlFramebufferComplete(value.id); rlDisableFramebuffer();
    if(!complete) throw std::runtime_error("color framebuffer is incomplete");
    SetTextureFilter(value.texture,TEXTURE_FILTER_POINT);
    SetTextureWrap(value.texture,TEXTURE_WRAP_CLAMP);
}
