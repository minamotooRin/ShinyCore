#include "shiny/text.h"
#include "shiny/input_replay.h"
#include "shiny/settings.h"
#ifdef SC_HAS_STREAMING
#include "shiny/stream.h"
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
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
            {"layers",ScValue{ScValue::Object{{"0",ScValue{cells}}}}}}});
        entries.push_back(ScValue{ScValue::Object{{"x",ScValue{double(i)}},{"y",ScValue{-1.0}},{"path",ScValue{name}}}});
    }
    directory.write("index.json",ScValue{ScValue::Object{{"format",ScValue{1.0}},{"chunk_size",ScValue{32.0}},{"chunks",ScValue{entries}}}});
    ScStream stream(index_path,512*1024);
    check(!stream.get(0,-1),"unrequested chunk rejected");
    for(int cycle=0;cycle<4;++cycle) for(int x=0;x<4;++x) {
        stream.request(x,-1);
        auto value=stream.get(x,-1);
        check(value.has_value()&&value->get("x")->number()==x,"chunk coordinates restored after eviction");
        stream.release(x,-1);
        auto stats=stream.statistics();
        check(stats.get("resident_bytes")->number()<=512*1024,"cache stays bounded");
        check(stats.get("pinned")->number()==0,"reference released");
    }
    check(stream.get(-8,-8).has_value(),"absent sparse chunk is empty");
    bool rejected=false;
    try { stream.release(0,-1); } catch(const std::exception&) { rejected=true; }
    check(rejected,"unbalanced release rejected");
    directory.write("0.json",ScValue{ScValue::Object{{"x",ScValue{999.0}},{"y",ScValue{-1.0}}}});
    ScStream malformed(index_path);
    malformed.request(0,-1);
    auto wrong=malformed.get(0,-1);
    check(!wrong&&wrong.error().find("coordinates")!=std::string::npos,"bad coordinates produce a recoverable error");
    std::filesystem::remove(directory.path/"1.json");
    malformed.request(1,-1);
    check(!malformed.get(1,-1),"missing chunk reports failure without deadlock");
    malformed.request(2,-1); malformed.request(3,-1); // Destruction joins in-flight work.
}
#endif
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"source directory required"); input_roundtrip(); dynamic_font(argv[1]); settings();
#ifdef SC_HAS_STREAMING
        streaming();
#endif
        std::cout<<"content: input roundtrip, lazy metrics and enabled streaming checks passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
