#define _POSIX_C_SOURCE 200809L
#include "shiny/core.h"
#include "shiny/script.h"
#ifdef SC_HAS_GRAPHICS
#include "shiny/render.h"
#endif
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct { uint64_t frame; uint32_t mask; } ReplayEvent;
typedef struct { ScWorld world; ScScript script; } Runtime;
typedef struct {
    const char *project, *replay, *snapshot, *capture;
    bool headless, check, mute, frames_set, realtime;
    uint64_t frames;
    uint32_t seed;
} Options;

/* Wall-clock pacing belongs to the host; simulation always receives SC_DT. */
static double monotonic_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)) return -1;
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
#endif
}

static bool pace(double *deadline) {
    *deadline += 1.0 / 60.0;
    for (;;) {
        double now = monotonic_seconds();
        if (now < 0) return false;
        double remaining = *deadline - now;
        if (remaining <= 0) {
            if (remaining < -0.25) *deadline = now;
            return true;
        }
#ifdef _WIN32
        Sleep((DWORD)ceil(remaining * 1000.0));
#else
        struct timespec delay = {.tv_sec = (time_t)remaining,
            .tv_nsec = (long)((remaining - floor(remaining)) * 1000000000.0)};
        if (nanosleep(&delay, NULL) && errno != EINTR) return false;
#endif
    }
}

static void json_string(FILE *file, const char *str) {
    fputc('"', file);
    for (const unsigned char *p=(const unsigned char *)str; *p; ++p) {
        if (*p=='"' || *p=='\\') { fputc('\\',file); fputc(*p,file); }
        else if (*p<32) fprintf(file,"\\u%04x",*p);
        else fputc(*p,file);
    }
    fputc('"',file);
}
static int diagnostic(const char *code, const char *message) {
    fputs("{\"ok\":false,\"code\":",stderr); json_string(stderr,code);
    fputs(",\"error\":",stderr); json_string(stderr,message); fputs("}\n",stderr);
    return 1;
}
static bool number(const char *str, uint64_t max, uint64_t *value) {
    if (!str[0] || str[0]=='-' || str[0]=='+') return false;
    for (const char *p=str; *p; ++p) if (*p<'0'||*p>'9') return false;
    errno=0; char *end=NULL; unsigned long long n=strtoull(str,&end,10);
    if (errno || *end || n>max) return false;
    *value=(uint64_t)n; return true;
}
static void usage(void) {
    puts("ShinyCore " SC_VERSION " - a small, scriptable native 2D engine\n"
         "Usage: shiny [project-directory] [options]\n"
         "  --check             Validate main.lua, init, draw and referenced assets\n"
         "  --headless          Simulate without window, GPU or audio\n"
         "  --realtime          Pace headless ticks at 60 Hz for live networking\n"
         "  --frames N          Stop after N simulation frames (headless default: 600)\n"
         "  --seed N            Simulation seed, 1..4294967295 (default: 42)\n"
         "  --replay FILE       Frame-indexed held input: frame mask; # comments\n"
         "  --snapshot FILE     Write final deterministic state as JSON\n"
         "  --capture FILE.png  Capture final rendered framebuffer (requires --frames)\n"
         "  --mute              Disable audio device initialization\n"
         "  --api               Print machine-readable Lua API\n"
         "  --version           Print version\n"
         "  --help              Show this help\n"
         "Keys: arrows/WASD, Z/Space jump, X/E interact; F1 stats, F2 hitboxes,\n"
         "      F3 lighting, F5 reload, P pause, O single step, Esc quit.\n"
         "Replay masks: left=1 right=2 up=4 down=8 jump=16 action=32.\n"
         "Frame numbers start at zero and continue through scene changes.");
}
static bool assets_exist(const Runtime *runtime, char *error, size_t size) {
    for (int i=0;i<SC_MAX_ENTITIES;i++) {
        const ScEntity *e=&runtime->world.entities[i];
        if (!e->alive || !e->sprite[0]) continue;
        char path[SC_PATH_MAX*2];
        if (!sc_script_validate_path(e->sprite)) {
            snprintf(error,size,"invalid sprite path: %s",e->sprite); return false;
        }
        snprintf(path,sizeof(path),"%s/%s",runtime->script.root,e->sprite);
        FILE *file=fopen(path,"rb");
        if (!file) { snprintf(error,size,"sprite asset unavailable: %s",path); return false; }
        fclose(file);
    }
    return true;
}
static Runtime *runtime_open(const char *root, const char *entry, uint32_t seed, char *error, size_t size) {
    Runtime *runtime=calloc(1,sizeof(*runtime));
    if (!runtime) { snprintf(error,size,"out of memory creating world"); return NULL; }
    sc_world_init(&runtime->world,seed);
    if (!sc_script_open(&runtime->script,&runtime->world,root,entry) ||
        !sc_script_draw(&runtime->script,0)) {
        snprintf(error,size,"%s",runtime->script.error);
        sc_script_close(&runtime->script); free(runtime); return NULL;
    }
    if (!assets_exist(runtime,error,size)) {
        sc_script_close(&runtime->script); free(runtime); return NULL;
    }
#ifdef SC_HAS_GRAPHICS
    if (!sc_render_validate_assets(&runtime->world,root,error,size)) {
        sc_script_close(&runtime->script); free(runtime); return NULL;
    }
#endif
    return runtime;
}
static void runtime_close(Runtime *runtime) {
    if (runtime) { sc_script_close(&runtime->script); free(runtime); }
}
static bool read_replay(const char *path, ReplayEvent **events, size_t *count, char *error, size_t size) {
    *events=NULL; *count=0;
    if (!path) return true;
    FILE *file=fopen(path,"r");
    if (!file) { snprintf(error,size,"cannot open replay: %s",path); return false; }
    char line[256]; size_t capacity=0, line_number=0;
    while (fgets(line,sizeof(line),file)) {
        line_number++;
        if (!strchr(line,'\n') && !feof(file)) goto invalid;
        char *comment=strchr(line,'#'); if (comment) *comment='\0';
        char a[64], b[64], extra[2];
        int fields=sscanf(line," %63s %63s %1s",a,b,extra);
        if (fields==EOF || fields==0) continue;
        uint64_t frame,mask;
        if (fields!=2 || !number(a,UINT64_C(1000000000),&frame) || !number(b,63,&mask)) goto invalid;
        if (*count && frame<=(*events)[*count-1].frame) goto invalid;
        if (*count>=1000000) goto invalid;
        if (*count==capacity) {
            capacity=capacity?capacity*2:128;
            ReplayEvent *grown=realloc(*events,capacity*sizeof(**events));
            if (!grown) { snprintf(error,size,"out of memory reading replay"); goto failed; }
            *events=grown;
        }
        (*events)[(*count)++]=(ReplayEvent){frame,(uint32_t)mask};
    }
    if (ferror(file)) { snprintf(error,size,"cannot read replay: %s",path); goto failed; }
    fclose(file); return true;
invalid:
    snprintf(error,size,"%s:%zu: expected strictly increasing nonnegative frame and input mask 0..63",path,line_number);
failed:
    fclose(file); free(*events); *events=NULL; *count=0; return false;
}
static void snapshot(FILE *file, const Runtime *runtime, uint64_t frames) {
    const ScWorld *w=&runtime->world;
    fprintf(file,"{\"ok\":true,\"version\":\"%s\",\"frames\":%" PRIu64 ",\"tick\":%" PRIu64 ",\"scene\":",SC_VERSION,frames,w->tick);
    json_string(file,runtime->script.entry);
    fprintf(file,",\"rng\":%" PRIu32 ",\"hash\":\"%016" PRIx64 "\",\"message\":",w->rng,sc_state_hash(w));
    json_string(file,w->message);
    fprintf(file,",\"camera\":{\"x\":%.6g,\"y\":%.6g},\"entities\":[",(double)w->camera_x,(double)w->camera_y);
    bool first=true;
    for (int i=0;i<SC_MAX_ENTITIES;i++) {
        const ScEntity *e=&w->entities[i]; if (!e->alive) continue;
        if (!first) fputc(',',file); first=false;
        fprintf(file,"{\"id\":%" PRIu32 ",\"tag\":",e->id); json_string(file,e->tag);
        fprintf(file,",\"x\":%.9g,\"y\":%.9g,\"vx\":%.9g,\"vy\":%.9g,\"grounded\":%s}",
                (double)e->x,(double)e->y,(double)e->vx,(double)e->vy,e->grounded?"true":"false");
    }
    fputs("]}\n",file);
}

int main(int argc, char **argv) {
    Options opt={.project="examples/lantern",.frames=600,.seed=42};
    bool project_set=false;
    for (int i=1;i<argc;i++) {
        const char *arg=argv[i];
        if (!strcmp(arg,"--help") || !strcmp(arg,"-h")) { usage(); return 0; }
        if (!strcmp(arg,"--version")) { puts(SC_VERSION); return 0; }
        if (!strcmp(arg,"--api")) { sc_script_describe(); return 0; }
        if (!strcmp(arg,"--headless")) { opt.headless=true; continue; }
        if (!strcmp(arg,"--realtime")) { opt.realtime=true; continue; }
        if (!strcmp(arg,"--check")) { opt.check=true; continue; }
        if (!strcmp(arg,"--mute")) { opt.mute=true; continue; }
        if (!strcmp(arg,"--frames") || !strcmp(arg,"--seed") || !strcmp(arg,"--replay") || !strcmp(arg,"--snapshot") || !strcmp(arg,"--capture")) {
            if (++i>=argc) return diagnostic("arguments","option requires a value");
            const char *value=argv[i]; uint64_t n=0;
            if (!strcmp(arg,"--frames")) {
                if (!number(value,UINT64_C(1000000000),&n)) return diagnostic("arguments","frames must be an integer in 0..1000000000");
                opt.frames=n; opt.frames_set=true;
            } else if (!strcmp(arg,"--seed")) {
                if (!number(value,UINT32_MAX,&n) || !n) return diagnostic("arguments","seed must be an integer in 1..4294967295");
                opt.seed=(uint32_t)n;
            } else if (!strcmp(arg,"--replay")) opt.replay=value;
            else if (!strcmp(arg,"--snapshot")) opt.snapshot=value;
            else opt.capture=value;
            continue;
        }
        if (arg[0]=='-' || project_set) return diagnostic("arguments","unknown option or extra project path; use --help");
        opt.project=arg; project_set=true;
    }
    if (opt.realtime && (!opt.headless || opt.check))
        return diagnostic("arguments","realtime requires --headless and cannot be combined with --check");
    if (opt.capture && (opt.headless || opt.check || !opt.frames_set || !opt.frames))
        return diagnostic("arguments","capture requires a graphical run with --frames greater than zero");
    char error[SC_ERROR_MAX]={0};
    Runtime *runtime=runtime_open(opt.project,"main.lua",opt.seed,error,sizeof(error));
    if (!runtime) return diagnostic("scene",error);
    ReplayEvent *events=NULL; size_t event_count=0,event_index=0;
    if (!read_replay(opt.replay,&events,&event_count,error,sizeof(error))) { runtime_close(runtime); return diagnostic("replay",error); }
    int status=0; uint64_t frames=0; uint32_t held=0;
    if (opt.check) goto finished;
#ifndef SC_HAS_GRAPHICS
    if (!opt.headless) { status=diagnostic("backend","this build is headless only; use --headless or build with SHINY_GRAPHICS=ON"); goto cleanup; }
#else
    bool graphics_open=false,paused=false; double accumulator=0;
    uint32_t pending_pressed=0,pending_released=0,last_sample=0;
    if (!opt.headless) {
        if (!sc_render_open(&runtime->world,opt.project,!opt.mute,error,sizeof(error))) {
            status=diagnostic("backend",error); goto cleanup;
        }
        graphics_open=true;
        sc_render_audio(&runtime->world);
    }
#endif
    double deadline = opt.realtime ? monotonic_seconds() : 0;
    if (deadline < 0) { status=diagnostic("clock","cannot read monotonic clock"); goto run_finished; }
    while ((opt.headless || opt.frames_set) ? frames<opt.frames : true) {
        int steps=1;
#ifdef SC_HAS_GRAPHICS
        if (graphics_open) {
            if (sc_render_should_close()) break;
            if (sc_render_reload_requested()) {
                Runtime *candidate=runtime_open(opt.project,runtime->script.entry,opt.seed,error,sizeof(error));
                if (candidate) {
                    if (candidate->world.view_width!=runtime->world.view_width || candidate->world.view_height!=runtime->world.view_height) {
                        snprintf(error,sizeof(error),"view dimensions changed; restart the application to resize the render targets");
                        runtime_close(candidate);
                    } else {
                        runtime_close(runtime); runtime=candidate; error[0]='\0';
                        sc_render_reload_assets(); accumulator=0; paused=false;
                        sc_render_audio(&runtime->world);
                        pending_pressed=pending_released=last_sample=0;
                    }
                }
                if (error[0]) diagnostic("reload",error);
            }
            if (sc_render_pause_requested()) paused=!paused;
            bool single_step=sc_render_step_requested();
            if (!opt.replay) {
                uint32_t sample=sc_render_input();
                uint32_t queued=sc_render_pressed_input()&~last_sample;
                pending_pressed|=sample&~last_sample;
                pending_released|=last_sample&~sample;
                pending_pressed|=queued;
                pending_released|=queued&~sample;
                held=last_sample=sample;
            }
            if (opt.frames_set) steps=paused?(single_step?1:0):1;
            else {
                double delta=(double)sc_render_delta(); if (delta>0.25) delta=0.25;
                accumulator+=delta;
                steps=(int)(accumulator/(double)SC_DT); if (steps>8) steps=8;
                if (paused) { steps=single_step?1:0; accumulator=0; }
                else {
                    accumulator-=(double)steps*(double)SC_DT;
                    if (accumulator>=(double)SC_DT) accumulator=fmod(accumulator,(double)SC_DT);
                }
            }
        }
#endif
        for (int step=0;step<steps;step++) {
            runtime->world.tone_count=0;
            if (opt.replay) {
                while (event_index<event_count && events[event_index].frame==frames) held=events[event_index++].mask;
            }
            sc_input(&runtime->world,held);
#ifdef SC_HAS_GRAPHICS
            if (graphics_open && !opt.replay && step==0) {
                runtime->world.pressed|=pending_pressed;
                runtime->world.released|=pending_released;
                pending_pressed=pending_released=0;
            }
#endif
            if (!sc_script_update(&runtime->script)) { status=diagnostic("script",runtime->script.error); goto run_finished; }
            sc_step(&runtime->world); frames++;
#ifdef SC_HAS_GRAPHICS
            if (graphics_open) sc_render_audio(&runtime->world);
#endif
            if (runtime->script.pending_scene[0]) {
                Runtime *candidate=runtime_open(opt.project,runtime->script.pending_scene,opt.seed,error,sizeof(error));
                if (!candidate) { status=diagnostic("scene",error); goto run_finished; }
#ifdef SC_HAS_GRAPHICS
                if (graphics_open && (candidate->world.view_width!=runtime->world.view_width || candidate->world.view_height!=runtime->world.view_height)) {
                    runtime_close(candidate); status=diagnostic("scene","scene view dimensions must match the running window"); goto run_finished;
                }
                if (graphics_open) sc_render_reload_assets();
#endif
                runtime_close(runtime); runtime=candidate;
#ifdef SC_HAS_GRAPHICS
                if (graphics_open) sc_render_audio(&runtime->world);
#endif
                runtime->world.held=held; /* A held action must not press again in the next room. */
            }
            if (opt.headless && !sc_script_draw(&runtime->script,0)) { status=diagnostic("script",runtime->script.error); goto run_finished; }
            if (opt.realtime && !pace(&deadline)) { status=diagnostic("clock","cannot pace headless simulation"); goto run_finished; }
            if ((opt.headless || opt.frames_set) && frames>=opt.frames) break;
        }
#ifdef SC_HAS_GRAPHICS
        if (graphics_open) {
            float alpha=opt.frames_set?0:(float)(accumulator/(double)SC_DT);
            if (!sc_script_draw(&runtime->script,alpha)) { status=diagnostic("script",runtime->script.error); goto run_finished; }
            sc_render_frame(&runtime->world,alpha,error,paused);
            if (sc_render_error()[0]) { status=diagnostic("render",sc_render_error()); goto run_finished; }
        }
#endif
    }
run_finished:
#ifdef SC_HAS_GRAPHICS
    if (graphics_open) {
        if (!status && opt.capture && !sc_render_capture(opt.capture)) status=diagnostic("capture","cannot write capture PNG");
        sc_render_close();
    }
#endif
finished:
    if (!status) {
        if (opt.snapshot) {
            FILE *file=fopen(opt.snapshot,"w");
            if (!file) status=diagnostic("snapshot","cannot open snapshot output");
            else {
                snapshot(file,runtime,frames);
                if (fclose(file)) status=diagnostic("snapshot","failed writing snapshot output");
            }
        }
        if (!status) snapshot(stdout,runtime,frames);
    }
cleanup:
    free(events); runtime_close(runtime); return status;
}
