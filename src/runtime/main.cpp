#include "shiny/core.h"
#include "shiny/input_replay.h"
#include "shiny/script.h"
#include "shiny/save_io.h"
#include <cstring>
#include "shiny/physics.h"
#include "shiny/projectiles.h"
#include "shiny/settings.h"
#include "profile.h"
#ifdef SC_HAS_DEVTOOLS
#include "../dev/debug_stdio.h"
#endif
#ifdef SC_HAS_NETWORK
#include "shiny/net.h"
#endif
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
struct ReplayEvent { uint64_t frame{}; uint32_t mask{}; bool devices{}; ScDeviceInput input{}; };
enum class Command { run, help, version, api };
struct Options {
    std::string project="examples/lantern", replay, snapshot, capture, save_directory, record, trace, profile;
    bool headless=false, check=false, check_all=false, mute=false, realtime=false, debug_keys=false, debug_stdio=false, debug_load=false, capture_hidden=false;
    std::optional<uint64_t> frames;
    uint32_t seed=42;
    Command command=Command::run;
};
/* Both Lua's allocator and extraspace borrow addresses inside this owner.
 * Move the unique_ptr to a runtime, never the runtime itself. */
struct Runtime final {
    ScWorld world{};
    ScScript script{};
    bool prepared{};
#ifdef SC_HAS_STREAMING
    std::uint64_t index_ticket{};
#endif
    Runtime()=default;
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    Runtime(Runtime&&)=delete;
    Runtime& operator=(Runtime&&)=delete;
#ifdef SC_HAS_STREAMING
    ~Runtime() { if(index_ticket&&script.content_loader) script.content_loader->cancel(index_ticket); }
#endif
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
        if (arg=="--capture-hidden") {
#ifdef SC_HAS_GRAPHICS
            opt.capture_hidden=true; continue;
#else
            return std::unexpected(Error{"arguments","capture-hidden requires SHINY_GRAPHICS=ON"});
#endif
        }
        if (arg=="--debug-stdio") {
#ifdef SC_HAS_DEVTOOLS
            opt.debug_stdio=true; continue;
#else
            return std::unexpected(Error{"arguments","debug-stdio requires SHINY_DEVTOOLS=ON"});
#endif
        }
        if(arg=="--debug-load") { opt.debug_load=true; continue; }
        if (arg=="--debug-keys") { opt.debug_keys=true; continue; }
        if (arg=="--realtime") { opt.realtime=true; continue; }
        if (arg=="--frames" || arg=="--seed" || arg=="--replay" || arg=="--snapshot" || arg=="--capture" || arg=="--save-dir" || arg=="--record" || arg=="--trace" || arg=="--profile") {
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
            else if (arg=="--record") opt.record=value;
            else if (arg=="--trace") opt.trace=value;
            else if (arg=="--profile") opt.profile=value;
            else opt.capture=value;
            continue;
        }
        if (arg.starts_with('-') || project_set) return std::unexpected(Error{"arguments","unknown option or extra project path; use --help"});
        opt.project=arg; project_set=true;
    }
    if (!project_set) {
        // A packaged executable owns its adjacent game directory, regardless of the caller's cwd.
        std::error_code path_error;
        if (exe_name) {
            auto directory=std::filesystem::absolute(std::filesystem::path(exe_name),path_error).parent_path();
            if (!path_error && std::filesystem::exists(directory/"game"/"project.lua",path_error)
                && std::filesystem::exists(directory/"game"/"main.lua",path_error))
                opt.project=(directory/"game").string();
            else if (!path_error && std::filesystem::exists("main.lua",path_error)) opt.project=".";
            else if (!path_error && std::filesystem::exists(directory/"main.lua",path_error)) opt.project=directory.string();
        } else if (std::filesystem::exists("main.lua",path_error)) opt.project=".";
    }
    if(opt.debug_stdio&&opt.check) return std::unexpected(Error{"arguments","debug-stdio cannot be combined with check"});
    if(opt.debug_load&&!opt.debug_stdio) return std::unexpected(Error{"arguments","debug-load requires --debug-stdio"});
    if (opt.realtime && (!opt.headless || opt.check)) return std::unexpected(Error{"arguments","realtime requires --headless and cannot be combined with --check"});
    if (!opt.capture.empty() && (opt.headless || opt.check || !opt.frames || *opt.frames==0))
        return std::unexpected(Error{"arguments","capture requires a graphical run with --frames greater than zero"});
    if(!opt.record.empty()&&!opt.replay.empty()) return std::unexpected(Error{"arguments","record and replay are mutually exclusive"});
    if(opt.capture_hidden && opt.capture.empty()) return std::unexpected(Error{"arguments","capture-hidden requires --capture and --frames"});
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
        "  --replay FILE       Legacy frame/mask or version 2/3 device JSON Lines\n"
        "  --record FILE       Record fixed-tick device input as JSON Lines\n"
        "  --trace FILE        Write per-tick snapshots (opt in)\n"
        "  --profile FILE      Write CPU phases, asynchronous GPU timing and capacities\n"
        "  --snapshot FILE     Write final JSON state\n"
        "  --capture FILE.png  Capture final native output; requires --frames\n"
        "  --capture-hidden    Keep the bounded capture window hidden and unfocused\n"
        "  --mute              Disable the audio device\n"
        "  --api / --version / --help\n"
        "  --debug-stdio       Paused JSON Lines debugger; requires devtools build\n"
        "  --debug-load        Stop at first Lua line, including project/load/init\n"
        "  --debug-keys        Enable host shortcuts (off by default)\n"
        "Game: arrows/WASD, Z/Space jump, X/E interact. Debug keys: F1 stats, F2 bounds,\n"
        "      F3 lighting, F5 reload, P pause, O single step, Esc quit.\n"
        "Replay masks: left=1 right=2 up=4 down=8 jump=16 action=32.\n";
}
Result<RuntimeOwner> open_runtime(const Options& options,const char* entry,const ScScript* previous=nullptr,ScNetSessions* network=nullptr,ScSettingsService* settings=nullptr,[[maybe_unused]] ScContentLoader* loader=nullptr,ScSaveIo* saves=nullptr,[[maybe_unused]] ScImageCache* images=nullptr
#ifdef SC_HAS_DEVTOOLS
    ,ScDebugStdio* debugger=nullptr,std::uint64_t frame=0
#endif
) {
    auto runtime=std::make_unique<Runtime>();
    runtime->script.checking=options.check;
    runtime->script.network_sessions=previous?previous->network_sessions:network;
    runtime->script.settings=previous?previous->settings:settings;
    runtime->script.save_io=previous?previous->save_io:saves;
#ifdef SC_HAS_STREAMING
    runtime->script.content_loader=previous?previous->content_loader:loader;
    runtime->script.image_cache=previous?previous->image_cache:images;
#endif
    runtime->script.candidate=previous!=nullptr;
    runtime->script.save_directory=options.save_directory.empty()&&!options.headless&&!options.check?sc_user_data_directory():options.save_directory;
    if(previous) {
        runtime->script.state=previous->has_pending_state?previous->pending_state:previous->state;
        runtime->script.memory_saves=previous->memory_saves;
        runtime->script.save_snapshot=previous->save_snapshot;
        runtime->script.save_snapshot_slot=previous->save_snapshot_slot;
    }
    sc_world_init(&runtime->world,options.seed);
    if (previous) {
        if (previous->world->epoch >= 0xfffff) return std::unexpected(Error{"capacity","room generation exhausted"});
        runtime->world.epoch=previous->world->epoch+1;
    }
    if(previous) {
        runtime->world.input=previous->world->input; runtime->world.input.clear_edges();
        runtime->world.held=previous->world->held;
        runtime->script.audio_draft=previous->audio->room_candidate();
    }
#ifdef SC_HAS_DEVTOOLS
    if(debugger) debugger->attach(runtime->script,frame);
#endif
    if (!sc_script_load(&runtime->script,&runtime->world,options.project.c_str(),entry))
        return std::unexpected(Error{"scene",runtime->script.error});
#ifdef SC_HAS_STREAMING
    if(const auto* indexes=runtime->script.project.get("stream_indexes"))
        if(const auto* index=indexes->get(runtime->script.entry)) {
            runtime->script.preloaded_index_path=index->text();
            try {
                runtime->index_ticket=runtime->script.content_loader->submit_index(
                    options.project+"/"+runtime->script.preloaded_index_path);
            } catch(const std::exception& error) {
                return std::unexpected(Error{"resources","stream index "+runtime->script.preloaded_index_path+": "+error.what()});
            }
        }
    if(runtime->index_ticket) return runtime;
#endif
    if(!sc_script_initialize(&runtime->script)) return std::unexpected(Error{"scene",runtime->script.error});
    return runtime;
}
// Poll once per host frame. Only the candidate changes until every preparation succeeds.
Result<bool> prepare_runtime(Runtime& runtime,const Options& options,bool graphics=false) {
    if(runtime.prepared) return true;
#ifdef SC_HAS_STREAMING
    if(runtime.index_ticket) {
        auto result=runtime.script.content_loader->take_index(runtime.index_ticket);
        if(!result) return false;
        runtime.index_ticket=0;
        if(!*result) return std::unexpected(Error{"resources","stream index "+runtime.script.preloaded_index_path+": "+result->error()});
        runtime.script.preloaded_index=std::move(**result);
        if(!sc_script_initialize(&runtime.script)) return std::unexpected(Error{"scene",runtime.script.error});
    }
    if(runtime.script.initial_images) {
        ScResult<bool> ready;
#ifdef SC_HAS_GRAPHICS
        if(graphics) ready=sc_render_prepare_images(*runtime.script.images);
        else
#endif
        ready=runtime.script.images->advance();
        if(!ready) return std::unexpected(Error{"resources",ready.error()});
        if(!*ready) return false;
        auto committed=runtime.script.images->commit(runtime.script.initial_images);
        if(!committed) return std::unexpected(Error{"resources",committed.error()});
        runtime.script.initial_images=0;
    }
#endif
    if(!sc_script_draw(&runtime.script,1)) {
        if(runtime.world.exit_requested) return true;
        return std::unexpected(Error{"scene",runtime.script.error});
    }
    try { sc_physics_sync(&runtime.world); }
    catch(const std::exception& e) { return std::unexpected(Error{"physics",e.what()}); }
    for (const auto& entity:runtime.world.entities) {
        if (!entity.alive || !entity.sprite[0]) continue;
        std::string path=options.project+"/"+entity.sprite;
        if (!sc_script_validate_path(entity.sprite) || !std::ifstream(path,std::ios::binary))
            return std::unexpected(Error{"scene","sprite asset unavailable: "+path});
    }
#ifdef SC_HAS_STREAMING
    if(options.check) for(const auto& resource:runtime.world.resources) if(resource.streamed) {
        const auto ticket=runtime.script.content_loader->submit_image({options.project+"/"+resource.path,resource.image_width,resource.image_height});
        for(;;) {
            auto result=runtime.script.content_loader->take_image(ticket);
            if(result) {
                if(!*result) return std::unexpected(Error{"resources","resources."+resource.name+": "+result->error()});
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // CLI validation, never the running game loop.
        }
    }
#endif
#ifdef SC_HAS_GRAPHICS
    char error[SC_ERROR_MAX]{};
    if (!sc_render_validate_assets(&runtime.world,options.project.c_str(),error,sizeof(error)))
        return std::unexpected(Error{"scene",error});
#endif
#ifdef SC_HAS_GRAPHICS
    if(graphics&&!sc_render_prepare_assets(&runtime.world,error,sizeof error,&runtime.script))
        return std::unexpected(Error{"resources",error});
#else
    (void)graphics;
#endif
    runtime.prepared=true;
    return true;
}
// CLI/startup only. Running room changes use prepare_runtime from the host loop.
Result<void> prepare_initial(Runtime& runtime,const Options& options,bool graphics=false) {
    for(;;) {
        auto ready=prepare_runtime(runtime,options,graphics);
        if(!ready) return std::unexpected(ready.error());
        if(*ready) return {};
#ifdef SC_HAS_GRAPHICS
        if(graphics) {
            if(sc_render_should_close()) { runtime.world.exit_requested=true; return {}; }
            sc_render_loading();
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
Result<std::vector<ReplayEvent>> read_replay(const std::string& path) {
    std::vector<ReplayEvent> events;
    if (path.empty()) return events;
    std::ifstream file(path);
    if (!file) return std::unexpected(Error{"replay","cannot open replay: "+path});
    std::string line; size_t line_number=0; bool format_seen=false,devices=false;
    while (std::getline(file,line)) {
        ++line_number;
        if(line.find_first_not_of(" \t\r")==std::string::npos) continue;
        if(!format_seen && line[line.find_first_not_of(" \t\r")]=='#') continue;
        if(!format_seen) {
            format_seen=true;
            if(line[line.find_first_not_of(" \t\r")]=='{') {
                auto header=sc_json_read(line,65536);
                if(!header || !header->get("version") || (header->get("version")->number()!=2&&header->get("version")->number()!=3) ||
                   !std::holds_alternative<ScValue::Object>(header->data) || std::get<ScValue::Object>(header->data).size()!=1)
                    return std::unexpected(Error{"replay",path+":"+std::to_string(line_number)+": expected {\"version\":2} header"});
                devices=true; continue;
            }
        }
        if(devices) {
            auto value=sc_json_read(line,65536);
            if(!value) return std::unexpected(Error{"replay",path+":"+std::to_string(line_number)+": "+value.error()});
            auto event=sc_device_replay_event(*value);
            if(!event) return std::unexpected(Error{"replay",path+":"+std::to_string(line_number)+": "+event.error()});
            if((!events.empty()&&event->frame<=events.back().frame)||events.size()>=1'000'000)
                return std::unexpected(Error{"replay",path+":"+std::to_string(line_number)+": expected increasing frames and at most 1000000 events"});
            events.push_back({event->frame,0,true,event->input}); continue;
        }
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
        out << "{\"id\":" << entity.id << ",\"tag\":" << json_string(entity.tag) << ",\"persistent_id\":" << json_string(entity.persistent_id) << std::setprecision(9)
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
    for(const auto& voice:runtime.script.audio->audio) if(voice.alive) {
        if(!first) out<<',';
        first=false;
        out<<"{\"id\":"<<voice.id<<",\"path\":"<<json_string(voice.path)<<",\"volume\":"<<voice.volume<<",\"position\":"<<voice.position<<",\"paused\":"<<(voice.paused?"true":"false")<<'}';
    }
    out << "],\"input\":" << sc_json_write(sc_input_snapshot(world.input)) << ",\"state\":" << sc_json_write(runtime.script.state)
        << ",\"watches\":" << sc_json_write(ScValue{runtime.script.watches})
        << ",\"settings\":" << sc_json_write(runtime.script.settings?sc_settings_value(runtime.script.settings->current):ScValue{ScValue::Object{}})
        << ",\"projectiles\":" << (world.projectiles?world.projectiles->count:0)
        << ",\"projectile_hits\":" << (world.projectiles?world.projectiles->total_hits:0)
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
    ScAudioState application_audio;
    ScSettingsService settings;
    ScSaveIo save_io;
#ifdef SC_HAS_STREAMING
    ScContentLoader content_loader;
    auto* content_service=&content_loader;
    ScImageCache image_cache(content_loader); auto* image_service=&image_cache;
#else
    ScContentLoader* content_service=nullptr;
    ScImageCache* image_service=nullptr;
#endif
#ifdef SC_HAS_NETWORK
    ScNetSessions network;
    auto* network_service=&network;
#else
    ScNetSessions* network_service=nullptr;
#endif
#ifdef SC_HAS_DEVTOOLS
    std::optional<ScDebugStdio> debugger;
    if(options.debug_load) {
        debugger.emplace(true);
        debugger->pump=[&](ScScript&) {
#ifdef SC_HAS_NETWORK
            network.service();
#endif
        };
    }
#endif
    auto opened=open_runtime(options,"__project_entry__.lua",nullptr,network_service,&settings,content_service,&save_io,image_service
#ifdef SC_HAS_DEVTOOLS
        ,debugger?&*debugger:nullptr
#endif
    );
#ifdef SC_HAS_DEVTOOLS
    if(debugger&&debugger->exiting()) { debugger->finish(0); return {}; }
#endif
    if (!opened) return std::unexpected(opened.error());
    auto runtime=std::move(*opened);
    auto commit_audio=[&](Runtime& room) {
        application_audio=*room.script.audio;
        room.script.audio=&application_audio;
        room.script.candidate=false;
    };
    if(options.headless||options.check) {
        auto ready=prepare_initial(*runtime,options);
        if(!ready) return ready;
        commit_audio(*runtime);
    }
    if(options.check_all) {
        if(const auto* rooms=runtime->script.project.get("rooms")) {
            auto* entries=std::get_if<ScValue::Array>(&rooms->data);
            if(!entries) return std::unexpected(Error{"project","rooms must be an array"});
            for(const auto& room:*entries) {
                auto path=room.text(); auto candidate=open_runtime(options,path.c_str(),nullptr,nullptr,&settings,content_service,&save_io,image_service);
                if(!candidate) return std::unexpected(candidate.error());
                auto ready=prepare_initial(**candidate,options);
                if(!ready) return ready;
            }
        }
    }
    std::ofstream record_file,trace_file,profile_file;
    auto output_file=[](std::ofstream& out,const std::string& path) {
        if(path.empty()) return;
        if(std::filesystem::exists(path)) throw std::runtime_error("diagnostic output already exists: "+path);
        out.open(path,std::ios::binary); if(!out) throw std::runtime_error("cannot create diagnostic output: "+path);
    };
    output_file(record_file,options.record); output_file(trace_file,options.trace); output_file(profile_file,options.profile);
    if(record_file.is_open()) record_file << "{\"version\":3}\n";
    auto replay=read_replay(options.replay);
    if (!replay) return std::unexpected(replay.error());
    size_t event_index=0; uint64_t frames=0; uint32_t held=0;
    ScInputBuffer input_buffer; bool device_replay=false;
    bool bounded=options.headless || options.frames.has_value();
    uint64_t limit=options.frames.value_or(600);
    std::string reload_error;
#ifdef SC_HAS_GRAPHICS
    GraphicsSession graphics;
    bool captured=false;
    bool paused=false;
    double accumulator=0;
    if (!options.headless && !options.check) {
        char error[SC_ERROR_MAX]{};
        if (!sc_render_open(&runtime->world,options.project.c_str(),!options.mute,error,sizeof(error),options.capture_hidden))
            return std::unexpected(Error{"backend",error});
        graphics.active=true;
        settings.apply_native=sc_render_settings;
        auto configured=sc_render_settings(settings.current);
        if(!configured) return std::unexpected(Error{"settings",configured.error()});
        sc_render_debug_keys(options.debug_keys);
#ifdef SC_HAS_DEVTOOLS
        if(debugger) debugger->pump=[&](ScScript& stopped) {
#ifdef SC_HAS_NETWORK
            network.service();
#endif
            if(sc_render_should_close()) stopped.world->exit_requested=true;
            sc_render_loading();
        };
#endif
        auto ready=prepare_initial(*runtime,options,true);
        if(!ready) return ready;
        commit_audio(*runtime);
        sc_render_audio(&runtime->script);
    }
#else
    if (!options.headless && !options.check)
        return std::unexpected(Error{"backend","this build is headless only; use --headless or build with SHINY_GRAPHICS=ON"});
#endif
    const bool profiling=profile_file.is_open();
    bool profile_graphics=false,gpu_supported=false;
    const char* profile_renderer=nullptr;
#ifdef SC_HAS_GRAPHICS
    profile_graphics=graphics.active;
    if(profiling && graphics.active) { gpu_supported=sc_render_profile_enable(); profile_renderer=sc_render_device(); }
#endif
    std::optional<ScProfileWriter> profiler;
    if(profiling) profiler.emplace(profile_file,profile_graphics,gpu_supported,trace_file.is_open()||record_file.is_open(),profile_renderer);
    Pacer pacer;
    uint64_t display_frame=0;
    bool io_waiting=false;
    std::string io_error;
    [[maybe_unused]] bool io_saving=false,io_reading=false,io_deleting=false,io_images=false;
#ifdef SC_HAS_DEVTOOLS
    if(options.debug_stdio) {
        if(!debugger) debugger.emplace();
#ifdef SC_HAS_GRAPHICS
        if(graphics.active) debugger->panel=sc_render_inspector;
#endif
        debugger->pump=[&](ScScript& stopped) {
            (void)stopped;
#ifdef SC_HAS_NETWORK
            network.service();
#endif
#ifdef SC_HAS_GRAPHICS
            if(graphics.active) {
                if(sc_render_should_close()) stopped.world->exit_requested=true;
                if(sc_render_pause_requested()) debugger->local_resume();
                sc_render_frame(&runtime->world,1,{},true,nullptr,&runtime->script);
            }
#endif
        };
    }
#endif
    RuntimeOwner candidate;
    bool candidate_tick=false;
    auto begin_candidate=[&](const char* entry) -> Result<void> {
        auto opened_room=open_runtime(options,entry,&runtime->script,nullptr,nullptr,nullptr,nullptr,nullptr
#ifdef SC_HAS_DEVTOOLS
            ,debugger?&*debugger:nullptr,frames
#endif
        );
        if(!opened_room) return std::unexpected(opened_room.error());
#ifdef SC_HAS_GRAPHICS
        if(graphics.active&&((*opened_room)->world.view_width!=runtime->world.view_width||(*opened_room)->world.view_height!=runtime->world.view_height))
            return std::unexpected(Error{"scene","scene view dimensions must match the running window"});
#endif
        candidate=std::move(*opened_room);
        return {};
    };
    auto reject_candidate=[&](const Error& error) -> Result<bool> {
        candidate.reset();
        runtime->script.pending_scene[0]=0; runtime->script.has_pending_state=false;
#ifdef SC_HAS_DEVTOOLS
        if(debugger&&debugger->exiting()) { runtime->world.exit_requested=true; return true; }
#endif
        if(options.headless) return std::unexpected(error);
        reload_error=error.message; diagnostic(error); return true;
    };
    auto advance_candidate=[&]() -> Result<bool> {
        bool native=false;
#ifdef SC_HAS_GRAPHICS
        native=graphics.active;
#endif
        auto ready=prepare_runtime(*candidate,options,native);
        if(!ready) return reject_candidate(ready.error());
        if(!*ready) return false;
#ifdef SC_HAS_DEVTOOLS
        if(debugger&&debugger->exiting()) { candidate.reset(); runtime->world.exit_requested=true; return true; }
#endif
        commit_audio(*candidate); runtime=std::move(candidate); runtime->world.held=held;
        reload_error.clear(); io_waiting=false;
#ifdef SC_HAS_GRAPHICS
        if(graphics.active) sc_render_audio(&runtime->script);
        accumulator=0; paused=false;
#endif
        return true;
    };
    auto finish_tick=[&](ScFrameProfile& sample) -> Result<void> {
#ifdef SC_HAS_DEVTOOLS
        if(debugger&&debugger->exiting()) return {};
#endif
        if(options.headless) {
            ScProfileScope timing(profiling?&sample.script_draw_ms:nullptr);
            if(!sc_script_draw(&runtime->script,1)) return std::unexpected(Error{"script",runtime->script.error});
        }
        if(trace_file.is_open()) {
            ScProfileScope timing(profiling?&sample.diagnostic_ms:nullptr);
            trace_file << snapshot(*runtime,frames);
            if(!trace_file) return std::unexpected(Error{"trace","failed writing trace"});
        }
        if(options.realtime) { ScProfileScope timing(profiling?&sample.pace_ms:nullptr); pacer.wait(); }
#ifdef SC_HAS_DEVTOOLS
        if(debugger) debugger->completed(frames);
#endif
        return {};
    };
    while (!options.check && !runtime->world.exit_requested && (!bounded || frames<limit || candidate)) {
        ScFrameProfile sample;
        if(profiling) sample.start=ScProfileClock::now();
        sample.render.frame=display_frame;
#ifdef SC_HAS_NETWORK
        network.service();
#endif
        int steps=1;
        ScDeviceInput ui_input;
        bool ui_updated=false;
#ifdef SC_HAS_GRAPHICS
        if (graphics.active) {
            if (sc_render_should_close()) break;
            if (sc_render_reload_requested()) {
                bool images_busy=false;
#ifdef SC_HAS_STREAMING
                images_busy=runtime->script.images&&runtime->script.images->busy();
#endif
                ScProfileScope timing(profiling?&sample.room_ms:nullptr);
                auto started=(candidate||save_io.active()||images_busy)?Result<void>{std::unexpected(Error{"reload","finish the room/save/image request before reloading"})}:begin_candidate(runtime->script.entry);
                if(!started) { reload_error=started.error().message; diagnostic({"reload",reload_error}); }
            }
            if (sc_render_pause_requested()) {
                paused=!paused;
#ifdef SC_HAS_DEVTOOLS
                if(debugger&&debugger->local_resume()) paused=false;
#endif
            }
            bool single_step=sc_render_step_requested();
            if (options.replay.empty()) {
                ui_input=sc_render_sample_input();
                input_buffer.push(ui_input);
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
#ifdef SC_HAS_DEVTOOLS
        if(debugger) {
            debugger->service(runtime->script,frames);
            if(runtime->world.exit_requested) break;
            if(!debugger->running()) steps=0;
            else if(steps==0) steps=1;
        }
#endif
#ifdef SC_HAS_GRAPHICS
        if(graphics.active&&options.replay.empty()) {
            ScProfileScope timing(profiling?&sample.script_update_ms:nullptr);
            if(!sc_script_ui_update(&runtime->script,
                options.frames?(steps?SC_DT:0.f):std::clamp(sc_render_delta(),0.f,.25f),ui_input))
                return std::unexpected(Error{"script",runtime->script.error});
            ui_updated=true;
        }
#endif
        if(candidate) {
            ScProfileScope timing(profiling?&sample.room_ms:nullptr);
            bool can_prepare=true;
#ifdef SC_HAS_DEVTOOLS
            if(debugger&&!debugger->running()) can_prepare=false;
#endif
            if(can_prepare) {
                auto ready=advance_candidate();
                if(!ready) return std::unexpected(ready.error());
                if(*ready&&candidate_tick) {
                    auto finished=finish_tick(sample); if(!finished) return finished;
                    candidate_tick=false;
                }
            }
            io_waiting=candidate!=nullptr; io_saving=false; io_deleting=false; io_images=true; io_error.clear();
            steps=0;
#ifdef SC_HAS_GRAPHICS
            accumulator=0;
#endif
        }
        for (int step=0;step<steps&&!runtime->world.exit_requested;step++) {
            io_waiting=false;
            runtime->world.tone_count=0;
            const char* io_code="save";
            ScResult<bool> ready=true;
            if(save_io.active()) ready=save_io.advance(save_io.request());
            io_saving=true; io_reading=save_io.reading(); io_deleting=save_io.deleting(); io_images=false;
#ifdef SC_HAS_STREAMING
            if(ready&&*ready) {
                io_code="stream"; io_saving=false;
                if(runtime->script.stream) ready=runtime->script.stream->advance(runtime->world.tick);
            }
            if(ready&&*ready&&runtime->script.images) {
                io_code="images"; io_saving=false; io_images=true;
#ifdef SC_HAS_GRAPHICS
                if(graphics.active) ready=sc_render_prepare_images(*runtime->script.images);
                else
#endif
                ready=runtime->script.images->advance();
            }
#endif
            auto unhandled_failure=[&] {
                if(std::strcmp(io_code,"save")==0)
                    return save_io.active()&&save_io.outcome(save_io.request())&&!*save_io.outcome(save_io.request());
#ifdef SC_HAS_STREAMING
                if(std::strcmp(io_code,"images")==0) return runtime->script.images&&runtime->script.images->failed();
                return runtime->script.stream&&runtime->script.stream->failure().get("sequence");
#else
                return false;
#endif
            };
            if(!ready) {
                if(options.headless&&(!runtime->script.has_ui_update||!unhandled_failure()))
                    return std::unexpected(Error{io_code,ready.error()});
                if(io_error!=ready.error()) { io_error=ready.error(); diagnostic({io_code,io_error}); }
                io_waiting=true;
                if(options.headless) {
                    // One explicit UI recovery opportunity; otherwise fail fast.
                    ScProfileScope timing(profiling?&sample.script_update_ms:nullptr);
                    ui_input=runtime->world.input; ui_input.clear_edges();
                    if(!sc_script_ui_update(&runtime->script,0,ui_input))
                        return std::unexpected(Error{"script",runtime->script.error});
                    ui_updated=true;
                    if(!runtime->world.exit_requested&&unhandled_failure())
                        return std::unexpected(Error{io_code,ready.error()});
                }
            } else {
                io_error.clear(); io_waiting=!*ready;
            }
            if(io_waiting) {
#ifdef SC_HAS_GRAPHICS
                accumulator=0; // Disk waits never accumulate catch-up work.
#endif
                break; // No input consumption, Lua update, physics, trace or replay advance.
            }
            if (!options.replay.empty()) {
                if(event_index<replay->size() && (*replay)[event_index].frame==frames) {
                    const auto& event=(*replay)[event_index++];
                    device_replay=event.devices;
                    if(device_replay) input_buffer.push(event.input); else held=event.mask;
                }
                if(device_replay) input_buffer.consume(&runtime->world); else sc_input(&runtime->world,held);
            } else {
                input_buffer.consume(&runtime->world);
#ifdef SC_HAS_GRAPHICS
                if(graphics.active) sc_render_input_consumed();
#endif
            }
            if(record_file.is_open()) {
                ScProfileScope timing(profiling?&sample.diagnostic_ms:nullptr);
                auto event=sc_input_snapshot(runtime->world.input);
                std::get<ScValue::Object>(event.data).emplace("frame",ScValue{static_cast<double>(frames)});
                record_file << sc_json_write(event) << '\n';
                if(!record_file) return std::unexpected(Error{"record","failed writing input recording"});
            }
            held=runtime->world.held;
            {
                ScProfileScope timing(profiling?&sample.script_update_ms:nullptr);
                // Replays/headless present each fixed snapshot to UI first. Live
                // graphics already presented the host sample, even with no tick.
#ifdef SC_HAS_GRAPHICS
                const bool fixed_ui=!graphics.active||!options.replay.empty();
#else
                constexpr bool fixed_ui=true;
#endif
                if(fixed_ui) {
                    if(!sc_script_ui_update(&runtime->script,SC_DT,runtime->world.input))
                        return std::unexpected(Error{"script",runtime->script.error});
                    ui_updated=true;
                    if(runtime->world.exit_requested) break;
                }
                if (!sc_script_update(&runtime->script)) {
#ifdef SC_HAS_DEVTOOLS
                    if(debugger&&runtime->world.exit_requested) break;
#endif
                    return std::unexpected(Error{"script",runtime->script.error});
                }
            }
            {
                ScProfileScope timing(profiling?&sample.simulation_ms:nullptr);
                sc_step(&runtime->world,profiling?&sample.step:nullptr);
            }
            { ScProfileScope timing(profiling?&sample.audio_ms:nullptr); runtime->script.audio->step(SC_DT); }
            ++frames; ++sample.steps;
#ifdef SC_HAS_GRAPHICS
            if (graphics.active) {
                ScProfileScope timing(profiling?&sample.audio_ms:nullptr);
                sc_render_audio(&runtime->script);
            }
#endif
            if (runtime->script.pending_scene[0]) {
                ScProfileScope timing(profiling?&sample.room_ms:nullptr);
                auto started=begin_candidate(runtime->script.pending_scene);
                auto prepared=started?advance_candidate():reject_candidate(started.error());
                if(!prepared) return std::unexpected(prepared.error());
                if(!*prepared) {
                    candidate_tick=true; io_waiting=true; io_images=true; io_saving=false; io_deleting=false; io_error.clear();
#ifdef SC_HAS_GRAPHICS
                    accumulator=0;
#endif
                    break; // Complete this tick's trace only after the candidate resolves.
                }
            }
            auto finished=finish_tick(sample); if(!finished) return finished;
#ifdef SC_HAS_DEVTOOLS
            if(debugger&&!debugger->running()) break;
#endif
            if (bounded && frames>=limit) break;
        }
        if(!ui_updated) {
            // Loading/debug waits must not replay input edges or advance UI time.
            ui_input=runtime->world.input; ui_input.clear_edges();
            ScProfileScope timing(profiling?&sample.script_update_ms:nullptr);
            if(!sc_script_ui_update(&runtime->script,0,ui_input))
                return std::unexpected(Error{"script",runtime->script.error});
        }
#ifdef SC_HAS_GRAPHICS
        if (graphics.active) {
            bool latest=options.frames||io_waiting||paused;
#ifdef SC_HAS_DEVTOOLS
            if(debugger&&!debugger->running()) latest=true;
#endif
            const float alpha=latest?1:static_cast<float>(std::clamp(accumulator/SC_DT,0.0,1.0));
            {
                ScProfileScope timing(profiling?&sample.script_draw_ms:nullptr);
                if (!sc_script_draw(&runtime->script,alpha)) {
#ifdef SC_HAS_DEVTOOLS
                    if(debugger&&runtime->world.exit_requested) break;
#endif
                    return std::unexpected(Error{"script",runtime->script.error});
                }
            }
            if(io_waiting) { ScProfileScope timing(profiling?&sample.audio_ms:nullptr); sc_render_audio(&runtime->script); }
            const char* capture=!candidate&&!options.capture.empty()&&(frames>=limit||runtime->world.exit_requested)?options.capture.c_str():nullptr;
            ScRenderNotice notice{ScRenderNoticeKind::reload_error,reload_error.c_str()};
            if(io_waiting) {
                const bool failed=!io_error.empty();
                ScRenderNoticeKind kind;
                if(io_saving) {
                    if(io_reading) kind=failed?ScRenderNoticeKind::read_error:ScRenderNoticeKind::read_pending;
                    else if(io_deleting) kind=failed?ScRenderNoticeKind::delete_error:ScRenderNoticeKind::delete_pending;
                    else kind=failed?ScRenderNoticeKind::save_error:ScRenderNoticeKind::save_pending;
                } else if(io_images) kind=failed?ScRenderNoticeKind::image_error:ScRenderNoticeKind::image_pending;
                else kind=failed?ScRenderNoticeKind::stream_error:ScRenderNoticeKind::stream_pending;
                notice={kind,io_error.c_str()};
            }
            sc_render_frame(&runtime->world,alpha,notice,paused,profiling?&sample.render:nullptr,&runtime->script,capture);
            if (sc_render_error()[0]) return std::unexpected(Error{"render",sc_render_error()});
            captured=captured||capture!=nullptr;
        }
#endif
#ifdef SC_HAS_DEVTOOLS
        if(debugger&&!debugger->running()&&options.headless)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
        if(io_waiting && options.headless) {
            ScProfileScope timing(profiling?&sample.pace_ms:nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(profiler) {
            profiler->frame(sample,runtime->script,frames);
            if(!profile_file) return std::unexpected(Error{"profile","failed writing profile"});
        }
        ++display_frame;
    }
    // Explicit quit/frame limit drains accepted writes and reports failures.
    if(save_io.active()) for(;;) {
        auto ready=save_io.advance(save_io.request());
        if(!ready) return std::unexpected(Error{"save",ready.error()});
        if(*ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(profiler) {
        profiler->finish();
        if(!profile_file) return std::unexpected(Error{"profile","failed writing profile"});
    }
#ifdef SC_HAS_GRAPHICS
    if (graphics.active && !options.capture.empty() && !captured)
        return std::unexpected(Error{"capture","run ended before a final frame could be captured"});
#endif
    auto output=snapshot(*runtime,frames);
    if (!options.snapshot.empty()) {
        std::ofstream file(options.snapshot);
        if (!file) return std::unexpected(Error{"snapshot","cannot open snapshot output"});
        file << output; file.close();
        if (!file) return std::unexpected(Error{"snapshot","failed writing snapshot output"});
    }
#ifdef SC_HAS_DEVTOOLS
    if(debugger) debugger->finish(frames); else
#endif
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
