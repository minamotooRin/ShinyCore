#include "audio_contract.h"

ScValue sc_audio_contracts() {
    ScValue::Array options,music,bus,state;
    const ScAudioVoice initial;
    auto field=[](const char* name,const char* type,ScValue value,const char* description) {
        return ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{false}},{"default",std::move(value)},{"description",ScValue{std::string(description)}}};
    };
    for(const auto& item:SC_AUDIO_NUMBERS) {
        auto out=field(item.name,"number",ScValue{double(initial.*item.member)},item.description);
        out.emplace("minimum",ScValue{item.minimum}); out.emplace("maximum",ScValue{item.maximum});
        out.emplace("finite",ScValue{true}); options.emplace_back(std::move(out));
    }
    for(const auto& item:SC_AUDIO_BOOLEANS)
        options.emplace_back(field(item.name,"boolean",ScValue{initial.*item.member},item.description));
    auto priority=field("priority","integer",ScValue{double(initial.priority)},"Only equal/lower-priority voices can be stolen; then lowest priority, oldest age, lowest handle wins.");
    priority.emplace("minimum",ScValue{double(SC_AUDIO_PRIORITY_MIN)}); priority.emplace("maximum",ScValue{double(SC_AUDIO_PRIORITY_MAX)});
    options.emplace_back(std::move(priority));
    auto route=field("bus","'master'|'music'|'sfx'|'ui'",ScValue{},"Routing category. Music defaults to music, sounds to sfx; master routing does not multiply master gain twice.");
    route.erase("default");
    route.emplace("default_by_resource",ScValue{ScValue::Object{{"music",ScValue{std::string("music")}}, {"sound",ScValue{std::string("sfx")}}}});
    options.emplace_back(std::move(route));
    for(const auto& item:options) {
        auto name=item.get("name")->text();
        if(name!="persistent") {
            music.push_back(item);
            if(name=="bus") {
                auto& copy=std::get<ScValue::Object>(music.back().data);
                copy.erase("default_by_resource"); copy["default"]=ScValue{std::string("music")};
            }
        }
        if(name=="volume"||name=="fade"||name=="paused") {
            bus.push_back(item);
            auto& copy=std::get<ScValue::Object>(bus.back().data);
            if(name=="fade") copy["description"]=ScValue{std::string("Seconds remaining to reach the bus target volume.")};
            if(name=="paused") copy["description"]=ScValue{std::string("Pause affected voices; bus fades continue advancing.")};
        }
    }
    for(const auto& item:bus) {
        auto out=std::get<ScValue::Object>(item.data);
        out.erase("default"); out["required"]=ScValue{true}; out.emplace("readonly",ScValue{true});
        if(item.get("name")->text()=="volume") out["description"]=ScValue{std::string("Current bus gain during interpolation.")};
        state.emplace_back(std::move(out));
    }
    auto target=field("target","number",ScValue{},"Target bus gain; preferences multiply independently.");
    target.erase("default"); target["required"]=ScValue{true}; target.emplace("readonly",ScValue{true});
    target.emplace("minimum",ScValue{0.0}); target.emplace("maximum",ScValue{1.0}); state.emplace_back(std::move(target));
    auto type=[](ScValue::Array fields,const char* scope,std::initializer_list<const char*> rules) {
        ScValue::Array constraints; for(const auto* rule:rules) constraints.emplace_back(std::string(rule));
        return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},{"default_scope",ScValue{std::string(scope)}},
            {"constraints",ScValue{std::move(constraints)}}}};
    };
    return ScValue{ScValue::Object{
        {"ScMusicOptions",type(std::move(music),"new persistent music voice",{
            "Plain table with the audio playback fields except persistent; persistence is always enabled.",
            "Defaults apply only when no live persistent non-stopping voice matches the resource path. Reuse patches omitted fields unchanged and never restarts playback.",
            "Candidate room changes remain isolated until commit; no runtime handle needs to be stored in game state or saves."})},
        {"ScAudioOptions",type(std::move(options),"play",{
            "Plain table; unknown fields, metatables, nonfinite values and wrong scalar types are rejected.",
            "Defaults apply to play only; set keeps every omitted value, including an active fade.",
            "Candidate room changes affect an isolated draft until room commit. Only persistent music carries across rooms."})},
        {"ScAudioBusOptions",type(std::move(bus),"application initialization",{
            "Plain table; unknown fields, metatables, nonfinite values and wrong scalar types are rejected.",
            "Omitted fields keep their current values. Volume patches set the target; fade zero applies it immediately.",
            "Bus pause stops affected voices, but bus fades continue. Bus state and application preferences are independent."})},
        {"ScAudioBusState",type(std::move(state),"none",{"Independent read-only snapshot of the requested bus; modifying it does not change audio."})}}};
}
