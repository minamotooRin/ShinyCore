#include "shiny/input_replay.h"
#include <cmath>
#include <stdexcept>

namespace {
void fields(const ScValue& value,std::initializer_list<std::string_view> allowed) {
    auto object=std::get_if<ScValue::Object>(&value.data);
    if(!object) throw std::runtime_error("expected an object");
    for(const auto& [key,item]:*object) {
        bool found=false;
        for(auto name:allowed) if(key==name) found=true;
        if(!found) throw std::runtime_error("unknown field: "+key);
    }
}
const ScValue& required(const ScValue& value,const char* name) {
    auto item=value.get(name);
    if(!item) throw std::runtime_error(std::string("missing field: ")+name);
    return *item;
}
template<std::size_t N,class Set> void names(const ScValue* value,const ScInputName (&list)[N],Set set) {
    if(!value) return;
    auto array=std::get_if<ScValue::Array>(&value->data);
    if(!array) throw std::runtime_error("controls must be an array of names");
    std::bitset<512> seen;
    for(const auto& item:*array) {
        auto name=std::get_if<std::string>(&item.data);
        int id=name?sc_input_id(list,*name):-1;
        if(id<0) throw std::runtime_error("unknown control name or non-string control");
        auto index=static_cast<std::size_t>(id);
        if(seen[index]) throw std::runtime_error("duplicate control: "+*name);
        seen.set(index); set(index);
    }
}
double numeric(const ScValue& value,double low,double high) {
    auto number=std::get_if<double>(&value.data);
    if(!number||!std::isfinite(*number)||*number<low||*number>high) throw std::runtime_error("number outside allowed range");
    return *number;
}
}
ScResult<ScDeviceReplayEvent> sc_device_replay_event(const ScValue& value) {
    try {
        fields(value,{"frame","keys","gamepad","key_pressed","key_released","button_pressed","button_released"});
        ScDeviceReplayEvent event;
        double frame=numeric(required(value,"frame"),0,1'000'000'000);
        if(std::floor(frame)!=frame) throw std::runtime_error("frame must be an integer");
        event.frame=static_cast<std::uint64_t>(frame);
        auto& in=event.input;
        names(&required(value,"keys"),SC_KEYS,[&](auto id){in.keys.set(id);});
        names(value.get("key_pressed"),SC_KEYS,[&](auto id){in.key_pressed.set(id);});
        names(value.get("key_released"),SC_KEYS,[&](auto id){in.key_released.set(id);});
        names(value.get("button_pressed"),SC_BUTTONS,[&](auto id){in.button_pressed|=1u<<id;});
        names(value.get("button_released"),SC_BUTTONS,[&](auto id){in.button_released|=1u<<id;});
        const auto& pad=required(value,"gamepad"); fields(pad,{"connected","buttons","axes"});
        auto connected=std::get_if<bool>(&required(pad,"connected").data);
        if(!connected) throw std::runtime_error("connected must be boolean");
        in.connected=*connected;
        names(pad.get("buttons"),SC_BUTTONS,[&](auto id){in.buttons|=1u<<id;});
        if(auto axes=pad.get("axes")) {
            fields(*axes,{"left_x","left_y","right_x","right_y","left_trigger","right_trigger"});
            for(const auto& axis:SC_AXES) if(auto val=axes->get(axis.name)) {
                auto n=numeric(*val,axis.id<4?-1:0,1);
                if(!in.connected && n!=0) throw std::runtime_error("disconnected gamepad must be neutral");
                float axis_value=static_cast<float>(n);
                in.axes[static_cast<std::size_t>(axis.id)]=axis_value==0?0:axis_value;
            }
        }
        if(!in.connected) {
            // Explicit releases are valid on a disconnect frame; held/press/axis state is not.
            bool nonzero=in.buttons||in.button_pressed;
            for(float axis:in.axes) nonzero|=axis!=0;
            if(nonzero) throw std::runtime_error("disconnected gamepad must be neutral");
        }
        return event;
    } catch(const std::exception& error) { return std::unexpected(error.what()); }
}
ScValue sc_input_snapshot(const ScDeviceInput& input) {
    auto names_json=[](const auto& list,auto held) {
        ScValue::Array array;
        for(const auto& name:list) if(held(static_cast<std::size_t>(name.id))) array.emplace_back(std::string(name.name));
        return ScValue(std::move(array));
    };
    ScValue::Object axes;
    for(const auto& axis:SC_AXES) axes.emplace(axis.name,ScValue(static_cast<double>(input.axes[static_cast<std::size_t>(axis.id)])));
    return ScValue(ScValue::Object{
        {"keys",names_json(SC_KEYS,[&](auto id){return input.keys[id];})},
        {"key_pressed",names_json(SC_KEYS,[&](auto id){return input.key_pressed[id];})},
        {"key_released",names_json(SC_KEYS,[&](auto id){return input.key_released[id];})},
        {"button_pressed",names_json(SC_BUTTONS,[&](auto id){return input.button_pressed&(1u<<id);})},
        {"button_released",names_json(SC_BUTTONS,[&](auto id){return input.button_released&(1u<<id);})},
        {"gamepad",ScValue(ScValue::Object{
            {"connected",ScValue(input.connected)},
            {"buttons",names_json(SC_BUTTONS,[&](auto id){return input.buttons&(1u<<id);})},
            {"axes",ScValue(std::move(axes))}})}
    });
}
