#include "shiny/render.h"
#include "shiny/script.h"
#include "shiny/project.h"
#include "shiny/projectiles.h"
#include "shiny/platform.h"
#include "shiny/text.h"
#include "raylib.h"
#include "rlgl.h"
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
#include <stdexcept>

namespace {
constexpr int TEXTURE_CAPACITY=64, TONE_VOICES=16, LIGHT_RAYS=128, LIGHT_CAPACITY=32;
constexpr float PI_F=3.14159265358979323846f;

// Move-only C-resource owners. All borrowed handles expire when the owner resets.
template<class T, bool (*Valid)(T), void (*Release)(T)>
class Owned final {
    T value_{};
public:
    Owned() noexcept=default;
    explicit Owned(T value) noexcept:value_(value) {}
    Owned(const Owned&)=delete;
    Owned& operator=(const Owned&)=delete;
    Owned(Owned&& other) noexcept:value_(std::exchange(other.value_,T{})) {}
    Owned& operator=(Owned&& other) noexcept {
        if (this!=&other) reset(std::exchange(other.value_,T{}));
        return *this;
    }
    ~Owned() { reset(); }
    void reset(T value={}) noexcept { if (Valid(value_)) Release(value_); value_=value; }
    [[nodiscard]] T get() const noexcept { return value_; }
    [[nodiscard]] T* ptr() noexcept { return &value_; }
    explicit operator bool() const noexcept { return Valid(value_); }
};
using TextureOwner=Owned<Texture2D,IsTextureValid,UnloadTexture>;
using TargetOwner=Owned<RenderTexture2D,IsRenderTextureValid,UnloadRenderTexture>;
using SoundOwner=Owned<Sound,IsSoundValid,UnloadSound>;
using SoundAliasOwner=Owned<Sound,IsSoundValid,UnloadSoundAlias>;
using WaveOwner=Owned<Wave,IsWaveValid,UnloadWave>;
using ImageOwner=Owned<Image,IsImageValid,UnloadImage>;
using FontOwner=Owned<Font,IsFontValid,UnloadFont>;
using MusicOwner=Owned<Music,IsMusicValid,UnloadMusicStream>;
struct NativeVoice { uint32_t id{}; SoundAliasOwner sound; MusicOwner music; bool paused{}; std::string resource; };
struct FontPage { int first{}; FontOwner font; };
struct NativeFont { std::string name; std::vector<FontPage> pages; };
struct Asset { char path[128]{}; TextureOwner texture; };
struct Backend final {
    TargetOwner scene, light, final;
    std::array<Asset,TEXTURE_CAPACITY> assets;
    std::array<SoundOwner,TONE_VOICES> sounds;
    std::array<NativeVoice,34> audio_voices;
    std::vector<NativeFont> fonts;
    std::map<std::string,SoundOwner,std::less<>> sound_cache;
    std::array<bool,512> pressed_keys{};
    std::bitset<512> sampled_keys{};
    std::size_t asset_count{},voice{};
    int width{},height{};
    bool debug_keys{};
    ScGamepadSelection gamepad;
    bool audio{},open{},stats{},hitboxes{},lighting{true};
    bool smooth{};
    char root[SC_PATH_MAX]{},error[SC_ERROR_MAX]{};
    Backend()=default;
    Backend(const Backend&)=delete;
    Backend& operator=(const Backend&)=delete;
    ~Backend() { close(); }
    void close() noexcept {
        // Devices must outlive the resources released through them.
        for (auto& sound:sounds) sound.reset();
        for(auto& item:audio_voices) { item.sound.reset(); item.music.reset(); item.id=0; }
        sound_cache.clear();
        fonts.clear();
        for (auto& asset:assets) { asset.texture.reset(); asset.path[0]='\0'; }
        final.reset();light.reset();scene.reset();
        if (audio) CloseAudioDevice();
        if (open) { sc_platform_close(); CloseWindow(); }
        smooth=false;
        audio=open=stats=hitboxes=debug_keys=false; lighting=true; gamepad={};
        asset_count=voice=0; width=height=0; root[0]=error[0]='\0';pressed_keys.fill(false);sampled_keys.reset();
    }
} backend;
} // namespace

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
            if(r.type=="image") load(r.path);
            if(r.type=="sound"||r.type=="music") {
                WaveOwner wave{LoadWave((std::string(root)+"/"+r.path).c_str())};
                if(!wave) throw std::runtime_error("cannot decode audio: "+r.path);
            }
        }
        return true;
    } catch(const std::exception& e) { snprintf(error,error_size,"%s",e.what()); return false; }
}
static Texture2D *texture(const char *relative) {
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
    for(auto& voice:backend.audio_voices) { voice.sound.reset(); voice.music.reset(); voice.id=0; }
}
const char *sc_render_error(void) { return backend.error; }
bool sc_render_open(const ScWorld *world,const char *root,bool audio,char *error,size_t error_size) {
    backend.close();
    backend.width=world->view_width; backend.height=world->view_height;
    backend.lighting=true;
    snprintf(backend.root,sizeof(backend.root),"%s",root);
    SetTraceLogCallback(raylib_log); SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_VSYNC_HINT);
    InitWindow(backend.width*3,backend.height*3,world->title);
    if (!IsWindowReady()) { snprintf(error,error_size,"unable to create native graphics window"); return false; }
    backend.open=true; sc_platform_open(GetWindowHandle()); SetExitKey(KEY_NULL);
    SetWindowMinSize(320,180); SetTargetFPS(60);
    backend.scene.reset(LoadRenderTexture(backend.width,backend.height));
    backend.light.reset(LoadRenderTexture(backend.width,backend.height));
    backend.final.reset(LoadRenderTexture(backend.width,backend.height));
    if (!static_cast<bool>(backend.scene)||!static_cast<bool>(backend.light)||!static_cast<bool>(backend.final)) {
        snprintf(error,error_size,"unable to create render targets"); sc_render_close(); return false;
    }
    SetTextureFilter(backend.final.get().texture,TEXTURE_FILTER_POINT);
    if (audio) {
        InitAudioDevice(); backend.audio=IsAudioDeviceReady();
        if (!backend.audio) fputs("[shiny] audio device unavailable; continuing silently\n",stderr);
    }
    return true;
}
ScResult<void> sc_render_settings(const ScSettings& settings) {
    if(!backend.open) return std::unexpected("window is not open");
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
    SetTargetFPS(settings.vsync?60:0);
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
float sc_render_delta(void) { return GetFrameTime(); }
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
static FontPage font_page(const ScResource& resource,int first) {
    std::array<int,64> codes{};
    for(int i=0;i<64;++i) codes[static_cast<size_t>(i)]=first+i;
    FontPage page; page.first=first;
    page.font.reset(LoadFontFromMemory(".ttf",resource.font_bytes.data(),static_cast<int>(resource.font_bytes.size()),
        resource.size,codes.data(),static_cast<int>(codes.size())));
    if(!page.font) throw std::runtime_error("cannot upload font page: "+resource.name);
    SetTextureFilter(page.font.get().texture,TEXTURE_FILTER_POINT);
    return page;
}
static Font font_asset(const ScResource* resource,int codepoint) {
    if(!resource) return GetFontDefault();
    NativeFont* cached=nullptr;
    for(auto& font:backend.fonts) if(font.name==resource->name) { cached=&font; break; }
    if(!cached) { backend.fonts.push_back({resource->name,{}}); cached=&backend.fonts.back(); }
    const int first=codepoint/64*64;
    for(auto& page:cached->pages) if(page.first==first) return page.font.get();
    if(cached->pages.size()>=128) throw std::runtime_error("font atlas budget exhausted (128 pages): "+resource->name);
    cached->pages.push_back(font_page(*resource,first));
    return cached->pages.back().font.get();
}
bool sc_render_prepare_assets(const ScWorld* world,char* error,size_t error_size) {
    try {
        std::array<Asset,TEXTURE_CAPACITY> assets;
        std::vector<NativeFont> fonts;
        size_t count=0;
        auto prepare=[&](const char* path) {
            if(!*path) return true;
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
            if(resource.type=="image"&&!prepare(resource.path.c_str())) return false;
            if(resource.type!="font") continue;
            NativeFont item; item.name=resource.name;
            fonts.push_back(std::move(item));
        }
        // Candidate first-draw glyph uploads must succeed before replacing active assets.
        for(int i=0;i<world->draw_count;++i) {
            const auto& draw=world->draws[static_cast<size_t>(i)];
            if(draw.kind!=SC_DRAW_TEXT) continue;
            auto layout=sc_text_layout(world,draw.text,draw.h,draw.font,draw.wrap,draw.align);
            for(const auto& letter:layout.letters) if(letter.font) {
                for(auto& font:fonts) if(font.name==letter.font->name) {
                    const int first=letter.codepoint/64*64;
                    bool exists=false;
                    for(const auto& page:font.pages) if(page.first==first) exists=true;
                    if(!exists) {
                        if(font.pages.size()>=128) throw std::runtime_error("font atlas budget exhausted (128 pages)");
                        font.pages.push_back(font_page(*letter.font,first));
                    }
                }
            }
        }
        backend.assets=std::move(assets); backend.asset_count=count; backend.fonts=std::move(fonts);
        for(size_t i=0;i<world->audio.size();++i) if(!world->audio[i].persistent||world->audio[i].id!=backend.audio_voices[i].id) {
            auto& voice=backend.audio_voices[i]; voice.sound.reset(); voice.music.reset(); voice.id=0;
        }
        backend.error[0]=0;
        return true;
    } catch(const std::exception& e) { snprintf(error,error_size,"resource preparation: %s",e.what()); return false; }
}
static void text_draw(const ScWorld* world,const ScDraw& draw,float x,float y) {
    auto layout=sc_text_layout(world,draw.text,draw.h,draw.font,draw.wrap,draw.align);
    for(const auto& letter:layout.letters) DrawTextCodepoint(font_asset(letter.font,letter.codepoint),letter.codepoint,{x+letter.x,y+letter.y},draw.h,rgba(draw.color));
}
static void draw_commands(const ScWorld *world,bool screen,float cx,float cy) {
    std::array<Rectangle,32> clips{}; size_t depth=0;
    for (std::size_t i=0;i<static_cast<std::size_t>(world->draw_count);i++) {
        const ScDraw *d=&world->draws[i]; if (d->screen!=screen) continue;
        float x=floorf(d->x-(screen?0:cx)), y=floorf(d->y-(screen?0:cy));
        if (d->kind==SC_DRAW_RECT) DrawRectangleRec(Rectangle{x,y,d->w,d->h},rgba(d->color));
        else if (d->kind==SC_DRAW_CIRCLE) DrawCircleV(Vector2{x,y},d->w,rgba(d->color));
        else if (d->kind==SC_DRAW_TEXT) text_draw(world,*d,x,y);
        else if(d->kind==SC_DRAW_IMAGE) {
            if(auto* asset=texture(d->text)) DrawTexturePro(*asset,{0,0,static_cast<float>(asset->width),static_cast<float>(asset->height)},{x,y,d->w,d->h},{0,0},0,WHITE);
        } else if(d->kind==SC_DRAW_CLIP) {
            if(depth==clips.size()) throw std::runtime_error("clip stack capacity exhausted");
            Rectangle area{x,y,d->w,d->h};
            if(depth) {
                const auto& old=clips[depth-1]; float right=std::min(area.x+area.width,old.x+old.width),bottom=std::min(area.y+area.height,old.y+old.height);
                area.x=std::max(area.x,old.x); area.y=std::max(area.y,old.y); area.width=std::max(0.0f,right-area.x); area.height=std::max(0.0f,bottom-area.y);
            }
            clips[depth++]=area;
            BeginScissorMode(static_cast<int>(area.x),static_cast<int>(area.y),static_cast<int>(area.width),static_cast<int>(area.height));
        } else if(d->kind==SC_DRAW_UNCLIP) {
            if(!depth) throw std::runtime_error("clip stack underflow");
            --depth; EndScissorMode();
            if(depth) { const auto& a=clips[depth-1]; BeginScissorMode(static_cast<int>(a.x),static_cast<int>(a.y),static_cast<int>(a.width),static_cast<int>(a.height)); }
        }
    }
    if(depth) { EndScissorMode(); throw std::runtime_error("unbalanced clip stack"); }
}
static void draw_map(const ScWorld *world,float cx,float cy) {
    int tile=world->map.tile_size;
    int left=(int)floorf(cx/(float)tile),top=(int)floorf(cy/(float)tile);
    int right=left+backend.width/tile+2,bottom=top+backend.height/tile+2;
    Color base=rgba(world->map.color),accent=rgba(world->map.accent);
    for (int y=top;y<bottom && y<world->map.height;y++) for (int x=left;x<right && x<world->map.width;x++) {
        if (x<0 || y<0) continue;
        char t=sc_tile(world,x,y); if (t=='.') continue;
        int sx=x*tile-(int)cx,sy=y*tile-(int)cy;
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
static void draw_layer(const ScWorld* world,const ScLayer& layer,float cx,float cy) {
    if(!layer.visible) return;
    float tile=static_cast<float>(world->map.tile_size);
    for(size_t i=0;i<layer.cells.size();++i) {
        uint32_t gid=layer.cells[i]; if(!gid) continue;
        auto* graphic=sc_tile_graphic(world,gid); if(!graphic) continue;
        float x=static_cast<float>(i%static_cast<size_t>(world->map.width))*tile+layer.x-cx;
        float y=static_cast<float>(i/static_cast<size_t>(world->map.width))*tile+layer.y-cy;
        if(x+tile<0||y+tile<0||x>static_cast<float>(backend.width)||y>static_cast<float>(backend.height)) continue;
        auto* asset=texture(graphic->image.c_str()); if(!asset) continue;
        rlSetTexture(asset->id); rlBegin(RL_QUADS); rlColor4ub(255,255,255,static_cast<unsigned char>(layer.opacity*255));
        const float corners[4][2]={{0,0},{0,1},{1,1},{1,0}};
        for(const auto& corner:corners) {
            float u=corner[0],v=corner[1];
            if(gid&0x80000000u) u=1-u;
            if(gid&0x40000000u) v=1-v;
            if(gid&0x20000000u) std::swap(u,v);
            rlTexCoord2f((static_cast<float>(graphic->x)+u*static_cast<float>(graphic->w))/static_cast<float>(asset->width),
                        (static_cast<float>(graphic->y)+v*static_cast<float>(graphic->h))/static_cast<float>(asset->height));
            rlVertex2f(floorf(x)+corner[0]*tile,floorf(y)+corner[1]*tile);
        }
        rlEnd(); rlSetTexture(0);
    }
}
static void draw_entities(const ScWorld *world,float cx,float cy) {
    std::vector<std::size_t> order(world->entities.size()); std::size_t count=0;
    for (std::size_t i=0;i<world->entities.size();i++) if (world->entities[i].alive) {
        std::size_t j=count;
        while (j>0 && world->entities[order[j-1]].layer>world->entities[i].layer) { order[j]=order[j-1]; j--; }
        order[j]=i; count++;
    }
    size_t map_layer=0;
    for (std::size_t i=0;i<count;i++) {
        const ScEntity *e=&world->entities[order[i]];
        while(map_layer<world->layers.size()&&world->layers[map_layer].order<=e->layer) draw_layer(world,world->layers[map_layer++],cx,cy);
        float x=floorf(e->x-cx),y=floorf(e->y-cy);
        if (x+e->w<0||y+e->h<0||x>(float)backend.width||y>(float)backend.height) continue;
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
            DrawTexturePro(*asset,source,Rectangle{x+e->w/2,y+e->h/2,e->w,e->h},Vector2{e->w/2,e->h/2},e->angle*180/PI_F,rgba(e->color));
        } else if(e->shape==3&&e->vertex_count>=3) {
            Vector2 points[8];
            for(int j=0;j<e->vertex_count;++j) {
                float px=e->vertices[2*j]-e->w/2,py=e->vertices[2*j+1]-e->h/2;
                points[j]={x+e->w/2+px*cosf(e->angle)-py*sinf(e->angle),y+e->h/2+px*sinf(e->angle)+py*cosf(e->angle)};
            }
            for(int j=1;j<e->vertex_count-1;++j) {
                auto a=points[0],b=points[j],c=points[j+1];
                if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)>0) std::swap(b,c);
                DrawTriangle(a,b,c,rgba(e->color));
            }
        } else if(e->shape==1) DrawCircleV({x+e->w/2,y+e->h/2},fminf(e->w,e->h)/2,rgba(e->color));
        else DrawRectanglePro(Rectangle{x+e->w/2,y+e->h/2,e->w,e->h},Vector2{e->w/2,e->h/2},e->angle*180/PI_F,rgba(e->color));
    }
    while(map_layer<world->layers.size()) draw_layer(world,world->layers[map_layer++],cx,cy);
}
/* Grid DDA gives each light a shadow silhouette, without a second physics world. */
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
static void light_vertex(float x,float y,Color color,float intensity) {
    rlColor4ub((unsigned char)((float)color.r*intensity),(unsigned char)((float)color.g*intensity),(unsigned char)((float)color.b*intensity),255);
    rlVertex2f(x,y);
}
static void draw_lights(const ScWorld *world,float cx,float cy) {
    unsigned char ambient=(unsigned char)(fminf(1,fmaxf(0,world->ambient))*255);
    BeginTextureMode(backend.light.get());
    ClearBackground(Color{ambient,ambient,ambient,255});
    BeginBlendMode(BLEND_ADDITIVE);
    int count=0;
    for (std::size_t i=0;i<world->entities.size() && count<LIGHT_CAPACITY;i++) {
        const ScEntity *e=&world->entities[i];
        if (!e->alive||e->glow<=0) continue;
        float wx=e->x+e->w*.5f,wy=e->y+e->h*.5f,x=wx-cx,y=wy-cy,r=e->glow;
        if (x+r<0||y+r<0||x-r>(float)backend.width||y-r>(float)backend.height) continue;
        count++;
        Color c=rgba(e->color);
        /* White illumination retains sprite hue. Colored emitters tint nearby tiles. */
        c.r=(unsigned char)(160+(int)c.r*95/255); c.g=(unsigned char)(160+(int)c.g*95/255); c.b=(unsigned char)(160+(int)c.b*95/255);
        std::array<Vector2,LIGHT_RAYS+1> points{}; std::array<float,LIGHT_RAYS+1> intensities{};
        for (std::size_t j=0;j<=LIGHT_RAYS;j++) {
            float angle=(float)j*(2*PI_F)/(float)LIGHT_RAYS;
            float dx=cosf(angle),dy=sinf(angle),d=ray_distance(world,wx,wy,dx,dy,r);
            points[j]=Vector2{x+dx*d,y+dy*d}; intensities[j]=fmaxf(0,1-d/r);
        }
        rlSetTexture(0); rlBegin(RL_TRIANGLES);
        for (std::size_t j=0;j<LIGHT_RAYS;j++) {
            light_vertex(x,y,c,.95f);
            light_vertex(points[j+1].x,points[j+1].y,c,intensities[j+1]*.95f);
            light_vertex(points[j].x,points[j].y,c,intensities[j]*.95f);
        }
        rlEnd();
    }
    EndBlendMode(); EndTextureMode();
}
static Sound cached_sound(const std::string& path) {
    if(auto found=backend.sound_cache.find(path);found!=backend.sound_cache.end()) return found->second.get();
    SoundOwner decoded{LoadSound(path.c_str())};
    if(!decoded) throw std::runtime_error("cannot decode sound: "+path);
    constexpr std::uint64_t budget=64u*1024u*1024u;
    auto charge=[](Sound sound) { return static_cast<std::uint64_t>(sound.frameCount)*8; };
    auto resident=charge(decoded.get());
    for(const auto& [name,sound]:backend.sound_cache) { (void)name; resident+=charge(sound.get()); }
    while(backend.sound_cache.size()>=64||resident>budget) {
        auto victim=backend.sound_cache.end();
        for(auto entry=backend.sound_cache.begin();entry!=backend.sound_cache.end();++entry) {
            bool referenced=false;
            for(const auto& voice:backend.audio_voices) if(voice.sound&&voice.resource==entry->first) referenced=true;
            if(!referenced) { victim=entry; break; }
        }
        if(victim==backend.sound_cache.end()) throw std::runtime_error("decoded sound cache budget exhausted (64 MiB)");
        resident-=charge(victim->second.get()); backend.sound_cache.erase(victim);
    }
    return backend.sound_cache.emplace(path,std::move(decoded)).first->second.get();
}
void sc_render_audio(const ScWorld *world) {
    if (!backend.audio) return;
    for(size_t i=0;i<world->audio.size();++i) {
        const auto& v=world->audio[i]; auto& native=backend.audio_voices[i];
        if(!v.alive) { native.sound.reset(); native.music.reset(); native.id=0; continue; }
        if(native.id!=v.id) {
            native.sound.reset(); native.music.reset(); native.id=v.id; native.paused=false;
            std::string path=std::string(backend.root)+"/"+v.path;
            native.resource=path;
            if(v.music) {
                native.music.reset(LoadMusicStream(path.c_str()));
                if(native.music) { native.music.ptr()->looping=v.loop; PlayMusicStream(native.music.get()); if(v.position>0) SeekMusicStream(native.music.get(),v.position); }
            } else { native.sound.reset(LoadSoundAlias(cached_sound(path))); if(native.sound) PlaySound(native.sound.get()); }
            if(!native.music&&!native.sound) snprintf(backend.error,sizeof backend.error,"cannot decode audio: %s",v.path);
        }
        const auto& bus=world->audio_buses[static_cast<size_t>(v.bus)];
        const auto& master=world->audio_buses[0];
        const float volume=v.volume*master.volume*world->audio_gains[0]*(v.bus==0?1:bus.volume*world->audio_gains[static_cast<size_t>(v.bus)]);
        const bool paused=v.paused||master.paused||bus.paused;
        if(native.music) {
            native.music.ptr()->looping=v.loop;
            SetMusicVolume(native.music.get(),volume); SetMusicPan(native.music.get(),(v.pan+1)*.5f); SetMusicPitch(native.music.get(),v.pitch);
            if(paused&&!native.paused) PauseMusicStream(native.music.get());
            if(!paused&&native.paused) ResumeMusicStream(native.music.get());
            if(!paused) UpdateMusicStream(native.music.get());
        }
        if(native.sound) {
            SetSoundVolume(native.sound.get(),volume); SetSoundPan(native.sound.get(),(v.pan+1)*.5f); SetSoundPitch(native.sound.get(),v.pitch);
            if(paused&&!native.paused) PauseSound(native.sound.get());
            if(!paused&&native.paused) ResumeSound(native.sound.get());
            if(v.loop&&!paused&&!IsSoundPlaying(native.sound.get())) PlaySound(native.sound.get());
        }
        native.paused=paused;
    }
    for (std::size_t i=0;i<static_cast<std::size_t>(world->tone_count);i++) {
        ScTone tone=world->tones[i];
        unsigned int count=(unsigned int)(tone.duration*44100);
        if (count<1) continue;
        std::vector<float> samples(count);
        for (unsigned int j=0;j<count;j++) {
            float t=(float)j/44100,progress=(float)j/(float)count;
            float attack=fminf(1,t/.008f),envelope=(1-progress)*(1-progress)*attack;
            samples[j]=sinf(2*PI_F*tone.frequency*t)*envelope*tone.volume;
        }
        Wave wave={.frameCount=count,.sampleRate=44100,.sampleSize=32,.channels=1,.data=samples.data()};
        const std::size_t voice=backend.voice;
        backend.voice=(backend.voice+1)%TONE_VOICES;
        backend.sounds[voice].reset(LoadSoundFromWave(wave));
        if (backend.sounds[voice]) PlaySound(backend.sounds[voice].get());
    }
}
void sc_render_frame(ScWorld *world,float alpha,const char *error,bool paused) {
    (void)alpha; /* Pixel snapping renders the latest fixed state; no fractional sprites. */
    if (key_pressed(KEY_F1)) backend.stats=!backend.stats;
    if (key_pressed(KEY_F2)) backend.hitboxes=!backend.hitboxes;
    if (key_pressed(KEY_F3)) backend.lighting=!backend.lighting;
    float cx=floorf(world->camera_x),cy=floorf(world->camera_y);
    BeginTextureMode(backend.scene.get());
    ClearBackground(rgba(world->map.background));
    if(world->layers.empty()) draw_map(world,cx,cy);
    draw_entities(world,cx,cy); draw_commands(world,false,cx,cy);
    EndTextureMode();
    if (backend.lighting) draw_lights(world,cx,cy);
    BeginTextureMode(backend.final.get());
    ClearBackground(BLACK);
    Rectangle source={0,0,(float)backend.width,-(float)backend.height};
    DrawTextureRec(backend.scene.get().texture,source,Vector2{0,0},WHITE);
    if (backend.lighting) {
        BeginBlendMode(BLEND_MULTIPLIED);
        DrawTextureRec(backend.light.get().texture,source,Vector2{0,0},WHITE);
        EndBlendMode();
    }
    if(auto* p=world->projectiles.get()) {
        rlSetTexture(0); rlBegin(RL_QUADS);
        for(size_t i=0;i<p->count;++i) {
            float x=p->x[i]-cx,y=p->y[i]-cy,r=p->radius[i];
            if(x+r<0||y+r<0||x-r>static_cast<float>(backend.width)||y-r>static_cast<float>(backend.height)) continue;
            auto c=rgba(p->color[i]); rlColor4ub(c.r,c.g,c.b,c.a);
            rlVertex2f(x-r,y-r); rlVertex2f(x-r,y+r); rlVertex2f(x+r,y+r); rlVertex2f(x+r,y-r);
        }
        rlEnd(); rlSetTexture(0);
    }
    for (std::size_t i=0;i<world->particles.size();i++) {
        const ScParticle *p=&world->particles[i]; if (p->life<=0) continue;
        Color c=rgba(p->color); c.a=(unsigned char)((float)c.a*p->life/p->max_life);
        DrawRectangle((int)floorf(p->x-cx),(int)floorf(p->y-cy),(int)ceilf(p->size),(int)ceilf(p->size),c);
    }
    if (world->message[0]) {
        Vector2 size=MeasureTextEx(GetFontDefault(),world->message,10,1);
        DrawRectangle(12,49,(int)fminf((float)backend.width-24,size.x+12),16,Color{5,13,18,210});
        DrawTextEx(GetFontDefault(),world->message,Vector2{18,52},10,1,Color{170,190,177,255});
    }
    draw_commands(world,true,0,0);
    if (backend.hitboxes) for (std::size_t i=0;i<world->entities.size();i++) {
        const ScEntity *e=&world->entities[i]; if (!e->alive) continue;
        DrawRectangleLines((int)floorf(e->x-cx),(int)floorf(e->y-cy),(int)e->w,(int)e->h,e->grounded?GREEN:MAGENTA);
    }
    if (backend.stats) {
        int entities=0,particles=0;
        for (std::size_t i=0;i<world->entities.size();i++) if (world->entities[i].alive) entities++;
        for (std::size_t i=0;i<world->particles.size();i++) if (world->particles[i].life>0) particles++;
        DrawRectangle(8,48,backend.width-16,36,Color{3,8,13,240});
        DrawText(TextFormat("%d FPS  |  tick %llu  |  bodies %d  |  particles %d",GetFPS(),(unsigned long long)world->tick,entities,particles),13,53,10,Color{154,221,189,255});
        DrawText("F2 bounds / F3 lights / F5 reload / P pause / O step",13,68,10,Color{137,155,161,255});
    }
    if (paused) DrawText("PAUSED  [O] STEP  [P] RESUME",12,87,10,Color{245,196,108,255});
    if (error[0]) {
        DrawRectangle(8,88,backend.width-16,88,Color{44,12,18,248});
        DrawText("RELOAD FAILED - previous scene retained",14,95,10,Color{255,157,142,255});
        /* Wrap diagnostics at readable glyph boundaries without leaking beyond the panel. */
        char line[60]; size_t offset=0,length=strlen(error);
        for (int row=0;row<5 && offset<length;row++) {
            size_t n=0;
            while (n<56 && offset<length && error[offset]!='\n') line[n++]=error[offset++];
            if (offset<length && error[offset]=='\n') offset++;
            line[n]='\0'; DrawText(line,14,111+row*11,10,Color{218,188,183,255});
        }
    }
    EndTextureMode();
    if(world->clipboard_write) { SetClipboardText(world->clipboard_out.data()); world->clipboard_write=false; }
    float input_scale=viewport_scale();
    sc_platform_text_position(static_cast<int>(world->text_x*input_scale+(static_cast<float>(GetScreenWidth())-static_cast<float>(backend.width)*input_scale)*.5f),static_cast<int>(world->text_y*input_scale+(static_cast<float>(GetScreenHeight())-static_cast<float>(backend.height)*input_scale)*.5f),world->text_focus);
    BeginDrawing(); ClearBackground(Color{4,8,12,255});
    int sw=GetScreenWidth(),sh=GetScreenHeight();
    float scale=viewport_scale();
    float width=static_cast<float>(backend.width)*scale,height=static_cast<float>(backend.height)*scale;
    DrawTexturePro(backend.final.get().texture,source,Rectangle{((float)sw-width)*.5f,((float)sh-height)*.5f,width,height},Vector2{0,0},0,WHITE);
    EndDrawing();
}
bool sc_render_capture(const char *path) {
    ImageOwner image{LoadImageFromScreen()};
    if (!image) return false;
    return ExportImage(image.get(),path);
}
