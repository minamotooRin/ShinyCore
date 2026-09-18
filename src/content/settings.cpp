#include "shiny/settings.h"
#include "shiny/path.h"
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace {
constexpr const char* buses[]={"master","music","sfx","ui"};
double number(const ScValue& value,double low,double high) {
    auto* n=std::get_if<double>(&value.data);
    if(!n||!std::isfinite(*n)||*n<low||*n>high) throw std::runtime_error("settings number outside allowed range");
    return *n;
}
}
ScValue sc_settings_value(const ScSettings& settings) {
    ScValue::Object volumes;
    for(size_t i=0;i<settings.volume.size();++i) volumes.emplace(buses[i],ScValue{double(settings.volume[i])});
    return ScValue{ScValue::Object{
        {"width",ScValue{double(settings.width)}},{"height",ScValue{double(settings.height)}},
        {"mode",ScValue{std::string(settings.borderless?"borderless":"windowed")}},
        {"scale",ScValue{std::string(settings.smooth?"smooth":"integer")}},
        {"vsync",ScValue{settings.vsync}},{"volume",ScValue{std::move(volumes)}},{"bindings",settings.bindings}}};
}
ScResult<ScSettings> sc_settings_patch(const ScSettings& original,const ScValue& patch) {
    try {
        auto* fields=std::get_if<ScValue::Object>(&patch.data);
        if(!fields) return std::unexpected("settings patch requires an object");
        ScSettings result=original;
        for(const auto& [key,value]:*fields) {
            if(key=="width"||key=="height") {
                double n=number(value,key=="width"?320:180,key=="width"?7680:4320);
                if(std::floor(n)!=n) throw std::runtime_error("settings resolution requires integers");
                (key=="width"?result.width:result.height)=static_cast<int>(n);
            } else if(key=="mode") {
                auto name=value.text();
                if(name!="windowed"&&name!="borderless") throw std::runtime_error("settings.mode must be windowed or borderless");
                result.borderless=name=="borderless";
            } else if(key=="scale") {
                auto name=value.text();
                if(name!="integer"&&name!="smooth") throw std::runtime_error("settings.scale must be integer or smooth");
                result.smooth=name=="smooth";
            } else if(key=="vsync") {
                auto* b=std::get_if<bool>(&value.data);
                if(!b) throw std::runtime_error("settings.vsync requires boolean");
                result.vsync=*b;
            } else if(key=="volume") {
                auto* values=std::get_if<ScValue::Object>(&value.data);
                if(!values) throw std::runtime_error("settings.volume requires an object");
                for(const auto& [bus,gain]:*values) {
                    bool found=false;
                    for(size_t i=0;i<4;++i) if(bus==buses[i]) { result.volume[i]=static_cast<float>(number(gain,0,1)); found=true; }
                    if(!found) throw std::runtime_error("unknown settings volume bus: "+bus);
                }
            } else if(key=="bindings") {
                auto valid=sc_state_validate(value);
                if(!valid||sc_json_write(value).size()>16384) throw std::runtime_error("bindings require a plain object of at most 16 KiB");
                result.bindings=std::move(*valid);
            } else throw std::runtime_error("unknown settings field: "+key);
        }
        return result;
    } catch(const std::exception& error) { return std::unexpected(error.what()); }
}
ScResult<void> ScSettingsService::initialize(const ScValue* display,std::string_view root,std::string_view project) {
    auto defaults=display?sc_settings_patch(ScSettings{},*display):ScResult<ScSettings>{ScSettings{}};
    if(!defaults) return std::unexpected("project.display: "+defaults.error());
    if(initialized_) return {};
    current=std::move(*defaults); initialized_=true;
    if(root.empty()||project.empty()) return {};
    path_=std::string(root)+"/"+std::string(project)+"/config/settings.json";
    std::error_code error;
    const bool exists=std::filesystem::exists(sc_path(path_),error);
    if(error) { last_error="cannot read settings: "+error.message(); return {}; }
    if(!exists) return {};
    auto record=sc_json_file(path_,32768,18);
    if(!record) { last_error=record.error(); return {}; }
    auto format=record->get("format"),value=record->get("settings");
    if(!format||format->number()!=1||!value) { last_error="unsupported settings format"; return {}; }
    auto restored=sc_settings_patch(current,*value);
    if(restored) current=std::move(*restored); else last_error=restored.error();
    return {};
}
ScResult<void> ScSettingsService::apply(const ScValue& patch,bool persist) {
    auto candidate=sc_settings_patch(current,patch);
    if(!candidate) { last_error=candidate.error(); return std::unexpected(last_error); }
    auto restore=[&](const std::string& error) -> ScResult<void> {
        last_error=error;
        if(apply_native) {
            try {
                auto rolled_back=apply_native(current);
                if(!rolled_back) last_error+="; window rollback failed: "+rolled_back.error();
            } catch(const std::exception& failure) { last_error+="; window rollback failed: "+std::string(failure.what()); }
        }
        return std::unexpected(last_error);
    };
    try {
    if(apply_native) {
        auto applied=apply_native(*candidate);
        if(!applied) return restore(applied.error());
    }
    if(persist&&!path_.empty()) {
        auto record=ScValue{ScValue::Object{{"format",ScValue{1.0}},{"settings",sc_settings_value(*candidate)}}};
        auto written=sc_atomic_write(path_,sc_json_write(record));
        if(!written) return restore(written.error());
    }
    } catch(const std::exception& error) { return restore(error.what()); }
    current=std::move(*candidate); last_error.clear(); return {};
}
