#include "image_grid.h"
#include "shiny/render.h"
#include "shiny/script.h"
#include "shiny/project.h"
#include "shiny/projectiles.h"
#include "shiny/platform.h"
#include "shiny/text.h"
#include "raylib.h"
#include "../platform/raylib_owner.h"
#include "../audio/device.h"
#include "rlgl.h"
#include "gpu_timer.h"
#include "draw_order.h"
#include "quads.h"
#ifdef SC_HAS_STREAMING
#include "image_cache.h"
#endif
#ifdef SC_HAS_ADVANCED_RENDER
#include "materials.h"
#include "postprocess.h"
#include "occlusion.h"
#include "normals.h"
#endif
#ifdef SC_HAS_DEVTOOLS
#include "../dev/panel.h"
#endif
#include "../profile_clock.h"
#include <math.h>
#include <cmath>
#include <stdio.h>
#include <array>
#include <algorithm>
#include <utility>
#include <vector>
#include <map>
#include <exception>
#include <stdexcept>
#include <string.h>
#include <cstring>

namespace {
constexpr int TEXTURE_CAPACITY=64, LIGHT_RAYS=128, LIGHT_CAPACITY=32;
constexpr float PI_F=3.14159265358979323846f;

using TextureOwner=Owned<Texture2D,IsTextureValid,UnloadTexture>;
using TargetOwner=Owned<RenderTexture2D,IsRenderTextureValid,UnloadRenderTexture>;
using ImageOwner=Owned<Image,IsImageValid,UnloadImage>;
using FontOwner=Owned<Font,IsFontValid,UnloadFont>;
struct FontPage { int first{},size{}; FontOwner font; };
struct NativeFont { std::string name; std::vector<FontPage> pages; };
struct Asset { char path[128]{}; TextureOwner texture; };
struct Backend final {
#ifdef SC_HAS_STREAMING
    std::unique_ptr<ScGpuImages> images;
    const ScRoomImages* room_images{}; // Borrowed only during draw submission.
    std::vector<std::string> streamed_paths;
#endif
#ifdef SC_HAS_ADVANCED_RENDER
    ScGpuMaterials materials;
    const ScMaterials* material_bindings{}; // Borrowed only during one frame submission.
    ScPostProcess postprocess;
    std::unique_ptr<ScOcclusion> occlusion;
    ScGpuNormals normals;
#endif
    ScCameraView view;
    Camera2D camera{};
    bool pixel_snap{true};
    float alpha{1};
    ScGpuTimer gpu_timer;
    std::vector<ScSceneItem> scene_order;
    TargetOwner scene, light, final;
    std::array<Asset,TEXTURE_CAPACITY> assets;
    ScAudioDevice audio;
    std::vector<NativeFont> fonts;
    float font_scale{};
    std::array<bool,512> pressed_keys{};
    std::bitset<512> sampled_keys{};
    std::size_t asset_count{};
    int width{},height{};
    bool debug_keys{};
#ifdef SC_HAS_DEVTOOLS
    ScDebugPanel inspector;
#endif
    ScGamepadSelection gamepad;
    bool open{},stats{},hitboxes{},hidden{},lighting{true};
    bool smooth{};
    bool vsync{true};
    double frame_end{};
    float delta{SC_DT};
    char root[SC_PATH_MAX]{},error[SC_ERROR_MAX]{};
    Backend()=default;
    Backend(const Backend&)=delete;
    Backend& operator=(const Backend&)=delete;
    ~Backend() { close(); }
    void close() noexcept {
#ifdef SC_HAS_STREAMING
        images.reset(); room_images=nullptr; streamed_paths.clear();
#endif
#ifdef SC_HAS_ADVANCED_RENDER
        material_bindings=nullptr; materials.clear();
        postprocess.clear();
        occlusion.reset();
        normals.clear();
#endif
        gpu_timer.close();
        std::vector<ScSceneItem>{}.swap(scene_order);
        // Devices must outlive the resources released through them.
        audio.close();
        fonts.clear();
        for (auto& asset:assets) { asset.texture.reset(); asset.path[0]='\0'; }
        final.reset();light.reset();scene.reset();
        if (open) { sc_platform_close(); CloseWindow(); }
        smooth=false; vsync=true; frame_end=0; delta=SC_DT;
#ifdef SC_HAS_DEVTOOLS
        inspector={};
#endif
        open=stats=hitboxes=debug_keys=hidden=false; lighting=true; gamepad={};
#ifdef SC_HAS_DEVTOOLS
        inspector=ScDebugPanel{};
#endif
        asset_count=0; width=height=0; root[0]=error[0]='\0';pressed_keys.fill(false);sampled_keys.reset();
    }
} backend;
} // namespace

static float position(float value) { return backend.pixel_snap?std::floor(value):value; }
static bool visible(float x,float y,float w,float h) {
    const auto& v=backend.view.visible;
    return x+w>=v.x&&y+h>=v.y&&x<=v.x+v.w&&y<=v.y+v.h;
}
static Color rgba(uint32_t value) {
    return Color{(unsigned char)(value>>24),(unsigned char)(value>>16),(unsigned char)(value>>8),(unsigned char)value};
}
static Color shade(Color c,float factor) {
    c.r=(unsigned char)fminf(255,(float)c.r*factor);
    c.g=(unsigned char)fminf(255,(float)c.g*factor);
    c.b=(unsigned char)fminf(255,(float)c.b*factor);
    return c;
}
static void raylib_log(int level,const char *format,va_list args) {
    if (level>=LOG_WARNING) { fputs("[raylib] ",stderr); vfprintf(stderr,format,args); fputc('\n',stderr); }
}
bool sc_render_validate_assets(const ScWorld *world,const char *root,char *error,size_t error_size) {
    SetTraceLogCallback(raylib_log); SetTraceLogLevel(LOG_WARNING);
    try {
        std::map<std::string,std::pair<int,int>> infos;
        auto load=[&](const std::string& path) -> std::pair<int,int> {
            if(auto it=infos.find(path);it!=infos.end()) return it->second;
            // The content worker verifies streamed PNG dimensions. Never decode them again here.
            for(const auto& resource:world->resources) if(resource.streamed&&resource.path==path)
                return {resource.image_width,resource.image_height};
            if(infos.size()==TEXTURE_CAPACITY) throw std::runtime_error("texture capacity exceeded (64)");
            ImageOwner image{LoadImage((std::string(root)+"/"+path).c_str())};
            if(!image||image.get().width>8192||image.get().height>8192) throw std::runtime_error("cannot decode bounded texture: "+path);
            return infos.emplace(path,std::pair{image.get().width,image.get().height}).first->second;
        };
        for(const auto& e:world->entities) {
            if(!e.alive||!e.sprite[0]) continue;
            auto [width,height]=load(e.sprite);
            int fw=e.frame_w?e.frame_w:width,fh=e.frame_h?e.frame_h:height;
            if(fw<1||fh<1||width%fw||height%fh||e.frame<0||e.frame>=(width/fw)*(height/fh))
                throw std::runtime_error("invalid sprite frame or grid: "+std::string(e.sprite));
        }
        for(const auto& g:world->tile_graphics) {
            auto [width,height]=load(g.image);
            if(g.x<0||g.y<0||g.x+g.w>width||g.y+g.h>height) throw std::runtime_error("tileset grid exceeds image: "+g.image);
        }
        for(const auto& r:world->resources) {
            if(r.type=="image"&&!r.streamed) load(r.path);
        }
        auto audio=sc_audio_validate(world->resources,root);
        if(!audio) throw std::runtime_error(audio.error());
        return true;
    } catch(const std::exception& e) { snprintf(error,error_size,"%s",e.what()); return false; }
}
static Texture2D *texture(const char *relative) {
#ifdef SC_HAS_STREAMING
    for(const auto& path:backend.streamed_paths) if(path==relative) {
        char full[SC_PATH_MAX*2]; snprintf(full,sizeof full,"%s/%s",backend.root,relative);
        const auto id=backend.room_images?backend.room_images->path(full):0;
        if(id&&backend.images) if(auto* image=backend.images->texture(id)) return image;
        snprintf(backend.error,sizeof backend.error,"streamed image is not committed: %s",relative); return nullptr;
    }
#endif
    for (std::size_t i=0;i<backend.asset_count;i++) if (!strcmp(backend.assets[i].path,relative)) return backend.assets[i].texture.ptr();
    if (backend.asset_count==TEXTURE_CAPACITY) {
        snprintf(backend.error,sizeof(backend.error),"texture capacity exceeded (%d)",TEXTURE_CAPACITY); return NULL;
    }
    if (!sc_script_validate_path(relative)) {
        snprintf(backend.error,sizeof(backend.error),"invalid texture path: %s",relative); return NULL;
    }
    char path[SC_PATH_MAX*2]; snprintf(path,sizeof(path),"%s/%s",backend.root,relative);
    ImageOwner img{LoadImage(path)};
    if (!img) { snprintf(backend.error,sizeof(backend.error),"cannot decode texture: %s",path); return NULL; }
    if (img.get().width>8192 || img.get().height>8192) {
        snprintf(backend.error,sizeof(backend.error),"texture dimensions exceed 8192: %s",relative); return NULL;
    }
    Asset *asset=&backend.assets[backend.asset_count];
    asset->texture.reset(LoadTextureFromImage(img.get()));
    if (!asset->texture) { snprintf(backend.error,sizeof(backend.error),"cannot upload texture: %s",relative); return NULL; }
    snprintf(asset->path,sizeof(asset->path),"%s",relative);
    SetTextureFilter(asset->texture.get(),TEXTURE_FILTER_POINT);
    backend.asset_count++; return asset->texture.ptr();
}
void sc_render_reload_assets(void) {
    for (auto& asset:backend.assets) { asset.texture.reset(); asset.path[0]='\0'; }
    backend.asset_count=0; backend.error[0]='\0';
    backend.fonts.clear();
    backend.audio.reset_voices();
}
const char *sc_render_error(void) { return backend.error; }
bool sc_render_open(const ScWorld *world,const char *root,bool audio,char *error,size_t error_size,bool hidden) {
    backend.close();
    backend.width=world->view_width; backend.height=world->view_height;
    backend.lighting=true;
    backend.hidden=hidden;
    snprintf(backend.root,sizeof(backend.root),"%s",root);
    SetTraceLogCallback(raylib_log); SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_VSYNC_HINT|
        (hidden?(FLAG_WINDOW_HIDDEN|FLAG_WINDOW_UNFOCUSED|FLAG_WINDOW_ALWAYS_RUN):0));
    InitWindow(backend.width*3,backend.height*3,world->title);
    if (!IsWindowReady()) { snprintf(error,error_size,"unable to create native graphics window"); return false; }
    backend.open=true; sc_platform_open(GetWindowHandle()); SetExitKey(KEY_NULL);
    SetWindowMinSize(320,180); backend.frame_end=GetTime();
    backend.scene.reset(LoadRenderTexture(backend.width,backend.height));
    backend.light.reset(LoadRenderTexture(backend.width,backend.height));
    backend.final.reset(LoadRenderTexture(backend.width,backend.height));
    if (!static_cast<bool>(backend.scene)||!static_cast<bool>(backend.light)||!static_cast<bool>(backend.final)) {
        snprintf(error,error_size,"unable to create render targets"); sc_render_close(); return false;
    }
    SetTextureFilter(backend.final.get().texture,TEXTURE_FILTER_POINT);
    SetTextureWrap(backend.final.get().texture,TEXTURE_WRAP_CLAMP);
    if (audio) {
        if (!backend.audio.open()) fputs("[shiny] audio device unavailable; continuing silently\n",stderr);
    }
    return true;
}
void sc_render_loading() {
    BeginDrawing(); ClearBackground(Color{18,28,42,255});
    DrawText("Loading room...",24,24,20,RAYWHITE);
    EndDrawing(); SwapScreenBuffer(); PollInputEvents();
}
ScResult<void> sc_render_settings(const ScSettings& settings) {
    if(!backend.open) return std::unexpected("window is not open");
    if(backend.hidden && settings.borderless) return std::unexpected("hidden capture requires windowed settings");
    if(IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)!=settings.borderless) ToggleBorderlessWindowed();
    if(!settings.borderless) {
        if(GetScreenWidth()!=settings.width||GetScreenHeight()!=settings.height)
            SetWindowSize(settings.width,settings.height);
        if(GetScreenWidth()!=settings.width||GetScreenHeight()!=settings.height)
            return std::unexpected("window manager rejected the requested resolution");
    }
    if(IsWindowState(FLAG_VSYNC_HINT)!=settings.vsync) {
        if(settings.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
    }
    if(IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)!=settings.borderless)
        return std::unexpected("window manager rejected the requested mode");
    backend.smooth=settings.smooth;
    SetTextureFilter(backend.final.get().texture,settings.smooth?TEXTURE_FILTER_BILINEAR:TEXTURE_FILTER_POINT);
    backend.vsync=settings.vsync;
    return {};
}
static float viewport_scale() {
    float fit=std::min(static_cast<float>(std::max(1,GetScreenWidth()))/static_cast<float>(backend.width),
        static_cast<float>(std::max(1,GetScreenHeight()))/static_cast<float>(backend.height));
    return !backend.smooth&&fit>=1?std::floor(fit):fit;
}
void sc_render_close(void) { backend.close(); }

bool sc_render_should_close(void) {
    backend.pressed_keys.fill(false);
    for (int key=GetKeyPressed();key;key=GetKeyPressed()) {
        if (key>0 && key<512) backend.pressed_keys[static_cast<std::size_t>(key)]=true;
    }
    return WindowShouldClose();
}
float sc_render_delta(void) { return backend.delta; }
static bool key_pressed(int key) { return backend.debug_keys && IsWindowFocused() && (backend.pressed_keys[static_cast<std::size_t>(key)] || IsKeyPressed(key)); }
bool sc_render_reload_requested(void) { return key_pressed(KEY_F5); }
bool sc_render_pause_requested(void) { return key_pressed(KEY_P); }
bool sc_render_step_requested(void) { return key_pressed(KEY_O); }
void sc_render_debug_keys(bool enabled) {
    backend.debug_keys=enabled; SetExitKey(enabled?KEY_ESCAPE:KEY_NULL);
}
void sc_render_input_consumed() { backend.gamepad.consumed(); }
ScDeviceInput sc_render_sample_input() {
    ScDeviceInput input;
    std::array<bool,4> available{};
    for(std::size_t i=0;i<available.size();++i) available[i]=IsGamepadAvailable(static_cast<int>(i));
    int pad=backend.gamepad.sample(available);
    if(!IsWindowFocused()) { backend.sampled_keys.reset(); return input; }
    for(const auto& key:SC_KEYS) {
        auto id=static_cast<std::size_t>(key.id);
        input.keys[id]=IsKeyDown(key.id);
        input.key_pressed[id]=backend.pressed_keys[id]||IsKeyPressed(key.id);
        // GLFW queues fresh presses, not auto-repeat. A queued press on a key
        // held at the last sample proves an intervening release, even when the
        // frame's final held state hides it from IsKeyReleased().
        input.key_released[id]=IsKeyReleased(key.id)||(backend.pressed_keys[id]&&backend.sampled_keys[id]);
    }
    backend.sampled_keys=input.keys;
    auto position=GetMousePosition(),delta=GetMouseDelta(),wheel=GetMouseWheelMoveV();
    float scaling=viewport_scale();
    float offset_x=(static_cast<float>(GetScreenWidth())-static_cast<float>(backend.width)*scaling)*.5f;
    float offset_y=(static_cast<float>(GetScreenHeight())-static_cast<float>(backend.height)*scaling)*.5f;
    input.mouse_x=(position.x-offset_x)/scaling; input.mouse_y=(position.y-offset_y)/scaling;
    input.mouse_dx=delta.x/scaling; input.mouse_dy=delta.y/scaling;
    input.wheel_x=wheel.x; input.wheel_y=wheel.y;
    input.mouse_inside=input.mouse_x>=0&&input.mouse_y>=0&&input.mouse_x<static_cast<float>(backend.width)&&input.mouse_y<static_cast<float>(backend.height);
    for(int i=0;i<5;++i) { if(IsMouseButtonDown(i)) input.mouse_buttons|=1u<<i; if(IsMouseButtonPressed(i)) input.mouse_pressed|=1u<<i; if(IsMouseButtonReleased(i)) input.mouse_released|=1u<<i; }
    sc_platform_input(input);
    if((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL))&&IsKeyPressed(KEY_V)) {
        const char* clipboard=GetClipboardText();
        if(clipboard) { if(std::strlen(clipboard)>=input.clipboard.size()) throw std::runtime_error("clipboard text exceeds 4095 bytes"); std::snprintf(input.clipboard.data(),input.clipboard.size(),"%s",clipboard); }
    }
    size_t text_length=0;
    for(int code=GetCharPressed();code;code=GetCharPressed()) {
        int size=0; const char* bytes=CodepointToUTF8(code,&size);
        if(size>0&&text_length+static_cast<size_t>(size)<input.text.size()) { std::memcpy(input.text.data()+text_length,bytes,static_cast<size_t>(size)); text_length+=static_cast<size_t>(size); }
        else throw std::runtime_error("text input queue capacity exhausted");
    }
    if(pad<0) return input;
    input.connected=true;
    constexpr int buttons[]={
        GAMEPAD_BUTTON_LEFT_FACE_UP,GAMEPAD_BUTTON_LEFT_FACE_RIGHT,GAMEPAD_BUTTON_LEFT_FACE_DOWN,GAMEPAD_BUTTON_LEFT_FACE_LEFT,
        GAMEPAD_BUTTON_RIGHT_FACE_UP,GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,GAMEPAD_BUTTON_RIGHT_FACE_DOWN,GAMEPAD_BUTTON_RIGHT_FACE_LEFT,
        GAMEPAD_BUTTON_LEFT_TRIGGER_1,GAMEPAD_BUTTON_LEFT_TRIGGER_2,GAMEPAD_BUTTON_RIGHT_TRIGGER_1,GAMEPAD_BUTTON_RIGHT_TRIGGER_2,
        GAMEPAD_BUTTON_MIDDLE_LEFT,GAMEPAD_BUTTON_MIDDLE,GAMEPAD_BUTTON_MIDDLE_RIGHT,GAMEPAD_BUTTON_LEFT_THUMB,GAMEPAD_BUTTON_RIGHT_THUMB
    };
    for(std::size_t i=0;i<std::size(buttons);++i) {
        if(IsGamepadButtonDown(pad,buttons[i])) input.buttons|=1u<<i;
        if(IsGamepadButtonPressed(pad,buttons[i])) input.button_pressed|=1u<<i;
        if(IsGamepadButtonReleased(pad,buttons[i])) input.button_released|=1u<<i;
    }
    constexpr int axes[]={GAMEPAD_AXIS_LEFT_X,GAMEPAD_AXIS_LEFT_Y,GAMEPAD_AXIS_RIGHT_X,GAMEPAD_AXIS_RIGHT_Y,GAMEPAD_AXIS_LEFT_TRIGGER,GAMEPAD_AXIS_RIGHT_TRIGGER};
    int count=GetGamepadAxisCount(pad);
    for(std::size_t i=0;i<std::size(axes);++i) if(axes[i]<count) {
        input.axes[i]=sc_normalize_gamepad_axis(GetGamepadAxisMovement(pad,axes[i]),i>=4);
    }
    for(int slot=0;slot<4;++slot) if(available[static_cast<size_t>(slot)]) {
        auto& device=input.pads[static_cast<size_t>(slot)]; device.connected=true;
        for(size_t i=0;i<std::size(buttons);++i) {
            if(IsGamepadButtonDown(slot,buttons[i])) device.buttons|=1u<<i;
            if(IsGamepadButtonPressed(slot,buttons[i])) device.pressed|=1u<<i;
            if(IsGamepadButtonReleased(slot,buttons[i])) device.released|=1u<<i;
        }
        for(size_t i=0;i<std::size(axes);++i) if(axes[i]<GetGamepadAxisCount(slot)) device.axes[i]=sc_normalize_gamepad_axis(GetGamepadAxisMovement(slot,axes[i]),i>=4);
    }
    return input;
}
static float font_display_scale() {
    const float dpi=static_cast<float>(GetRenderWidth())/static_cast<float>(std::max(1,GetScreenWidth()));
    return viewport_scale()*dpi;
}
static int font_raster_size(const ScResource& resource,const ScDraw& draw) {
    if(!draw.screen) return resource.size;
    return std::clamp(static_cast<int>(std::ceil(draw.h*font_display_scale())),1,512);
}
static FontPage font_page(const ScResource& resource,int first,int size) {
    std::array<int,64> codes{};
    for(int i=0;i<64;++i) codes[static_cast<size_t>(i)]=first+i;
    FontPage page; page.first=first; page.size=size;
    page.font.reset(LoadFontFromMemory(".ttf",resource.font_bytes.data(),static_cast<int>(resource.font_bytes.size()),
        size,codes.data(),static_cast<int>(codes.size())));
    if(!page.font) throw std::runtime_error("cannot upload font page: "+resource.name);
    SetTextureFilter(page.font.get().texture,TEXTURE_FILTER_POINT);
    return page;
}
static Font font_asset(const ScResource* resource,int codepoint,int size) {
    if(!resource) return GetFontDefault();
    NativeFont* cached=nullptr;
    for(auto& font:backend.fonts) if(font.name==resource->name) { cached=&font; break; }
    if(!cached) { backend.fonts.push_back({resource->name,{}}); cached=&backend.fonts.back(); }
    const int first=codepoint/64*64;
    for(auto& page:cached->pages) if(page.first==first&&page.size==size) return page.font.get();
    if(cached->pages.size()>=128) throw std::runtime_error("font atlas budget exhausted (128 pages): "+resource->name);
    cached->pages.push_back(font_page(*resource,first,size));
    return cached->pages.back().font.get();
}
#ifdef SC_HAS_ADVANCED_RENDER
static std::expected<void,const char*> collect_occlusion(ScOcclusion& occlusion,const ScWorld& world,const ScLighting& settings,ScLightFrame& frame,float alpha=1) {
    const auto view=sc_display_camera(world,alpha).visible;
    if(auto collected=sc_collect_lights(world,settings,frame,alpha);!collected) { occlusion.clear(); return collected; }
    if(!frame.shadow_count) { occlusion.clear(); return {}; }
    return occlusion.collect(world,{view.x-1056,view.y-1056,view.w+2112,view.h+2112},settings.occluder_modes,alpha);
}
#endif
bool sc_render_prepare_assets(const ScWorld* world,char* error,size_t error_size,ScScript* script) {
    try {
        const auto capacity=world->entities.size()+world->draws.size()+world->layers.size();
        if(backend.scene_order.size()<capacity) backend.scene_order.resize(capacity);
        std::array<Asset,TEXTURE_CAPACITY> assets;
        std::vector<NativeFont> fonts;
#ifdef SC_HAS_STREAMING
        std::vector<std::string> streamed_paths;
        for(const auto& resource:world->resources) if(resource.streamed) streamed_paths.push_back(resource.path);
#endif
        size_t count=0;
        auto prepare=[&](const char* path) {
            if(!*path) return true;
#ifdef SC_HAS_STREAMING
            for(const auto& streamed:streamed_paths) if(streamed==path) {
                const auto full=std::string(backend.root)+"/"+path;
                const auto id=script&&script->images?script->images->path(full):0;
                if(id&&backend.images&&backend.images->texture(id)) return true;
                snprintf(error,error_size,"initial scene uses an uncommitted streamed image: %s",path); return false;
            }
#endif
            for(size_t i=0;i<count;++i) if(!strcmp(assets[i].path,path)) return true;
            if(count==assets.size()) { snprintf(error,error_size,"texture capacity exceeded (64)"); return false; }
            std::string full=std::string(backend.root)+"/"+path;
            ImageOwner image{LoadImage(full.c_str())};
            if(!image||image.get().width>8192||image.get().height>8192) { snprintf(error,error_size,"invalid texture: %s",path); return false; }
            auto& asset=assets[count]; asset.texture.reset(LoadTextureFromImage(image.get()));
            if(!asset.texture) { snprintf(error,error_size,"cannot upload texture: %s",path); return false; }
            snprintf(asset.path,sizeof asset.path,"%s",path); SetTextureFilter(asset.texture.get(),TEXTURE_FILTER_POINT); ++count; return true;
        };
        for(const auto& entity:world->entities) if(entity.alive&&!prepare(entity.sprite)) return false;
        for(const auto& graphic:world->tile_graphics) if(!prepare(graphic.image.c_str())) return false;
        for(const auto& resource:world->resources) {
            if(resource.type=="image"&&!resource.streamed&&!prepare(resource.path.c_str())) return false;
            if(resource.type!="font") continue;
            NativeFont item; item.name=resource.name;
            fonts.push_back(std::move(item));
        }
        // Candidate first-draw glyph uploads must succeed before replacing active assets.
        for(int i=0;i<world->draw_count;++i) {
            const auto& draw=world->draws[static_cast<size_t>(i)];
            if(draw.kind==SC_DRAW_IMAGE&&!prepare(draw.text)) return false;
            if(draw.kind!=SC_DRAW_TEXT) continue;
            auto layout=sc_text_layout(world,draw.text,draw.h,draw.font,draw.wrap,draw.align);
            for(const auto& letter:layout.letters) if(letter.font) {
                for(auto& font:fonts) if(font.name==letter.font->name) {
                    const int first=letter.codepoint/64*64;
                    const int size=font_raster_size(*letter.font,draw);
                    bool exists=false;
                    for(const auto& page:font.pages) if(page.first==first&&page.size==size) exists=true;
                    if(!exists) {
                        if(font.pages.size()>=128) throw std::runtime_error("font atlas budget exhausted (128 pages)");
                        font.pages.push_back(font_page(*letter.font,first,size));
                    }
                }
            }
        }
#ifdef SC_HAS_ADVANCED_RENDER
        ScGpuMaterials materials;
        auto compiled=materials.sync(script?script->materials.get():nullptr,true);
        if(!compiled) { snprintf(error,error_size,"%s",compiled.error().c_str()); return false; }
        ScPostProcess postprocess;
        ScGpuNormals normals;
        auto occlusion=std::make_unique<ScOcclusion>();
        const ScLighting default_lighting; ScLightFrame light_frame;
        auto geometry=collect_occlusion(*occlusion,*world,script?script->lighting:default_lighting,light_frame);
        if(!geometry) {
            if(occlusion->required()>occlusion->capacity())
                snprintf(error,error_size,"%s (required=%zu, capacity=%zu)",geometry.error(),occlusion->required(),occlusion->capacity());
            else snprintf(error,error_size,"%s",geometry.error());
            return false;
        }
        if(script&&script->materials) {
            auto prepared=postprocess.prepare(script->materials->post,world->view_width,world->view_height);
            if(!prepared) { snprintf(error,error_size,"%s",prepared.error().c_str()); return false; }
        }
        if(script) {
            auto prepared=normals.prepare(script->lighting,world->view_width,world->view_height);
            if(!prepared) { snprintf(error,error_size,"normal resources: %s",prepared.error().c_str()); return false; }
        }
#endif
        auto audio=backend.audio.prepare(script?script->audio:nullptr,backend.root);
        if(!audio) { snprintf(error,error_size,"audio: %s",audio.error().c_str()); return false; }
#ifdef SC_HAS_ADVANCED_RENDER
        backend.materials=std::move(materials);
        backend.postprocess=std::move(postprocess);
        backend.occlusion=std::move(occlusion);
        backend.normals=std::move(normals);
#endif
        backend.assets=std::move(assets); backend.asset_count=count; backend.fonts=std::move(fonts);
#ifdef SC_HAS_STREAMING
        backend.streamed_paths=std::move(streamed_paths);
#endif
        backend.font_scale=font_display_scale();
        backend.audio.commit(std::move(*audio));
        backend.error[0]=0;
        return true;
    } catch(const std::exception& e) { snprintf(error,error_size,"resource preparation: %s",e.what()); return false; }
}
#ifdef SC_HAS_STREAMING
ScResult<bool> sc_render_prepare_images(ScRoomImages& images) {
    if(!backend.images) backend.images=std::make_unique<ScGpuImages>(images.cache());
    return images.advance([](std::span<const ScImageId> ids) { return backend.images->prepare(ids,1); });
}
#endif
static void text_draw(const ScWorld* world,const ScDraw& draw,float x,float y) {
    auto layout=sc_text_layout(world,draw.text,draw.h,draw.font,draw.wrap,draw.align);
    for(const auto& letter:layout.letters)
        DrawTextCodepoint(font_asset(letter.font,letter.codepoint,letter.font?font_raster_size(*letter.font,draw):0),
                          letter.codepoint,{x+letter.x,y+letter.y},draw.h,rgba(draw.color));
}
static ScGpuMaterials* material_programs() {
#ifdef SC_HAS_ADVANCED_RENDER
    return &backend.materials;
#else
    return nullptr;
#endif
}
static std::uint64_t image_material(const char* path) {
#ifdef SC_HAS_ADVANCED_RENDER
    return backend.material_bindings?backend.material_bindings->image_material(path):0;
#else
    (void)path; return 0;
#endif
}
static void draw_image(const ScDraw* d,float x,float y) {
#ifdef SC_HAS_ADVANCED_RENDER
    const auto material=d->material?d->material:d->default_material?image_material(d->text):0;
    if(backend.normals.drawing()) backend.normals.surface(d->text,texture,d->flip_x,d->flip_y,d->diagonal,d->screen?-backend.view.rotation:0);
    else if(material&&!backend.materials.begin(material,texture)) return;
#endif
    if(auto* asset=texture(d->text)) {
        const float sw=d->source_w?d->source_w:static_cast<float>(asset->width);
        const float sh=d->source_h?d->source_h:static_cast<float>(asset->height);
        const auto tint=rgba(d->color);
        rlSetTexture(asset->id); rlBegin(RL_QUADS); rlColor4ub(tint.r,tint.g,tint.b,tint.a);
        const auto grid=sc_image_grid(*d,sw,sh);
        constexpr std::size_t corners[4][2]={{0,0},{0,1},{1,1},{1,0}};
        for(std::size_t row=0;row<static_cast<std::size_t>(grid.y.cells);++row)
        for(std::size_t column=0;column<static_cast<std::size_t>(grid.x.cells);++column) {
            if(grid.x.position[column+1]<=grid.x.position[column]||grid.y.position[row+1]<=grid.y.position[row]) continue;
            for(const auto& corner:corners) {
                const auto cx=column+corner[0],cy=row+corner[1];
                float u=grid.x.uv[cx],v=grid.y.uv[cy];
                if(d->flip_x) u=1-u;
                if(d->flip_y) v=1-v;
                if(d->diagonal) std::swap(u,v);
                rlTexCoord2f((d->source_x+u*sw)/static_cast<float>(asset->width),
                             (d->source_y+v*sh)/static_cast<float>(asset->height));
                rlVertex2f(x+grid.x.position[cx],y+grid.y.position[cy]);
            }
        }
        rlEnd(); rlSetTexture(0);
    }
#ifdef SC_HAS_ADVANCED_RENDER
    if(backend.normals.drawing()) backend.normals.surface();
    else if(material) EndShaderMode();
#endif
}
static void draw_commands(const ScWorld *world,bool screen,float scale=1,Vector2 origin={}) {
    std::array<Rectangle,32> clips{}; size_t depth=0;
    const Rectangle viewport{0,0,static_cast<float>(backend.width),static_cast<float>(backend.height)};
    auto clip=[&](Rectangle area) {
        const float left=std::floor(origin.x+area.x*scale),top=std::floor(origin.y+area.y*scale);
        const float right=std::ceil(origin.x+(area.x+area.width)*scale),bottom=std::ceil(origin.y+(area.y+area.height)*scale);
        BeginScissorMode(static_cast<int>(left),static_cast<int>(top),
                        area.width>0?static_cast<int>(right-left):0,area.height>0?static_cast<int>(bottom-top):0);
    };
    if(screen) clip(viewport);
    for (std::size_t i=0;i<static_cast<std::size_t>(world->draw_count);i++) {
        const ScDraw *d=&world->draws[i]; if (d->screen!=screen||d->layered) continue;
        float x=screen?std::floor(d->x):position(d->x),y=screen?std::floor(d->y):position(d->y);
        if (d->kind==SC_DRAW_RECT) DrawRectangleRec(Rectangle{x,y,d->w,d->h},rgba(d->color));
        else if (d->kind==SC_DRAW_CIRCLE) DrawCircleV(Vector2{x,y},d->w,rgba(d->color));
        else if (d->kind==SC_DRAW_TEXT) text_draw(world,*d,x,y);
        else if(d->kind==SC_DRAW_IMAGE) {
            draw_image(d,x,y);
        } else if(d->kind==SC_DRAW_CLIP) {
            if(depth==clips.size()) throw std::runtime_error("clip stack capacity exhausted");
            Rectangle area{x,y,d->w,d->h};
            if(depth||screen) {
                const auto& old=depth?clips[depth-1]:viewport; float right=std::min(area.x+area.width,old.x+old.width),bottom=std::min(area.y+area.height,old.y+old.height);
                area.x=std::max(area.x,old.x); area.y=std::max(area.y,old.y); area.width=std::max(0.0f,right-area.x); area.height=std::max(0.0f,bottom-area.y);
            }
            clips[depth++]=area;
            clip(area);
        } else if(d->kind==SC_DRAW_UNCLIP) {
            if(!depth) throw std::runtime_error("clip stack underflow");
            --depth; EndScissorMode();
            if(depth) clip(clips[depth-1]);
            else if(screen) clip(viewport);
        }
    }
    if(depth) { EndScissorMode(); throw std::runtime_error("unbalanced clip stack"); }
    if(screen) EndScissorMode();
}
static void draw_map(const ScWorld *world) {
    int tile=world->map.tile_size;
    const auto& v=backend.view.visible;
    int left=std::max(0,static_cast<int>(std::floor(v.x/static_cast<float>(tile))));
    int top=std::max(0,static_cast<int>(std::floor(v.y/static_cast<float>(tile))));
    int right=static_cast<int>(std::ceil((v.x+v.w)/static_cast<float>(tile)))+1;
    int bottom=static_cast<int>(std::ceil((v.y+v.h)/static_cast<float>(tile)))+1;
    Color base=rgba(world->map.color),accent=rgba(world->map.accent);
    for (int y=top;y<bottom && y<world->map.height;y++) for (int x=left;x<right && x<world->map.width;x++) {
        if (x<0 || y<0) continue;
        char t=sc_tile(world,x,y); if (t=='.') continue;
        int sx=x*tile,sy=y*tile;
        uint32_t noise=(uint32_t)x*UINT32_C(374761393)+(uint32_t)y*UINT32_C(668265263);
        noise=(noise^(noise>>13))*UINT32_C(1274126177);
        if (t=='=') {
            DrawRectangle(sx,sy,tile,2,accent);
            DrawRectangle(sx+1,sy+2,tile-2,1,shade(base,1.4f));
            if (noise%3==0) DrawRectangle(sx+tile/2,sy+3,1,3,shade(accent,.65f));
            continue;
        }
        DrawRectangle(sx,sy,tile,tile,shade(base,.86f+(float)(noise%30)/100));
        if (sc_tile(world,x,y-1)=='.' || sc_tile(world,x,y-1)=='=') {
            DrawRectangle(sx,sy,tile,1,accent);
            DrawRectangle(sx+(int)(noise%3),sy+1,tile/2,1,shade(accent,.68f));
            if (noise%5==0) DrawPixel(sx+2,sy-1,shade(accent,1.3f));
        }
        if (sc_tile(world,x,y+1)=='.') DrawRectangle(sx,sy+tile-1,tile,1,shade(accent,.65f));
        if (noise%5==0 && tile>=4) DrawRectangle(sx+2,sy+tile/2,2,1,shade(accent,.48f));
        if (sc_tile(world,x-1,y)=='.') DrawRectangle(sx,sy,1,tile,shade(accent,.65f));
    }
}
static void draw_layer(const ScWorld* world,const ScLayer& layer) {
    if(!layer.visible) return;
    float tile=static_cast<float>(world->map.tile_size);
    ScQuadBatch batch(material_programs(),texture);
    for(size_t i=0;i<layer.cells.size();++i) {
        uint32_t gid=layer.cells[i]; if(!gid) continue;
        auto* graphic=sc_tile_graphic(world,gid); if(!graphic) continue;
        float x=static_cast<float>(i%static_cast<size_t>(world->map.width))*tile+layer.x;
        float y=static_cast<float>(i/static_cast<size_t>(world->map.width))*tile+layer.y;
        if(!visible(x,y,tile,tile)) continue;
        auto* asset=texture(graphic->image.c_str()); if(!asset) continue;
#ifdef SC_HAS_ADVANCED_RENDER
        backend.normals.surface(graphic->image.c_str(),texture,(gid&0x80000000u)!=0,(gid&0x40000000u)!=0,(gid&0x20000000u)!=0);
#endif
        bool normal_pass=false;
#ifdef SC_HAS_ADVANCED_RENDER
        normal_pass=backend.normals.drawing();
#endif
        if(!normal_pass) {
            batch.draw(asset->id,{static_cast<float>(graphic->x)/static_cast<float>(asset->width),static_cast<float>(graphic->y)/static_cast<float>(asset->height),
                static_cast<float>(graphic->w)/static_cast<float>(asset->width),static_cast<float>(graphic->h)/static_cast<float>(asset->height)},
                {position(x),position(y),tile,tile},{255,255,255,static_cast<unsigned char>(layer.opacity*255)},
                image_material(graphic->image.c_str()),false,(gid&0x80000000u)!=0,(gid&0x40000000u)!=0,(gid&0x20000000u)!=0);
            continue;
        }
        rlSetTexture(asset->id); rlBegin(RL_QUADS); rlColor4ub(255,255,255,static_cast<unsigned char>(layer.opacity*255));
        const float corners[4][2]={{0,0},{0,1},{1,1},{1,0}};
        for(const auto& corner:corners) {
            float u=corner[0],v=corner[1];
            if(gid&0x80000000u) u=1-u;
            if(gid&0x40000000u) v=1-v;
            if(gid&0x20000000u) std::swap(u,v);
            rlTexCoord2f((static_cast<float>(graphic->x)+u*static_cast<float>(graphic->w))/static_cast<float>(asset->width),
                        (static_cast<float>(graphic->y)+v*static_cast<float>(graphic->h))/static_cast<float>(asset->height));
            rlVertex2f(position(x)+corner[0]*tile,position(y)+corner[1]*tile);
        }
        rlEnd(); rlSetTexture(0);
    }
#ifdef SC_HAS_ADVANCED_RENDER
    backend.normals.surface();
#endif
}
class EntityMaterialScope final {
    bool active_{};
public:
    bool begin(const ScEntity& entity) {
#ifdef SC_HAS_ADVANCED_RENDER
        if(backend.normals.drawing()) return true;
        const auto id=backend.material_bindings?backend.material_bindings->entity_material(entity):0;
        if(id) { active_=backend.materials.begin(id,texture); return active_; }
#else
        (void)entity;
#endif
        return true;
    }
    ~EntityMaterialScope() { if(active_) EndShaderMode(); }
};
static void draw_entities(const ScWorld *world) {
    const auto count=sc_scene_order(*world,backend.scene_order);
    if(!count) throw std::runtime_error(count.error());
    for(std::size_t i=0;i<*count;++i) {
        const auto& item=backend.scene_order[i];
        if(item.kind==ScSceneKind::map) { draw_layer(world,world->layers[item.index]); continue; }
        if(item.kind==ScSceneKind::image) {
            const auto& draw=world->draws[item.index];
            if(draw.screen) EndMode2D();
            draw_image(&draw,draw.screen?std::floor(draw.x):position(draw.x),draw.screen?std::floor(draw.y):position(draw.y));
            if(draw.screen) BeginMode2D(backend.camera);
            continue;
        }
        const ScEntity* e=&world->entities[item.index];
        const auto pose=sc_display_pose(*world,*e,backend.alpha);
        float x=position(pose.x),y=position(pose.y);
        const float radius=std::hypot(e->w,e->h)*.5f;
        if(e->shape!=3&&!visible(x+e->w*.5f-radius,y+e->h*.5f-radius,2*radius,2*radius)) continue;
        EntityMaterialScope material; if(!material.begin(*e)) continue;
        if (e->sprite[0]) {
            Texture2D *asset=texture(e->sprite); if (!asset) continue;
            int fw=e->frame_w?e->frame_w:asset->width,fh=e->frame_h?e->frame_h:asset->height;
            int columns=asset->width/fw,rows=asset->height/fh;
            if (columns<1 || rows<1 || e->frame<0 || e->frame>=columns*rows || asset->width%fw || asset->height%fh) {
                snprintf(backend.error,sizeof(backend.error),"invalid sprite frame %d or grid %dx%d for %s (%dx%d)",e->frame,fw,fh,e->sprite,asset->width,asset->height);
                continue;
            }
            Rectangle source={(float)((e->frame%columns)*fw),(float)((e->frame/columns)*fh),(float)fw,(float)fh};
            if(e->flip_x) source.width=-source.width;
            if(e->flip_y) source.height=-source.height;
#ifdef SC_HAS_ADVANCED_RENDER
            backend.normals.surface(e->sprite,texture,e->flip_x,e->flip_y,false,pose.angle);
#endif
            DrawTexturePro(*asset,source,Rectangle{x+e->w/2,y+e->h/2,e->w,e->h},Vector2{e->w/2,e->h/2},pose.angle*180/PI_F,rgba(e->color));
#ifdef SC_HAS_ADVANCED_RENDER
            backend.normals.surface();
#endif
        } else if(e->shape==3&&e->vertex_count>=3) {
            Vector2 points[8];
            for(int j=0;j<e->vertex_count;++j) {
                float px=e->vertices[2*j]-e->w/2,py=e->vertices[2*j+1]-e->h/2;
                points[j]={x+e->w/2+px*cosf(pose.angle)-py*sinf(pose.angle),y+e->h/2+px*sinf(pose.angle)+py*cosf(pose.angle)};
            }
            for(int j=1;j<e->vertex_count-1;++j) {
                auto a=points[0],b=points[j],c=points[j+1];
                if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)>0) std::swap(b,c);
                DrawTriangle(a,b,c,rgba(e->color));
            }
        } else if(e->shape==1) DrawCircleV({x+e->w/2,y+e->h/2},fminf(e->w,e->h)/2,rgba(e->color));
        else DrawRectanglePro(Rectangle{x+e->w/2,y+e->h/2,e->w,e->h},Vector2{e->w/2,e->h/2},pose.angle*180/PI_F,rgba(e->color));
    }
}
#ifndef SC_HAS_ADVANCED_RENDER
/* Lightweight fallback; advanced builds use authored geometry below. */
static float ray_distance(const ScWorld *w,float x,float y,float dx,float dy,float radius) {
    float tile=(float)w->map.tile_size;
    int cell_x=(int)floorf(x/tile),cell_y=(int)floorf(y/tile);
    if (sc_tile(w,cell_x,cell_y)=='#') return 0;
    int step_x=dx>=0?1:-1,step_y=dy>=0?1:-1;
    float delta_x=fabsf(dx)>1e-6f?fabsf(tile/dx):1e20f;
    float delta_y=fabsf(dy)>1e-6f?fabsf(tile/dy):1e20f;
    float next_x=fabsf(dx)>1e-6f?(((float)(cell_x+(step_x>0?1:0))*tile)-x)/dx:1e20f;
    float next_y=fabsf(dy)>1e-6f?(((float)(cell_y+(step_y>0?1:0))*tile)-y)/dy:1e20f;
    float distance=0;
    while (distance<radius) {
        if (next_x<next_y) { distance=next_x; next_x+=delta_x; cell_x+=step_x; }
        else { distance=next_y; next_y+=delta_y; cell_y+=step_y; }
        if (sc_tile(w,cell_x,cell_y)=='#') return fminf(radius,distance+1.2f);
    }
    return radius;
}
#endif
static void light_vertex(float x,float y,Color color,float intensity) {
    rlColor4ub((unsigned char)((float)color.r*intensity),(unsigned char)((float)color.g*intensity),(unsigned char)((float)color.b*intensity),255);
    rlVertex2f(x,y);
}
static void draw_lights(const ScWorld *world,ScScript* script) {
#ifdef SC_HAS_ADVANCED_RENDER
    ScLighting defaults;
    auto& settings=script?script->lighting:defaults;
    settings.presented=false; settings.lights=settings.shadow_lights=0;
    if(!backend.occlusion) { snprintf(backend.error,sizeof backend.error,"lighting geometry is not prepared"); return; }
    ScLightFrame frame;
    auto collected=collect_occlusion(*backend.occlusion,*world,settings,frame,backend.alpha);
    settings.occluders=backend.occlusion->size(); settings.required_occluders=backend.occlusion->required();
    if(!collected) {
        if(backend.occlusion->required()>backend.occlusion->capacity())
            snprintf(backend.error,sizeof backend.error,"%s (required=%zu, capacity=%zu)",collected.error(),backend.occlusion->required(),backend.occlusion->capacity());
        else snprintf(backend.error,sizeof backend.error,"%s",collected.error());
        settings.error=backend.error; return;
    }
#else
    (void)script;
#endif
    unsigned char ambient=(unsigned char)(fminf(1,fmaxf(0,world->ambient))*255);
    BeginTextureMode(backend.light.get());
    BeginMode2D(backend.camera);
    ClearBackground(Color{ambient,ambient,ambient,255});
    BeginBlendMode(BLEND_ADDITIVE);
#ifdef SC_HAS_ADVANCED_RENDER
    for(std::size_t i=0;i<frame.count;++i) {
        const auto& light=frame.lights[i];
        const auto color=rgba(light.color);
        const int samples=light.shadows&&light.softness>0?light.samples:1;
        for(int sample=0;sample<samples;++sample) {
            const auto offset=sc_light_sample(light.softness,samples,sample);
            const auto points=backend.occlusion->outline(light.x+offset.x,light.y+offset.y,light.radius,light.ignore,light.shadows);
            const float strength=light.intensity*(static_cast<float>(color.a)/255)*offset.weight;
            const auto screen=sc_camera_to_screen(backend.view,{light.x+offset.x,light.y+offset.y});
            const auto normal_texture=backend.normals.light(screen.x,screen.y,light.height*backend.view.zoom);
            rlBegin(RL_TRIANGLES);
            // Changing primitive mode resets raylib's batch texture to white.
            rlSetTexture(normal_texture?normal_texture:rlGetTextureIdDefault());
            for(std::size_t j=0;j+1<points.size();++j) {
                light_vertex(light.x+offset.x,light.y+offset.y,color,strength);
                light_vertex(points[j+1].x,points[j+1].y,color,std::max(0.f,1-points[j+1].distance/light.radius)*strength);
                light_vertex(points[j].x,points[j].y,color,std::max(0.f,1-points[j].distance/light.radius)*strength);
            }
            rlEnd();
            backend.normals.end_light();
        }
    }
    settings.lights=frame.count; settings.shadow_lights=frame.shadow_count;
    settings.presented=true; settings.error.clear();
#else
    int count=0;
    for(const auto& e:world->entities) {
        if(!e.alive||e.glow<=0) continue;
        const auto pose=sc_display_pose(*world,e,backend.alpha);
        const float wx=pose.x+e.w*.5f,wy=pose.y+e.h*.5f,x=wx,y=wy,r=e.glow;
        if(!visible(x-r,y-r,2*r,2*r)) continue;
        if(count++==LIGHT_CAPACITY) break;
        Color c=rgba(e.color);
        c.r=(unsigned char)(160+(int)c.r*95/255); c.g=(unsigned char)(160+(int)c.g*95/255); c.b=(unsigned char)(160+(int)c.b*95/255);
        std::array<Vector2,LIGHT_RAYS+1> points{}; std::array<float,LIGHT_RAYS+1> intensities{};
        for(std::size_t j=0;j<=LIGHT_RAYS;++j) {
            const float angle=static_cast<float>(j)*(2*PI_F)/static_cast<float>(LIGHT_RAYS);
            const float dx=cosf(angle),dy=sinf(angle),d=ray_distance(world,wx,wy,dx,dy,r);
            points[j]={x+dx*d,y+dy*d}; intensities[j]=fmaxf(0,1-d/r);
        }
        rlSetTexture(0); rlBegin(RL_TRIANGLES);
        for(std::size_t j=0;j<LIGHT_RAYS;++j) {
            light_vertex(x,y,c,.95f);
            light_vertex(points[j+1].x,points[j+1].y,c,intensities[j+1]*.95f);
            light_vertex(points[j].x,points[j].y,c,intensities[j]*.95f);
        }
        rlEnd();
    }
#endif
    EndBlendMode(); EndMode2D(); EndTextureMode();
}
void sc_render_audio(const ScScript* script) {
    auto result=backend.audio.update(*script->audio,
        {script->world->tones.data(),static_cast<size_t>(script->world->tone_count)},backend.root);
    if(!result) snprintf(backend.error,sizeof backend.error,"audio: %s",result.error().c_str());
}
bool sc_render_profile_enable() { return backend.open && backend.gpu_timer.open(); }
const char* sc_render_device() { return backend.open?sc_gpu_renderer():nullptr; }
static void draw_reload_error(const char* error) {
    const float width=static_cast<float>(backend.width)-16;
    const float height=std::min(88.f,static_cast<float>(backend.height)-16);
    const float top=std::min(88.f,static_cast<float>(backend.height)-height-4);
    DrawRectangleRec({8,top,width,height},{44,12,18,248});
    const auto font=GetFontDefault();
    const char* title="RELOAD FAILED - previous scene retained";
    if(MeasureTextEx(font,title,10,1).x>width-12) title="RELOAD FAILED";
    if(MeasureTextEx(font,title,10,1).x>width-12) title="ERROR";
    DrawTextEx(font,title,{14,top+7},10,1,{255,157,142,255});
    for(float y=top+23;*error&&y+10<=top+height-4;y+=11) {
        char line[256]{}; size_t length=0;
        while(*error&&*error!='\n') {
            int bytes=0; (void)GetCodepointNext(error,&bytes);
            if(length+static_cast<size_t>(bytes)>=sizeof line) break;
            std::memcpy(line+length,error,static_cast<size_t>(bytes)); line[length+bytes]='\0';
            if(MeasureTextEx(font,line,10,1).x>width-12) {
                line[length]='\0';
                if(!length) error+=bytes;
                break;
            }
            length+=static_cast<size_t>(bytes); error+=bytes;
        }
        if(*error=='\n') ++error;
        DrawTextEx(font,line,{14,y},10,1,{218,188,183,255});
    }
}
static void draw_notice(ScRenderNotice notice) {
    if(notice.kind==ScRenderNoticeKind::reload_error) {
        if(notice.detail[0]) draw_reload_error(notice.detail);
        return;
    }
    const char* text="";
    bool failed=false;
    switch(notice.kind) {
    case ScRenderNoticeKind::save_pending: text="SAVING CHECKPOINT..."; break;
    case ScRenderNoticeKind::save_error: text="SAVE FAILED - see log"; failed=true; break;
    case ScRenderNoticeKind::read_pending: text="READING CHECKPOINT..."; break;
    case ScRenderNoticeKind::read_error: text="SAVE READ FAILED - see log"; failed=true; break;
    case ScRenderNoticeKind::stream_pending: text="LOADING MAP CHUNKS..."; break;
    case ScRenderNoticeKind::stream_error: text="MAP LOAD FAILED - see log"; failed=true; break;
    case ScRenderNoticeKind::image_pending: text="PREPARING IMAGES..."; break;
    case ScRenderNoticeKind::image_error: text="IMAGE LOAD FAILED - see log"; failed=true; break;
    case ScRenderNoticeKind::reload_error: break;
    }
    // A short footer leaves the game's recovery dialog and its controls visible.
    const float width=static_cast<float>(backend.width),top=static_cast<float>(backend.height)-16;
    DrawRectangleRec({0,top,width,16},failed?Color{44,12,18,248}:Color{8,18,28,248});
    const float size=std::min(10.f,10.f*std::max(1.f,width-8)/static_cast<float>(MeasureText(text,10)));
    DrawTextEx(GetFontDefault(),text,{4,top+3},size,1,failed?Color{255,157,142,255}:Color{188,212,230,255});
}
#ifdef SC_HAS_DEVTOOLS
static void inspector_text(const ScScript& script,std::string_view text,int x,int y,Color color) {
    const auto layout=sc_text_layout(script.world,text,14);
    const auto dpi=static_cast<float>(GetRenderWidth())/static_cast<float>(std::max(1,GetScreenWidth()));
    const int size=std::clamp(static_cast<int>(std::ceil(14*dpi)),1,512);
    for(const auto& letter:layout.letters)
        DrawTextCodepoint(font_asset(letter.font,letter.codepoint,letter.font?size:0),letter.codepoint,
                          {static_cast<float>(x)+letter.x,static_cast<float>(y)+letter.y},14,color);
}
#endif
#ifdef SC_HAS_DEVTOOLS
ScValue sc_render_inspector(const ScValue& request) { return backend.inspector.configure(request); }
#endif
void sc_render_frame(ScWorld *world,float alpha,ScRenderNotice notice,bool paused,ScRenderProfile* profile,ScScript* script,const char* capture) {
#ifdef SC_HAS_STREAMING
    backend.room_images=script?script->images.get():nullptr;
    struct RoomImageScope { ~RoomImageScope() { backend.room_images=nullptr; } } image_scope;
    if(backend.images) backend.images->collect();
#endif
    auto start=profile?ScProfileClock::now():ScProfileClock::time_point{};
    const float font_scale=font_display_scale();
    if(backend.font_scale!=font_scale) {
        // Resize replaces raster variants instead of retaining every old window size.
        for(auto& font:backend.fonts) font.pages.clear();
        backend.font_scale=font_scale;
    }
#ifdef SC_HAS_ADVANCED_RENDER
    backend.material_bindings=script?script->materials.get():nullptr;
    struct FrameBindings { ~FrameBindings() { backend.material_bindings=nullptr; } } binding_scope;
    (void)backend.materials.sync(script?script->materials.get():nullptr,false); // Errors stay on materials; retain valid programs.
#endif
#ifndef SC_HAS_DEVTOOLS
    (void)script;
#endif
    if(profile) backend.gpu_timer.begin(*profile);
    backend.alpha=alpha;
    if (key_pressed(KEY_F1)) backend.stats=!backend.stats;
    if (key_pressed(KEY_F2)) backend.hitboxes=!backend.hitboxes;
    if (key_pressed(KEY_F3)) backend.lighting=!backend.lighting;
    backend.view=sc_display_camera(*world,alpha); backend.pixel_snap=world->camera.pixel_snap;
    const auto& view=backend.view;
    backend.camera={{view.offset.x,view.offset.y},{view.center.x,view.center.y},view.rotation*180/PI_F,view.zoom};
    BeginTextureMode(backend.scene.get());
    ClearBackground(rgba(world->map.background));
    BeginMode2D(backend.camera);
    if(world->layers.empty()) draw_map(world);
    draw_entities(world); draw_commands(world,false);
    EndMode2D(); EndTextureMode();
#ifdef SC_HAS_ADVANCED_RENDER
    if(backend.lighting&&backend.normals.begin(view.rotation)) {
        BeginMode2D(backend.camera);
        if(world->layers.empty()) draw_map(world);
        draw_entities(world); draw_commands(world,false);
        EndMode2D(); backend.normals.end();
    }
#endif
    if (backend.lighting) draw_lights(world,script);
    BeginTextureMode(backend.final.get());
    ClearBackground(BLACK);
    Rectangle source={0,0,(float)backend.width,-(float)backend.height};
    // These targets already contain composited RGB. Copying them with source
    // alpha would darken translucent geometry a second time.
    rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD); BeginBlendMode(BLEND_CUSTOM);
    DrawTextureRec(backend.scene.get().texture,source,Vector2{0,0},WHITE);
    EndBlendMode();
    if (backend.lighting) {
        BeginBlendMode(BLEND_MULTIPLIED);
        DrawTextureRec(backend.light.get().texture,source,Vector2{0,0},WHITE);
        EndBlendMode();
    }
    BeginMode2D(backend.camera);
    if(auto* p=world->projectiles.get()) {
        std::array<Texture2D*,64> atlases{};
        std::array<std::uint64_t,64> atlas_materials{};
        std::bitset<64> used;
        for(std::size_t i=0;i<p->count;++i) if(p->sprite[i]) used.set(p->sprite[i]-1);
        for(size_t i=0;i<p->sprite_count;++i) {
            if(!used.test(i)) continue;
            const auto& sprite=p->sprites[i];
            if(sprite.resource<world->resources.size()) {
                const auto* path=world->resources[sprite.resource].path.c_str();
                atlases[i]=texture(path); atlas_materials[i]=image_material(path);
            }
        }
        ScQuadBatch batch(material_programs(),texture);
        for(size_t i:p->draw_order()) {
            const auto point=p->previous_display.empty()?ScCameraPoint{p->x[i],p->y[i]}:
                sc_display_point(*world,p->previous_display[i],p->x[i],p->y[i],alpha);
            float x=point.x,y=point.y,hw=p->radius[i],hh=hw;
            float u0=0,v0=0,u1=1,v1=1; unsigned int desired_texture=rlGetTextureIdDefault();
            std::uint64_t material=0;
            if(p->sprite[i]) {
                const auto index=static_cast<size_t>(p->sprite[i]-1); const auto& sprite=p->sprites[index];
                auto* atlas=atlases[index]; if(!atlas) continue;
                desired_texture=atlas->id; material=atlas_materials[index]; hw=sprite.width*.5f; hh=sprite.height*.5f;
                u0=static_cast<float>(sprite.x)/static_cast<float>(atlas->width); v0=static_cast<float>(sprite.y)/static_cast<float>(atlas->height);
                u1=static_cast<float>(sprite.x+sprite.w)/static_cast<float>(atlas->width); v1=static_cast<float>(sprite.y+sprite.h)/static_cast<float>(atlas->height);
            }
            if(!visible(x-hw,y-hh,2*hw,2*hh)) continue;
            batch.draw(desired_texture,{u0,v0,u1-u0,v1-v0},{x-hw,y-hh,2*hw,2*hh},rgba(p->color[i]),material);
        }
    }
    {
        const auto& p=world->particles;
        const auto definitions=p.definitions();
        std::array<Texture2D*,64> images{};
        std::array<std::uint64_t,64> image_materials{};
        std::bitset<64> used;
        for(std::size_t i=0;i<p.count;++i) if(p.emitter[i]) used.set(p.emitter[i]-1);
        for(std::size_t i=0;i<definitions.size();++i) {
            if(!used.test(i)) continue;
            const auto resource=definitions[i].image;
            if(resource<world->resources.size()) {
                const auto* path=world->resources[resource].path.c_str();
                images[i]=texture(path); image_materials[i]=image_material(path);
            }
        }
        ScQuadBatch batch(material_programs(),texture);
        for(std::size_t i=0;i<p.count;++i) {
            const auto point=p.previous_display.empty()?ScCameraPoint{p.x[i],p.y[i]}:
                sc_display_point(*world,p.previous_display[i],p.x[i],p.y[i],alpha);
            const float x=position(point.x),y=position(point.y),size=backend.pixel_snap?std::ceil(p.size[i]):p.size[i];
            float width=size,height=size,u0=0,v0=0,u1=1,v1=1;
            auto desired_texture=rlGetTextureIdDefault(); bool desired_additive=false; std::uint64_t material=0;
            if(p.emitter[i]) {
                const auto index=static_cast<std::size_t>(p.emitter[i]-1);
                const auto& style=definitions[index]; desired_additive=style.additive;
                if(style.image<128) {
                    const auto* image=images[index]; if(!image) continue;
                    desired_texture=image->id; material=image_materials[index];
                    const float longest=static_cast<float>(std::max(style.w,style.h));
                    width=size*static_cast<float>(style.w)/longest; height=size*static_cast<float>(style.h)/longest;
                    u0=static_cast<float>(style.x)/static_cast<float>(image->width); v0=static_cast<float>(style.y)/static_cast<float>(image->height);
                    u1=static_cast<float>(style.x+style.w)/static_cast<float>(image->width); v1=static_cast<float>(style.y+style.h)/static_cast<float>(image->height);
                }
            }
            if(size<=0||!visible(x,y,width,height)) continue;
            Color c=rgba(p.color[i]);
            if(!p.emitter[i]) c.a=static_cast<unsigned char>(float(c.a)*p.life[i]/p.max_life[i]);
            batch.draw(desired_texture,{u0,v0,u1-u0,v1-v0},{x,y,width,height},c,material,desired_additive);
        }
    }
    EndMode2D();
#ifdef SC_HAS_ADVANCED_RENDER
    EndTextureMode();
    const auto processed=backend.postprocess.apply(backend.final.get().texture,script?script->materials.get():nullptr,backend.materials,texture);
    BeginTextureMode(backend.final.get());
    if(processed.id!=backend.final.get().texture.id) {
        rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD); BeginBlendMode(BLEND_CUSTOM);
        DrawTextureRec(processed,source,{0,0},WHITE); EndBlendMode();
    }
#endif
    EndTextureMode();
    if(world->clipboard_write) { SetClipboardText(world->clipboard_out.data()); world->clipboard_write=false; }
    float input_scale=viewport_scale();
    sc_platform_text_position(static_cast<int>(world->text_x*input_scale+(static_cast<float>(GetScreenWidth())-static_cast<float>(backend.width)*input_scale)*.5f),static_cast<int>(world->text_y*input_scale+(static_cast<float>(GetScreenHeight())-static_cast<float>(backend.height)*input_scale)*.5f),world->text_focus);
    BeginDrawing(); ClearBackground(Color{4,8,12,255});
    int sw=GetScreenWidth(),sh=GetScreenHeight();
    float scale=viewport_scale();
    float width=static_cast<float>(backend.width)*scale,height=static_cast<float>(backend.height)*scale;
    rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD); BeginBlendMode(BLEND_CUSTOM);
    DrawTexturePro(backend.final.get().texture,source,Rectangle{((float)sw-width)*.5f,((float)sh-height)*.5f,width,height},Vector2{0,0},0,WHITE);
    EndBlendMode();
    // UI is composited at window resolution; the pixel scene stays at its authored resolution.
    const Vector2 origin{(static_cast<float>(sw)-width)*.5f,(static_cast<float>(sh)-height)*.5f};
    const Camera2D overlay_camera{origin,{0,0},0,scale};
    auto clip_overlay=[&]() {
        BeginScissorMode(static_cast<int>(std::floor(origin.x)),static_cast<int>(std::floor(origin.y)),
                        static_cast<int>(std::ceil(width)),static_cast<int>(std::ceil(height)));
    };
    clip_overlay();
    BeginMode2D(overlay_camera);
    if (world->message[0]) {
        Vector2 size=MeasureTextEx(GetFontDefault(),world->message,10,1);
        DrawRectangle(12,49,(int)fminf((float)backend.width-24,size.x+12),16,Color{5,13,18,210});
        DrawTextEx(GetFontDefault(),world->message,Vector2{18,52},10,1,Color{170,190,177,255});
    }
    draw_commands(world,true,scale,origin);
    EndMode2D();
    clip_overlay();
    auto debug_camera=backend.camera;
    debug_camera.offset={origin.x+debug_camera.offset.x*scale,origin.y+debug_camera.offset.y*scale};
    debug_camera.zoom*=scale;
    BeginMode2D(debug_camera);
    if (backend.hitboxes) for (std::size_t i=0;i<world->entities.size();i++) {
        const ScEntity *e=&world->entities[i]; if (!e->alive) continue;
        DrawRectangleLines((int)floorf(e->x),(int)floorf(e->y),(int)e->w,(int)e->h,e->grounded?GREEN:MAGENTA);
    }
    EndMode2D();
    BeginMode2D(overlay_camera);
    if (backend.stats) {
        int entities=0,particles=0;
        for (std::size_t i=0;i<world->entities.size();i++) if (world->entities[i].alive) entities++;
        particles=static_cast<int>(world->particles.count);
        DrawRectangle(8,48,backend.width-16,36,Color{3,8,13,240});
        DrawText(TextFormat("%.0f FPS  |  tick %llu  |  bodies %d  |  particles %d",1.0f/std::max(backend.delta,0.000001f),(unsigned long long)world->tick,entities,particles),13,53,10,Color{154,221,189,255});
        DrawText("F2 bounds / F3 lights / F5 reload / P pause / O step",13,68,10,Color{137,155,161,255});
    }
    if (paused) DrawText("PAUSED  [O] STEP  [P] RESUME",12,87,10,Color{245,196,108,255});
    draw_notice(notice);
    EndMode2D();
    // raylib is built with custom frame control; EndDrawing only submits.
    EndScissorMode();
#ifdef SC_HAS_DEVTOOLS
    if(script)
        backend.inspector.draw(*script,backend.delta,key_pressed(KEY_F4),key_pressed(KEY_F6),key_pressed(KEY_F7),key_pressed(KEY_F8),inspector_text);
#endif
    EndDrawing();
    if(profile) {
        backend.gpu_timer.end();
        profile->submit_ms=sc_profile_ms(start);
    }
    if(capture) {
        ScProfileScope timing(profile?&profile->capture_ms:nullptr);
        ImageOwner image{LoadImageFromScreen()};
        if(!image || !ExportImage(image.get(),capture))
            snprintf(backend.error,sizeof backend.error,"cannot write capture PNG");
    }
    {
        ScProfileScope timing(profile?&profile->present_ms:nullptr);
        SwapScreenBuffer();
        const double remaining=1.0/60.0-(GetTime()-backend.frame_end);
        if(backend.vsync && remaining>0) WaitTime(remaining);
    }
    { ScProfileScope timing(profile?&profile->poll_ms:nullptr); PollInputEvents(); }
    double now=GetTime(); backend.delta=static_cast<float>(now-backend.frame_end); backend.frame_end=now;
}
