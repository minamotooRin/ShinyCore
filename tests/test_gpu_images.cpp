#include "../src/render/image_cache.h"
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
const auto owner=std::this_thread::get_id();
#ifndef SC_IMAGE_CACHE_NATIVE
bool fail_next{};
unsigned next_texture{},uploads{},unloads{};
std::map<unsigned,Texture2D> live;
#endif
ScResult<bool> prepare(ScGpuImages& gpu,std::span<const ScImageId> ids) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;) {
        const auto before=gpu.count(); auto ready=gpu.prepare(ids,1);
        check(gpu.count()<=before+1,"one upload per prepare call");
        if(!ready||*ready) return ready;
        check(std::chrono::steady_clock::now()<deadline,"GPU preparation timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
ScImageId acquire(ScImageCache& cache,const char* name) {
    auto result=cache.acquire({name,2,2}); check(bool(result),"acquire color image"); return *result;
}
}
#ifndef SC_IMAGE_CACHE_NATIVE
// Exercise failure/ownership using real declarations without opening a device.
bool IsTextureValid(Texture2D texture) { return texture.id!=0&&live.contains(texture.id); }
Texture2D LoadTextureFromImage(Image image) {
    check(std::this_thread::get_id()==owner,"GPU upload must run on owner thread");
    check(image.data&&image.width==2&&image.height==2&&image.format==PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,"borrow complete RGBA pixels");
    if(std::exchange(fail_next,false)) return {};
    Texture2D result{++next_texture,image.width,image.height,1,image.format};
    live.emplace(result.id,result); ++uploads; return result;
}
void UnloadTexture(Texture2D texture) { check(std::this_thread::get_id()==owner&&live.erase(texture.id)==1,"owner releases GPU handle exactly once"); ++unloads; }
void SetTextureFilter(Texture2D texture,int filter) { check(IsTextureValid(texture)&&filter==TEXTURE_FILTER_POINT,"configure uploaded texture"); }
#endif
int main(int argc,char** argv) {
    try {
#ifdef SC_IMAGE_CACHE_NATIVE
        check(argc==2,"native check requires an output PNG path");
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_UNFOCUSED|FLAG_WINDOW_ALWAYS_RUN);
        InitWindow(128,64,"ShinyCore image cache check");
        check(IsWindowReady()&&IsWindowHidden(),"native context must remain hidden");
        struct Window { ~Window() { CloseWindow(); } } window;
#else
        (void)argc; (void)argv;
#endif
        ScContentLoader loader({},[](const ScImageRequest& request)->ScResult<ScImagePixels> {
            check(std::this_thread::get_id()!=owner,"CPU decoding runs on worker");
            ScImagePixels pixels{2,2,std::vector<unsigned char>(16)};
            const auto channel=request.path=="red"?0u:request.path=="green"?1u:2u;
            for(std::size_t i=0;i<16;i+=4) { pixels.rgba[i+channel]=255; pixels.rgba[i+3]=255; }
            return pixels;
        });
        ScImageCache cache(loader,65536,2); ScGpuImages gpu(cache);
        const auto red=acquire(cache,"red"),green=acquire(cache,"green");
        const std::array initial{red,green}; check(bool(prepare(gpu,initial)),"initial texture batch");
        check(gpu.count()==2&&gpu.bytes()==32,"GPU usage records actual RGBA dimensions");
        const auto preserved=gpu.texture(green)->id;
#ifdef SC_IMAGE_CACHE_NATIVE
        using Target=Owned<RenderTexture2D,IsRenderTextureValid,UnloadRenderTexture>;
        Target target{LoadRenderTexture(128,64)}; check(bool(target),"capture render target");
        BeginTextureMode(target.get()); ClearBackground(BLACK);
        DrawTexturePro(*gpu.texture(red),{0,0,2,2},{0,0,64,64},{0,0},0,WHITE);
        DrawTexturePro(*gpu.texture(green),{0,0,2,2},{64,0,64,64},{0,0},0,WHITE);
        EndTextureMode();
        using Pixels=Owned<Image,IsImageValid,UnloadImage>;
        Pixels output{LoadImageFromTexture(target.get().texture)};
        check(bool(output),"read actual rendered target");
        const auto left=GetImageColor(output.get(),32,32),right=GetImageColor(output.get(),96,32);
        check(left.r==255&&left.g==0&&right.g==255&&right.r==0,"native GPU pixels match decoded colors");
        check(ExportImage(output.get(),argv[1]),"write native capture");
#endif
        check(acquire(cache,"red")==red&&bool(cache.release(red))&&gpu.texture(red),"shared texture remains with second reference");
        check(bool(cache.release(red))&&!gpu.texture(red),"unretained texture cannot be drawn");
        const auto blue=acquire(cache,"blue");
        check(bool(prepare(gpu,std::array{blue}))&&!gpu.texture(red)&&gpu.texture(green)->id==preserved,"eviction replaces only unused texture");
#ifndef SC_IMAGE_CACHE_NATIVE
        check(bool(cache.release(blue)),"release replacement");
        const auto candidate=acquire(cache,"candidate"); fail_next=true;
        auto failed=prepare(gpu,std::array{candidate});
        check(!failed&&!gpu.texture(candidate)&&gpu.texture(green)->id==preserved,"failed candidate upload preserves active texture");
        check(bool(prepare(gpu,std::array{candidate})),"GPU upload can be retried without another CPU decode");
        check(bool(cache.release(candidate)),"release previous candidate");
        auto reloaded=cache.refresh(green); check(bool(reloaded),"reserve a fresh GPU revision"); fail_next=true;
        check(!prepare(gpu,std::array{*reloaded})&&gpu.texture(green)->id==preserved,"failed reload retains old GPU texture");
        check(bool(prepare(gpu,std::array{*reloaded}))&&gpu.texture(*reloaded)->id!=preserved,"fresh revision gets a distinct texture");
        check(bool(cache.publish(std::array{*reloaded}))&&bool(cache.release(green)),"publish GPU replacement");
        gpu.collect(); check(!gpu.texture(green)&&gpu.texture(*reloaded),"retire old texture only after final reference");
#endif
        gpu.clear(); check(gpu.count()==0&&gpu.bytes()==0,"clear all GPU resources before context closes");
#ifndef SC_IMAGE_CACHE_NATIVE
        check(live.empty()&&uploads==unloads,"every successful GPU resource creation is released");
#endif
        std::cout<<"GPU images: bounded uploads, retained texture, eviction and cleanup passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
