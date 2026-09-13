#include "shiny/core.h"
#include "shiny/script.h"
#include "shiny/physics.h"
#ifdef SC_HAS_GRAPHICS
#include "shiny/render.h"
#endif
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
struct Error { std::string code, message; };
template<class T> using Result = std::expected<T, Error>;
struct ReplayEvent { uint64_t frame{}; uint32_t mask{}; };
enum class Command { run, help, version, api };
struct Options {
    std::string project="examples/lantern", replay, snapshot, capture, save_directory;
    bool headless=false, check=false, check_all=false, mute=false, realtime=false;
    std::optional<uint64_t> frames;
    uint32_t seed=42;
    Command command=Command::run;
};
/* Both Lua's allocator and extraspace borrow addresses inside this owner.
 * Move the unique_ptr to a runtime, never the runtime itself. */
struct Runtime final {
    ScWorld world{};
    ScScript script{};
    Runtime()=default;
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    Runtime(Runtime&&)=delete;
    Runtime& operator=(Runtime&&)=delete;
};
using RuntimeOwner=std::unique_ptr<Runtime>;

std::string json_string(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (char byte:value) {
        auto ch=static_cast<unsigned char>(byte);
        if (ch=='"' || ch=='\\') out << '\\' << static_cast<char>(ch);
        else if (ch<32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(ch) << std::dec;
        else out << static_cast<char>(ch);
    }
    out << '"'; return out.str();
}
int diagnostic(const Error& error) {
    std::cerr << "{\"ok\":false,\"code\":" << json_string(error.code)
              << ",\"error\":" << json_string(error.message) << "}\n";
    return 1;
}
std::optional<uint64_t> number(std::string_view text,uint64_t maximum) {
    uint64_t value{};
    auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if (error!=std::errc{} || end!=text.data()+text.size() || value>maximum) return std::nullopt;
    return value;
}
Result<Options> parse_options(const char* exe_name,std::span<char*> arguments) {
    Options opt; bool project_set=false;
    for (size_t i=0;i<arguments.size();i++) {
        std::string_view arg=arguments[i];
        if (arg=="--help" || arg=="-h") { opt.command=Command::help; return opt; }
        if (arg=="--version") { opt.command=Command::version; return opt; }
        if (arg=="--api") { opt.command=Command::api; return opt; }
        if (arg=="--headless") { opt.headless=true; continue; }
        if (arg=="--check") { opt.check=true; continue; }
        if (arg=="--check-all") { opt.check=opt.check_all=true; continue; }
        if (arg=="--mute") { opt.mute=true; continue; }
        if (arg=="--realtime") { opt.realtime=true; continue; }
        if (arg=="--frames" || arg=="--seed" || arg=="--replay" || arg=="--snapshot" || arg=="--capture" || arg=="--save-dir") {
            if (++i==arguments.size()) return std::unexpected(Error{"arguments","option requires a value"});
            std::string_view value=arguments[i];
            if (arg=="--frames") {
                auto frames=number(value,1'000'000'000);
                if (!frames) return std::unexpected(Error{"arguments","frames must be an integer in 0..1000000000"});
                opt.frames=*frames;
            } else if (arg=="--seed") {
                auto seed=number(value,UINT32_MAX);
                if (!seed || *seed==0) return std::unexpected(Error{"arguments","seed must be an integer in 1..4294967295"});
                opt.seed=static_cast<uint32_t>(*seed);
            } else if (arg=="--replay") opt.replay=value;
            else if (arg=="--snapshot") opt.snapshot=value;
            else if (arg=="--save-dir") opt.save_directory=value;
            else opt.capture=value;
            continue;
        }
        if (arg.starts_with('-') || project_set) return std::unexpected(Error{"arguments","unknown option or extra project path; use --help"});
        opt.project=arg; project_set=true;
    }
    if (!project_set) {
        if (std::filesystem::exists("main.lua")) opt.project=".";
        else if (exe_name) {
            std::filesystem::path path(exe_name);
            if(path.has_parent_path()&&std::filesystem::exists(path.parent_path()/"main.lua")) opt.project=path.parent_path().string();
        }
    }
    if (opt.realtime && (!opt.headless || opt.check)) return std::unexpected(Error{"arguments","realtime requires --headless and cannot be combined with --check"});
    if (!opt.capture.empty() && (opt.headless || opt.check || !opt.frames || *opt.frames==0))
        return std::unexpected(Error{"arguments","capture requires a graphical run with --frames greater than zero"});
    return opt;
}
void usage() {
    std::cout << "ShinyCore " << SC_VERSION << " - C++23 native 2D engine\n"
        "Usage: shiny [project-directory] [options]\n"
        "  --check             Validate project entry, init, draw and assets\n"
        "  --check-all         Validate entry and all declared rooms\n"
        "  --save-dir DIR      Explicit save root; headless otherwise uses memory\n"
        "  --headless          Simulate without window, GPU or audio\n"
        "  --realtime          Pace headless simulation at 60 Hz wall time\n"
        "  --frames N          Stop after N ticks (headless default: 600)\n"
        "  --seed N            Simulation seed, 1..4294967295 (default: 42)\n"
        "  --replay FILE       Held input as zero-based frame mask lines\n"
        "  --snapshot FILE     Write final JSON state\n"
        "  --capture FILE.png  Capture final native output; requires --frames\n"
        "  --mute              Disable the audio device\n"
        "  --api / --version / --help\n"
        "Keys: arrows/WASD, Z/Space jump, X/E interact; F1 stats, F2 bounds,\n"
        "      F3 lighting, F5 reload, P pause, O single step, Esc quit.\n"
        "Replay masks: left=1 right=2 up=4 down=8 jump=16 action=32.\n";
}
Result<RuntimeOwner> open_runtime(const Options& options,const char* entry,const ScScript* previous=nullptr) {
    auto runtime=std::make_unique<Runtime>();
    runtime->script.checking=options.check;
    runtime->script.save_directory=options.save_directory.empty()&&!options.headless&&!options.check?sc_user_data_directory():options.save_directory;
    if(previous) {
        runtime->script.state=previous->has_pending_state?previous->pending_state:previous->state;
        runtime->script.memory_saves=previous->memory_saves;
    }
    sc_world_init(&runtime->world,options.seed);
    if(previous) {
        runtime->world.audio_generations=previous->world->audio_generations;
        for(size_t i=32;i<34;++i) if(previous->world->audio[i].alive&&previous->world->audio[i].persistent) runtime->world.audio[i]=previous->world->audio[i];
    }
    if (!sc_script_open(&runtime->script,&runtime->world,options.project.c_str(),entry) || !sc_script_draw(&runtime->script,0))
        return std::unexpected(Error{"scene",runtime->script.error});
    try { sc_physics_sync(&runtime->world); }
    catch(const std::exception& e) { return std::unexpected(Error{"physics",e.what()}); }
    for (const auto& entity:runtime->world.entities) {
        if (!entity.alive || !entity.sprite[0]) continue;
        std::string path=options.project+"/"+entity.sprite;
        if (!sc_script_validate_path(entity.sprite) || !std::ifstream(path,std::ios::binary))
            return std::unexpected(Error{"scene","sprite asset unavailable: "+path});
    }
#ifdef SC_HAS_GRAPHICS
    char error[SC_ERROR_MAX]{};
    if (!sc_render_validate_assets(&runtime->world,options.project.c_str(),error,sizeof(error)))
        return std::unexpected(Error{"scene",error});
#endif
    return runtime;
}
Result<std::vector<ReplayEvent>> read_replay(const std::string& path) {
    std::vector<ReplayEvent> events;
    if (path.empty()) return events;
    std::ifstream file(path);
    if (!file) return std::unexpected(Error{"replay","cannot open replay: "+path});
    std::string line; size_t line_number=0;
    while (std::getline(file,line)) {
        ++line_number;
        bool valid=line.size()<255;
        if (auto comment=line.find('#');comment!=std::string::npos) line.resize(comment);
        std::istringstream tokens(line); std::string first,second,extra;
        if (valid && !(tokens>>first)) continue;
        if (!(tokens>>second) || (tokens>>extra)) valid=false;
        auto frame=number(first,1'000'000'000),mask=number(second,63);
        if (!valid || !frame || !mask || (!events.empty() && *frame<=events.back().frame) || events.size()>=1'000'000)
            return std::unexpected(Error{"replay",path+":"+std::to_string(line_number)+": expected strictly increasing nonnegative frame and input mask 0..63"});
        events.push_back({*frame,static_cast<uint32_t>(*mask)});
    }
    if (file.bad()) return std::unexpected(Error{"replay","cannot read replay: "+path});
    return events;
}
std::string snapshot(const Runtime& runtime,uint64_t frames) {
    const auto& world=runtime.world;
    std::ostringstream out;
    out << "{\"ok\":true,\"version\":" << json_string(SC_VERSION) << ",\"frames\":" << frames
        << ",\"tick\":" << world.tick << ",\"scene\":" << json_string(runtime.script.entry)
        << ",\"rng\":" << world.rng << ",\"hash\":\"" << std::hex << std::setw(16) << std::setfill('0') << sc_state_hash(&world) << std::dec
        << "\",\"message\":" << json_string(world.message) << ",\"camera\":{\"x\":" << world.camera_x << ",\"y\":" << world.camera_y << "},\"entities\":[";
    bool first=true;
    for (const auto& entity:world.entities) {
        if (!entity.alive) continue;
        if (!first) out << ',';
        first=false;
        out << "{\"id\":" << entity.id << ",\"tag\":" << json_string(entity.tag) << std::setprecision(9)
            << ",\"x\":" << entity.x << ",\"y\":" << entity.y << ",\"vx\":" << entity.vx << ",\"vy\":" << entity.vy
            << ",\"grounded\":" << (entity.grounded?"true":"false")
            << ",\"angle\":" << entity.angle << ",\"support\":" << entity.support
            << ",\"normal_x\":" << entity.normal_x << ",\"normal_y\":" << entity.normal_y << '}';
    }
    out << "],\"contacts\":[";
    for(int i=0;i<world.contact_count;++i) {
        const auto& c=world.contacts[static_cast<size_t>(i)];
        if(i) out<<',';
        out<<"{\"a\":"<<c.a<<",\"b\":"<<c.b<<",\"nx\":"<<c.nx<<",\"ny\":"<<c.ny<<",\"sensor\":"<<(c.sensor?"true":"false")<<",\"phase\":"<<json_string(c.phase==1?"begin":c.phase==2?"end":"contact")<<'}';
    }
    out << "],\"audio\":["; first=true;
    for(const auto& voice:world.audio) if(voice.alive) {
        if(!first) out<<',';
        first=false;
        out<<"{\"id\":"<<voice.id<<",\"path\":"<<json_string(voice.path)<<",\"volume\":"<<voice.volume<<",\"position\":"<<voice.position<<",\"paused\":"<<(voice.paused?"true":"false")<<'}';
    }
    out << "],\"state\":" << sc_json_write(runtime.script.state)
        << ",\"state_hash\":\"" << std::hex << sc_data_hash(runtime.script.state) << "\"}\n"; return out.str();
}
#ifdef SC_HAS_GRAPHICS
class GraphicsSession final {
public:
    bool active=false;
    GraphicsSession()=default;
    GraphicsSession(const GraphicsSession&)=delete;
    GraphicsSession& operator=(const GraphicsSession&)=delete;
    ~GraphicsSession() { if (active) sc_render_close(); }
};
#endif
class Pacer final {
    using Clock=std::chrono::steady_clock;
    Clock::time_point deadline=Clock::now();
public:
    void wait() {
        deadline+=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/60.0));
        auto now=Clock::now();
        if (now>deadline+std::chrono::milliseconds(250)) deadline=now;
        else std::this_thread::sleep_until(deadline);
    }
};
Result<void> run(const Options& options) {
    auto opened=open_runtime(options,"__project_entry__.lua");
    if (!opened) return std::unexpected(opened.error());
    auto runtime=std::move(*opened);
    if(options.check_all) {
        if(const auto* rooms=runtime->script.project.get("rooms")) {
            auto* entries=std::get_if<ScValue::Array>(&rooms->data);
            if(!entries) return std::unexpected(Error{"project","rooms must be an array"});
            for(const auto& room:*entries) {
                auto path=room.text(); auto candidate=open_runtime(options,path.c_str());
                if(!candidate) return std::unexpected(candidate.error());
            }
        }
    }
    auto replay=read_replay(options.replay);
    if (!replay) return std::unexpected(replay.error());
    size_t event_index=0; uint64_t frames=0; uint32_t held=0;
    bool bounded=options.headless || options.frames.has_value();
    uint64_t limit=options.frames.value_or(600);
    std::string reload_error;
#ifdef SC_HAS_GRAPHICS
    GraphicsSession graphics;
    bool paused=false;
    double accumulator=0;
    uint32_t pending_pressed=0,pending_released=0,last_sample=0;
    if (!options.headless && !options.check) {
        char error[SC_ERROR_MAX]{};
        if (!sc_render_open(&runtime->world,options.project.c_str(),!options.mute,error,sizeof(error)))
            return std::unexpected(Error{"backend",error});
        graphics.active=true;
        if(!sc_render_prepare_assets(&runtime->world,error,sizeof error)) return std::unexpected(Error{"resources",error});
        sc_render_audio(&runtime->world);
    }
#else
    if (!options.headless && !options.check)
        return std::unexpected(Error{"backend","this build is headless only; use --headless or build with SHINY_GRAPHICS=ON"});
#endif
    Pacer pacer;
    while (!options.check && (!bounded || frames<limit)) {
        int steps=1;
#ifdef SC_HAS_GRAPHICS
        if (graphics.active) {
            if (sc_render_should_close()) break;
            if (sc_render_reload_requested()) {
                auto candidate=open_runtime(options,runtime->script.entry,&runtime->script);
                if (candidate && ((*candidate)->world.view_width!=runtime->world.view_width || (*candidate)->world.view_height!=runtime->world.view_height))
                    candidate=std::unexpected(Error{"reload","view dimensions changed; restart the application to resize the render targets"});
                if(candidate) {
                    char error[SC_ERROR_MAX]{};
                    if(!sc_render_prepare_assets(&(*candidate)->world,error,sizeof error)) candidate=std::unexpected(Error{"reload",error});
                }
                if (!candidate) {
                    reload_error=candidate.error().message;
                    diagnostic({"reload",reload_error});
                } else {
                    runtime=std::move(*candidate); reload_error.clear();
                    sc_render_audio(&runtime->world);
                    accumulator=0; paused=false; pending_pressed=pending_released=last_sample=0;
                }
            }
            if (sc_render_pause_requested()) paused=!paused;
            bool single_step=sc_render_step_requested();
            if (options.replay.empty()) {
                uint32_t sample=sc_render_input(),queued=sc_render_pressed_input()&~last_sample;
                pending_pressed|=(sample&~last_sample)|queued;
                pending_released|=(last_sample&~sample)|(queued&~sample);
                held=last_sample=sample;
            }
            if (options.frames) steps=paused?(single_step?1:0):1;
            else {
                accumulator+=std::clamp(static_cast<double>(sc_render_delta()),0.0,0.25);
                steps=std::min(8,static_cast<int>(accumulator/SC_DT));
                if (paused) { steps=single_step?1:0; accumulator=0; }
                else accumulator=std::fmod(accumulator-static_cast<double>(steps)*SC_DT,SC_DT);
            }
        }
#endif
        for (int step=0;step<steps;step++) {
            runtime->world.tone_count=0;
            if (!options.replay.empty())
                while (event_index<replay->size() && (*replay)[event_index].frame==frames) held=(*replay)[event_index++].mask;
            sc_input(&runtime->world,held);
#ifdef SC_HAS_GRAPHICS
            if (graphics.active && options.replay.empty() && step==0) {
                runtime->world.pressed|=pending_pressed; runtime->world.released|=pending_released;
                pending_pressed=pending_released=0;
            }
#endif
            if (!sc_script_update(&runtime->script)) return std::unexpected(Error{"script",runtime->script.error});
            sc_step(&runtime->world); ++frames;
#ifdef SC_HAS_GRAPHICS
            if (graphics.active) sc_render_audio(&runtime->world);
#endif
            if (runtime->script.pending_scene[0]) {
                auto candidate=open_runtime(options,runtime->script.pending_scene,&runtime->script);
                if (!candidate) {
                    runtime->script.pending_scene[0]=0; runtime->script.has_pending_state=false;
                    if(options.headless) return std::unexpected(candidate.error());
                    reload_error=candidate.error().message; diagnostic(candidate.error());
                    continue;
                }
#ifdef SC_HAS_GRAPHICS
                if (graphics.active && ((*candidate)->world.view_width!=runtime->world.view_width || (*candidate)->world.view_height!=runtime->world.view_height)) {
                    runtime->script.pending_scene[0]=0; runtime->script.has_pending_state=false;
                    reload_error="scene view dimensions must match the running window"; diagnostic({"scene",reload_error}); continue;
                }
                if(graphics.active) {
                    char error[SC_ERROR_MAX]{};
                    if(!sc_render_prepare_assets(&(*candidate)->world,error,sizeof error)) {
                        runtime->script.pending_scene[0]=0; runtime->script.has_pending_state=false;
                        reload_error=error; diagnostic({"resources",error}); continue;
                    }
                }
#endif
                runtime=std::move(*candidate); runtime->world.held=held;
#ifdef SC_HAS_GRAPHICS
                if (graphics.active) sc_render_audio(&runtime->world);
#endif
            }
            if (options.headless && !sc_script_draw(&runtime->script,0)) return std::unexpected(Error{"script",runtime->script.error});
            if (options.realtime) pacer.wait();
            if (bounded && frames>=limit) break;
        }
#ifdef SC_HAS_GRAPHICS
        if (graphics.active) {
            float alpha=options.frames?0:static_cast<float>(std::clamp(accumulator/SC_DT,0.0,1.0));
            if (!sc_script_draw(&runtime->script,alpha)) return std::unexpected(Error{"script",runtime->script.error});
            sc_render_frame(&runtime->world,alpha,reload_error.c_str(),paused);
            if (sc_render_error()[0]) return std::unexpected(Error{"render",sc_render_error()});
        }
#endif
    }
#ifdef SC_HAS_GRAPHICS
    if (graphics.active && !options.capture.empty() && !sc_render_capture(options.capture.c_str()))
        return std::unexpected(Error{"capture","cannot write capture PNG"});
#endif
    auto output=snapshot(*runtime,frames);
    if (!options.snapshot.empty()) {
        std::ofstream file(options.snapshot);
        if (!file) return std::unexpected(Error{"snapshot","cannot open snapshot output"});
        file << output; file.close();
        if (!file) return std::unexpected(Error{"snapshot","failed writing snapshot output"});
    }
    std::cout << output;
    return {};
}
} // namespace

int main(int argc,char** argv) {
    try {
        auto options=parse_options(argc>0?argv[0]:nullptr,std::span(argv+1,static_cast<size_t>(argc-1)));
        if (!options) return diagnostic(options.error());
        switch (options->command) {
            case Command::help: usage(); return 0;
            case Command::version: std::cout << SC_VERSION << '\n'; return 0;
            case Command::api: sc_script_describe(); return 0;
            case Command::run: break;
        }
        auto result=run(*options);
        return result?0:diagnostic(result.error());
    } catch (const std::bad_alloc&) {
        std::fputs("{\"ok\":false,\"code\":\"memory\",\"error\":\"out of memory\"}\n",stderr); return 1;
    } catch (const std::exception& exception) {
        return diagnostic({"host",exception.what()});
    }
}
