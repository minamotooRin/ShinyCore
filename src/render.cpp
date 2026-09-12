#include "shiny/render.h"
#include "shiny/script.h"
#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <array>
#include <utility>
#include <vector>
#include <string.h>

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
using ImageOwner=Owned<Image,IsImageValid,UnloadImage>;
struct Asset { char path[128]{}; TextureOwner texture; };
struct Backend final {
    TargetOwner scene, light, final;
    std::array<Asset,TEXTURE_CAPACITY> assets;
    std::array<SoundOwner,TONE_VOICES> sounds;
    std::array<bool,512> pressed_keys{};
    std::size_t asset_count{},voice{};
    int width{},height{};
    bool audio{},open{},stats{},hitboxes{},lighting{true};
    char root[SC_PATH_MAX]{},error[SC_ERROR_MAX]{};
    Backend()=default;
    Backend(const Backend&)=delete;
    Backend& operator=(const Backend&)=delete;
    ~Backend() { close(); }
    void close() noexcept {
        // Devices must outlive the resources released through them.
        for (auto& sound:sounds) sound.reset();
        for (auto& asset:assets) { asset.texture.reset(); asset.path[0]='\0'; }
        final.reset();light.reset();scene.reset();
        if (audio) CloseAudioDevice();
        if (open) CloseWindow();
        audio=open=stats=hitboxes=false; lighting=true;
        asset_count=voice=0; width=height=0; root[0]=error[0]='\0';pressed_keys.fill(false);
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
    struct { const char *path; int width,height; } infos[TEXTURE_CAPACITY];
    int count=0;
    SetTraceLogCallback(raylib_log); SetTraceLogLevel(LOG_WARNING);
    for (std::size_t i=0;i<SC_MAX_ENTITIES;i++) {
        const ScEntity *e=&world->entities[i];
        if (!e->alive || !e->sprite[0]) continue;
        int info=0;
        while (info<count && strcmp(infos[info].path,e->sprite)) info++;
        if (info==count) {
            if (count==TEXTURE_CAPACITY) { snprintf(error,error_size,"texture capacity exceeded (%d)",TEXTURE_CAPACITY); return false; }
            char path[SC_PATH_MAX*2]; snprintf(path,sizeof(path),"%s/%s",root,e->sprite);
            ImageOwner image{LoadImage(path)};
            if (!image) { snprintf(error,error_size,"cannot decode texture: %s",path); return false; }
            infos[info].path=e->sprite; infos[info].width=image.get().width; infos[info].height=image.get().height;
            count++;
        }
        int width=infos[info].width,height=infos[info].height;
        int fw=e->frame_w?e->frame_w:width,fh=e->frame_h?e->frame_h:height;
        if (width<1 || height<1 || width>8192 || height>8192 || fw<1 || fh<1 || width%fw || height%fh || e->frame<0 || e->frame>=(width/fw)*(height/fh)) {
            snprintf(error,error_size,"invalid sprite frame %d or grid %dx%d for %s (%dx%d)",e->frame,fw,fh,e->sprite,width,height); return false;
        }
    }
    return true;
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
    backend.open=true;
    SetWindowMinSize(backend.width,backend.height); SetTargetFPS(60);
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
void sc_render_close(void) { backend.close(); }

bool sc_render_should_close(void) {
    backend.pressed_keys.fill(false);
    for (int key=GetKeyPressed();key;key=GetKeyPressed()) {
        if (key>0 && key<512) backend.pressed_keys[static_cast<std::size_t>(key)]=true;
    }
    return WindowShouldClose();
}
float sc_render_delta(void) { return GetFrameTime(); }
static bool key_pressed(int key) { return backend.pressed_keys[static_cast<std::size_t>(key)] || IsKeyPressed(key); }
bool sc_render_reload_requested(void) { return key_pressed(KEY_F5); }
bool sc_render_pause_requested(void) { return key_pressed(KEY_P); }
bool sc_render_step_requested(void) { return key_pressed(KEY_O); }
uint32_t sc_render_pressed_input(void) {
    uint32_t mask=0;
    if (key_pressed(KEY_LEFT)||key_pressed(KEY_A)) mask|=SC_LEFT;
    if (key_pressed(KEY_RIGHT)||key_pressed(KEY_D)) mask|=SC_RIGHT;
    if (key_pressed(KEY_UP)||key_pressed(KEY_W)) mask|=SC_UP;
    if (key_pressed(KEY_DOWN)||key_pressed(KEY_S)) mask|=SC_DOWN;
    if (key_pressed(KEY_SPACE)||key_pressed(KEY_Z)) mask|=SC_JUMP;
    if (key_pressed(KEY_E)||key_pressed(KEY_X)) mask|=SC_ACTION;
    return mask;
}
uint32_t sc_render_input(void) {
    uint32_t mask=0;
    if (IsKeyDown(KEY_LEFT)||IsKeyDown(KEY_A)) mask|=SC_LEFT;
    if (IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_D)) mask|=SC_RIGHT;
    if (IsKeyDown(KEY_UP)||IsKeyDown(KEY_W)) mask|=SC_UP;
    if (IsKeyDown(KEY_DOWN)||IsKeyDown(KEY_S)) mask|=SC_DOWN;
    if (IsKeyDown(KEY_SPACE)||IsKeyDown(KEY_Z)) mask|=SC_JUMP;
    if (IsKeyDown(KEY_E)||IsKeyDown(KEY_X)) mask|=SC_ACTION;
    if (IsGamepadAvailable(0)) {
        float x=GetGamepadAxisMovement(0,GAMEPAD_AXIS_LEFT_X);
        if (x<-.25f || IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_LEFT)) mask|=SC_LEFT;
        if (x>.25f || IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) mask|=SC_RIGHT;
        if (IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_UP)) mask|=SC_UP;
        if (IsGamepadButtonDown(0,GAMEPAD_BUTTON_LEFT_FACE_DOWN)) mask|=SC_DOWN;
        if (IsGamepadButtonDown(0,GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) mask|=SC_JUMP;
        if (IsGamepadButtonDown(0,GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) mask|=SC_ACTION;
    }
    return mask;
}
static void draw_commands(const ScWorld *world,bool screen,float cx,float cy) {
    for (std::size_t i=0;i<static_cast<std::size_t>(world->draw_count);i++) {
        const ScDraw *d=&world->draws[i]; if (d->screen!=screen) continue;
        float x=floorf(d->x-(screen?0:cx)), y=floorf(d->y-(screen?0:cy));
        if (d->kind==SC_DRAW_RECT) DrawRectangleRec(Rectangle{x,y,d->w,d->h},rgba(d->color));
        else if (d->kind==SC_DRAW_CIRCLE) DrawCircleV(Vector2{x,y},d->w,rgba(d->color));
        else if (d->kind==SC_DRAW_TEXT) DrawTextEx(GetFontDefault(),d->text,Vector2{x,y},d->h,1,rgba(d->color));
    }
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
static void draw_entities(const ScWorld *world,float cx,float cy) {
    std::array<std::size_t,SC_MAX_ENTITIES> order{}; std::size_t count=0;
    for (std::size_t i=0;i<SC_MAX_ENTITIES;i++) if (world->entities[i].alive) {
        std::size_t j=count;
        while (j>0 && world->entities[order[j-1]].layer>world->entities[i].layer) { order[j]=order[j-1]; j--; }
        order[j]=i; count++;
    }
    for (std::size_t i=0;i<count;i++) {
        const ScEntity *e=&world->entities[order[i]];
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
            DrawTexturePro(*asset,source,Rectangle{x,y,e->w,e->h},Vector2{0,0},0,rgba(e->color));
        } else DrawRectangleRec(Rectangle{x,y,e->w,e->h},rgba(e->color));
    }
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
    for (std::size_t i=0;i<SC_MAX_ENTITIES && count<LIGHT_CAPACITY;i++) {
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
void sc_render_audio(const ScWorld *world) {
    if (!backend.audio) return;
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
    draw_map(world,cx,cy); draw_entities(world,cx,cy); draw_commands(world,false,cx,cy);
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
    for (std::size_t i=0;i<SC_MAX_PARTICLES;i++) {
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
    if (backend.hitboxes) for (std::size_t i=0;i<SC_MAX_ENTITIES;i++) {
        const ScEntity *e=&world->entities[i]; if (!e->alive) continue;
        DrawRectangleLines((int)floorf(e->x-cx),(int)floorf(e->y-cy),(int)e->w,(int)e->h,e->grounded?GREEN:MAGENTA);
    }
    if (backend.stats) {
        int entities=0,particles=0;
        for (std::size_t i=0;i<SC_MAX_ENTITIES;i++) if (world->entities[i].alive) entities++;
        for (std::size_t i=0;i<SC_MAX_PARTICLES;i++) if (world->particles[i].life>0) particles++;
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
    BeginDrawing(); ClearBackground(Color{4,8,12,255});
    int sw=GetScreenWidth(),sh=GetScreenHeight();
    int scale_x=sw/backend.width,scale_y=sh/backend.height;
    int scale=scale_x<scale_y?scale_x:scale_y; if (scale<1) scale=1;
    float width=(float)(backend.width*scale),height=(float)(backend.height*scale);
    DrawTexturePro(backend.final.get().texture,source,Rectangle{((float)sw-width)*.5f,((float)sh-height)*.5f,width,height},Vector2{0,0},0,WHITE);
    EndDrawing();
}
bool sc_render_capture(const char *path) {
    ImageOwner image{LoadImageFromTexture(backend.final.get().texture)};
    if (!image) return false;
    ImageFlipVertical(image.ptr());
    ImageResizeNN(image.ptr(),backend.width*3,backend.height*3);
    return ExportImage(image.get(),path);
}
