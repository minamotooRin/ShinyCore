#include "shiny/text.h"
#include "shiny/input_replay.h"
#include "shiny/settings.h"
#include "shiny/project.h"
#include "shiny/physics.h"
#include "shiny/navigation.h"
#ifdef SC_HAS_STREAMING
#include "shiny/stream.h"
#endif
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
void tiled_chunk_ownership() {
    for(float offset:{0.f,-8.f,-256.f}) {
        ScWorld world; world.map.width=64; world.map.height=32; world.map.tile_size=8;
        ScTileGraphic tile; tile.gid=1; tile.w=tile.h=8; tile.collision='#';
        world.tile_graphics.push_back(tile);
        ScLayer layer; layer.name="ground"; layer.x=offset; layer.y=80;
        layer.cells.resize(64,1); world.layers.push_back(layer);
        check(bool(sc_project_tiles(&world)),"generate chunked Tiled collision");
        check(world.terrain_shapes.size()==(offset==-8?3u:2u),"Tiled merge stops at world chunk boundaries");
        for(const auto& shape:world.terrain_shapes)
            check(std::floor(shape.x/256)==std::floor((shape.x+shape.w-1)/256),"merged tiles remain in one owner chunk");
        sc_physics_sync(&world); const auto before=sc_physics_terrain_stats(world);
        world.layers[0].cells[40]=0;
        check(bool(sc_project_tiles(&world)),"edit one Tiled collision cell"); sc_physics_sync(&world);
        check(sc_physics_terrain_stats(world).replacements==before.replacements+1,"Tiled edit rebuilds only its owner chunk");
        const auto ray=sc_physics_ray(&world,offset+324,60,0,40);
        check(!ray.hit,"removed Tiled cell no longer blocks ray");
    }
}
void tiled_navigation_transaction() {
    ScWorld world; world.map.width=3; world.map.height=1; world.map.tile_size=8;
    ScNavigationRegion region; region.map=world.map;
    ScTileGraphic tile; tile.gid=1; tile.w=tile.h=8; tile.collision='#';
    world.tile_graphics.push_back(tile);
    ScLayer layer; layer.name="wall"; layer.cells={0,1,0}; world.layers.push_back(layer);
    check(bool(sc_project_tiles(&world,&region)),"prepare both navigation grids");
    const auto blocked=world.map.navigation_blocked;
    check(blocked[1]&&blocked==region.map.navigation_blocked,"same initial obstacle");
    for(auto* exhausted:{&world.map.navigation_revision,&region.map.navigation_revision,&world.terrain_revision}) {
        const auto saved=*exhausted; *exhausted=UINT64_MAX;
        const auto room_revision=world.map.navigation_revision,region_revision=region.map.navigation_revision,
                   terrain_revision=world.terrain_revision;
        world.layers[0].cells[1]=0;
        auto result=sc_project_tiles(&world,&region,true);
        check(!result&&result.error().find("revision exhausted")!=std::string::npos,"exhausted revisions reject before commit");
        check(world.map.navigation_blocked==blocked&&region.map.navigation_blocked==blocked,"failed commit preserves both masks");
        check(world.map.navigation_revision==room_revision&&region.map.navigation_revision==region_revision&&
              world.terrain_revision==terrain_revision,"failed commit preserves all revisions");
        check(world.terrain_shapes.size()==1&&world.terrain_shapes[0].x==8,"failed commit preserves geometry");
        *exhausted=saved; world.layers[0].cells[1]=1;
    }
    world.layers[0].cells[1]=0;
    check(bool(sc_project_tiles(&world,&region,true)),"retry publishes geometry and navigation together");
    check(world.terrain_shapes.empty()&&world.map.navigation_blocked.none()&&region.map.navigation_blocked.none(),"both grids see the opening");
}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("shiny-content-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    explicit Directory(bool unicode=false) {
        if(unicode) path+=std::filesystem::path(u8"-\u5730\u56fe");
        std::filesystem::create_directory(path);
    }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path,error); }
    void write(const char* name,const ScValue& value) {
        std::ofstream output(path/name,std::ios::binary); output<<sc_json_write(value);
        if(!output) throw std::runtime_error("fixture write failed");
    }
};
void input_roundtrip() {
    ScDeviceInput sample;
    sample.pads[2].connected=true;
    sample.pads[2].pressed=sample.pads[2].released=1u<<6; // Tap with no held button.
    sample.pads[2].axes[1]=-.5f;
    sample.mouse_pressed=sample.mouse_released=1;
    auto value=sc_input_snapshot(sample);
    std::get<ScValue::Object>(value.data).emplace("frame",ScValue{17.0});
    auto decoded=sc_device_replay_event(value);
    check(decoded.has_value(),"snapshot must decode");
    check(decoded->input.pads[2].pressed==(1u<<6)&&decoded->input.pads[2].released==(1u<<6),"tap edges survive replay");
    check(decoded->input.pads[2].axes[1]==-.5f&&!decoded->input.pads[1].connected,"stable controller slots");
    check(decoded->input.mouse_pressed==1&&decoded->input.mouse_released==1,"pointer tap survives replay");
}
void dynamic_font(const std::string& root) {
    ScWorld world;
    auto bitmap=sc_text_layout(&world,"Wi",20,"");
    check(bitmap.width==18&&bitmap.letters[1].x==15,"default bitmap advances match native glyph widths plus spacing");
    auto wrapped=sc_text_layout(&world,"WW",20,"",15);
    check(wrapped.width==15&&wrapped.height==40,"default bitmap wrap follows actual glyph advances");
    ScResource font; font.name="ui"; font.type="font"; font.path="examples/workshop/assets/workshop.ttf";
    check(sc_font_load(font,root).has_value(),"font fixture loads");
    check(font.glyphs.size()<=95,"no predeclared CJK glyphs");
    world.resources.push_back(std::move(font));
    auto first=sc_text_layout(&world,"\xe6\x98\x9f\xe7\x81\xaf",16,"ui");
    check(!first.missing&&first.letters.size()==2&&first.width>16,"CJK metrics loaded on demand");
    const auto count=world.resources[0].glyphs.size();
    auto second=sc_text_layout(&world,"\xe6\x98\x9f\xe7\x81\xaf",16,"ui");
    check(first.width==second.width&&world.resources[0].glyphs.size()==count,"metrics cache stable");
}
void settings() {
    Directory directory;
    ScSettingsService service;
    check(service.initialize(nullptr,directory.path.string(),"settings-test").has_value(),"settings initialize");
    check(service.apply(ScValue{ScValue::Object{{"width",ScValue{960.0}}}}).has_value(),"settings persist");
    auto path=directory.path/"settings-test/config/settings.json";
    check(std::filesystem::is_regular_file(path),"settings stored separately from slots");
    ScSettingsService restored;
    check(restored.initialize(nullptr,directory.path.string(),"settings-test").has_value()&&restored.current.width==960,"settings restore");
    service.apply_native=[](const ScSettings& value) -> ScResult<void> {
        if(value.width==777) return std::unexpected("injected native refusal");
        return {};
    };
    check(!service.apply(ScValue{ScValue::Object{{"width",ScValue{777.0}}}}),"native refusal propagated");
    check(service.current.width==960&&service.last_error=="injected native refusal","failed native apply preserves configuration");
    std::filesystem::create_directory(path.string()+".tmp");
    check(!service.apply(ScValue{ScValue::Object{{"width",ScValue{1280.0}}}}),"disk failure propagated");
    check(service.current.width==960,"failed persistence rolls back current settings");
    auto record=sc_json_file(path.string());
    check(record&&record->get("settings")->get("width")->number()==960,"failed persistence preserves file");
    check(!service.apply(ScValue{ScValue::Object{{"volume",ScValue{ScValue::Object{{"master",ScValue{2.0}}}}}}}),"volume range validated");
}
#ifdef SC_HAS_STREAMING
void streaming() {
    Directory directory(true);
    const auto index_utf8=(directory.path/"index.json").generic_u8string();
    const std::string index_path(reinterpret_cast<const char*>(index_utf8.data()),index_utf8.size());
    ScValue::Array cells(1024,ScValue{1.0}),entries;
    for(int i=0;i<4;++i) {
        std::string name=std::to_string(i)+".json";
        directory.write(name.c_str(),ScValue{ScValue::Object{{"x",ScValue{double(i)}},{"y",ScValue{-1.0}},
            {"objects",ScValue{ScValue::Array{}}},{"layers",ScValue{ScValue::Object{{"0",ScValue{cells}}}}}}});
        entries.push_back(ScValue{ScValue::Object{{"x",ScValue{double(i)}},{"y",ScValue{-1.0}},{"path",ScValue{name}},{"bytes",ScValue{double(std::filesystem::file_size(directory.path/name))}}}});
    }
    directory.write("index.json",ScValue{ScValue::Object{{"format",ScValue{3.0}},{"tilewidth",ScValue{8.0}},{"tileheight",ScValue{8.0}},{"layers",ScValue{ScValue::Array{ScValue{ScValue::Object{{"type",ScValue{std::string{"tilelayer"}}}}},ScValue{ScValue::Object{{"type",ScValue{std::string{"objectgroup"}}}}}}}},{"chunk_size",ScValue{32.0}},{"chunks",ScValue{entries}}}});
    auto advance=[](ScStream& stream,std::uint64_t frame) {
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        for(;;) {
            auto ready=stream.advance(frame);
            if(!ready || *ready) return ready;
            if(std::chrono::steady_clock::now()>=until) throw std::runtime_error("stream test timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    ScStream stream(index_path,512*1024);
    check(!stream.get(0,-1),"unrequested chunk rejected");
    for(int cycle=0;cycle<4;++cycle) for(int x=0;x<4;++x) {
        auto frame=static_cast<std::uint64_t>(cycle*4+x);
        stream.request(x,-1,frame);
        check(bool(advance(stream,frame)),"scheduled chunk commits");
        auto value=stream.get(x,-1);
        check(value&&value->has_value()&&(**value).get("x")->number()==x,"chunk coordinates restored after eviction");
        stream.release(x,-1);
        auto stats=stream.statistics();
        check(stats.get("resident_bytes")->number()<=512*1024,"cache stays bounded");
        check(stats.get("pinned")->number()==0,"reference released");
    }
    check(stream.get(-8,-8).has_value(),"absent sparse chunk is empty");
    bool rejected=false;
    try { stream.release(0,-1); } catch(const std::exception&) { rejected=true; }
    check(rejected,"unbalanced release rejected");
    directory.write("0.json",ScValue{ScValue::Object{{"x",ScValue{9.0}},{"y",ScValue{-1.0}},
        {"objects",ScValue{ScValue::Array{}}},{"layers",ScValue{ScValue::Object{{"0",ScValue{cells}}}}}}});
    ScStream malformed(index_path);
    malformed.request(0,-1,0);
    auto wrong=advance(malformed,0);
    check(!wrong&&wrong.error().find("coordinates")!=std::string::npos,"bad coordinates produce a recoverable error");
    std::filesystem::remove(directory.path/"1.json");
    malformed.release(0,-1); check(bool(advance(malformed,0)),"failed request can be cancelled");
    malformed.request(1,-1,1);
    check(!advance(malformed,1),"missing chunk reports failure without deadlock");
    malformed.request(2,-1,2); malformed.request(3,-1,2); // Destruction joins in-flight work.
}
#endif
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--navigation") {
            tiled_chunk_ownership(); tiled_navigation_transaction();
            std::cout<<"content: local terrain/navigation transactions passed\n"; return 0;
        }
        check(argc==2,"source directory required"); input_roundtrip(); dynamic_font(argv[1]); settings(); tiled_chunk_ownership(); tiled_navigation_transaction();
#ifdef SC_HAS_STREAMING
        streaming();
#endif
        std::cout<<"content: input roundtrip, lazy metrics and enabled streaming checks passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
