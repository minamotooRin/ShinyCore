#pragma once
#include "script_binding.h"
#include "shiny/state.h"
#include <span>
#include <optional>

enum class ScLuaMutationWhen { non_nil, present };
enum class ScLuaPhases { read, mutate, update, initialize, ui_mutate, update_ui, draw };
struct ScLuaParameter {
    const char* name;
    const char* type;
    bool required{true};
    const char* description{};
    const ScValue* initial{};
    std::optional<double> minimum{},maximum{};
    bool exclusive_maximum{};
};
struct ScLuaReturn { const char* name; const char* type; const char* description{}; };
struct ScLuaContract {
    std::span<const ScLuaParameter> parameters;
    const char* result; // nullptr means no returned values.
    ScLuaPhases phases;
    const char* capacity{};
    const char* error_result{};
    const char* module{"core"};
    const char* mutation_parameter{}; // A selected argument switches a read/patch API to mutation.
    ScLuaPhases mutation_phases{ScLuaPhases::mutate};
    std::span<const ScLuaReturn> results{};
    ScLuaMutationWhen mutation_when{ScLuaMutationWhen::non_nil};
};

inline ScValue::Array sc_api_phases(ScLuaPhases value) {
    ScValue::Array phases;
    if(value!=ScLuaPhases::update&&value!=ScLuaPhases::update_ui&&value!=ScLuaPhases::draw)
        for(const char* phase:{"load","init"}) phases.emplace_back(std::string(phase));
    if(value!=ScLuaPhases::initialize&&value!=ScLuaPhases::draw) phases.emplace_back(std::string("update"));
    if(value==ScLuaPhases::read||value==ScLuaPhases::draw) phases.emplace_back(std::string("draw"));
    if(value==ScLuaPhases::read||value==ScLuaPhases::ui_mutate||value==ScLuaPhases::update_ui) phases.emplace_back(std::string("ui_update"));
    return phases;
}

// One entry drives registration and machine-readable documentation.
struct ScLuaApi {
    const char* name;
    lua_CFunction function;
    const char* signature;
    const char* description;
    const ScLuaContract* contract{};
};
inline void sc_api_register(lua_State* L,const ScLuaApi* api) {
    for(auto* entry=api;entry->name;++entry) {
        lua_pushcfunction(L,entry->function); lua_setfield(L,-2,entry->name);
    }
}
inline void sc_api_describe(const ScLuaApi* api,const char* prefix,bool comma=true) {
    for(auto* entry=api;entry->name;++entry) {
        ScValue::Object item{{"name",ScValue{std::string(prefix)+entry->name}},
            {"signature",ScValue{std::string(entry->signature)}},{"description",ScValue{std::string(entry->description)}},
            {"contract",ScValue{}}};
        if(const auto* contract=entry->contract) {
            ScValue::Array parameters,returns;
            for(const auto& parameter:contract->parameters) {
                ScValue::Object out{{"name",ScValue{std::string(parameter.name)}},{"type",ScValue{std::string(parameter.type)}},{"required",ScValue{parameter.required}}};
                if(parameter.description) out.emplace("description",ScValue{std::string(parameter.description)});
                if(parameter.initial) out.emplace("default",*parameter.initial);
                if(parameter.minimum) out.emplace("minimum",ScValue{*parameter.minimum});
                if(parameter.maximum) out.emplace("maximum",ScValue{*parameter.maximum});
                if(parameter.exclusive_maximum) out.emplace("exclusive_maximum",ScValue{true});
                parameters.emplace_back(std::move(out));
            }
            if(contract->result) returns.emplace_back(ScValue::Object{{"type",ScValue{std::string(contract->result)}}});
            if(contract->error_result) returns.emplace_back(ScValue::Object{{"type",ScValue{std::string(contract->error_result)}}});
            for(const auto& result:contract->results) {
                ScValue::Object out{{"name",ScValue{std::string(result.name)}},{"type",ScValue{std::string(result.type)}}};
                if(result.description) out.emplace("description",ScValue{std::string(result.description)});
                returns.emplace_back(std::move(out));
            }
            ScValue::Object metadata{{"parameters",ScValue{std::move(parameters)}},{"returns",ScValue{std::move(returns)}},
                {"phases",ScValue{sc_api_phases(contract->phases)}},{"module",ScValue{std::string(contract->module)}}};
            if(contract->capacity) metadata.emplace("capacity",ScValue{std::string(contract->capacity)});
            if(contract->mutation_parameter) metadata.emplace("mutation",ScValue{ScValue::Object{
                {"parameter",ScValue{std::string(contract->mutation_parameter)}},
                {"when",ScValue{std::string(contract->mutation_when==ScLuaMutationWhen::present?"present":"non_nil")}},
                {"phases",ScValue{sc_api_phases(contract->mutation_phases)}}}});
            item["contract"]=ScValue{std::move(metadata)};
        }
        const auto json=sc_json_write(ScValue{std::move(item)});
        std::printf("%s%s",comma?",":"",json.c_str());
        comma=true;
    }
}
