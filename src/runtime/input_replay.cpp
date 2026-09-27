#include "shiny/input_replay.h"
#include <cmath>
#include <stdexcept>
#include <cstring>
#include "shiny/text.h"

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
        fields(value,{"frame","keys","gamepad","key_pressed","key_released","button_pressed","button_released","mouse","text","composition","composition_edit","composition_segments","composition_segments_truncated","clipboard","pads"});
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
        if(auto mouse=value.get("mouse")) {
            fields(*mouse,{"x","y","dx","dy","wheel_x","wheel_y","inside","buttons","pressed","released"});
            auto n=[&](const char* name) { auto v=mouse->get(name); return v?static_cast<float>(numeric(*v,-1e6,1e6)):0.0f; };
            in.mouse_x=n("x"); in.mouse_y=n("y"); in.mouse_dx=n("dx"); in.mouse_dy=n("dy"); in.wheel_x=n("wheel_x"); in.wheel_y=n("wheel_y");
            if(auto v=mouse->get("inside")) { auto b=std::get_if<bool>(&v->data); if(!b) throw std::runtime_error("mouse.inside requires boolean"); in.mouse_inside=*b; }
            names(mouse->get("buttons"),SC_MOUSE_BUTTONS,[&](auto i){in.mouse_buttons|=1u<<i;});
            names(mouse->get("pressed"),SC_MOUSE_BUTTONS,[&](auto i){in.mouse_pressed|=1u<<i;});
            names(mouse->get("released"),SC_MOUSE_BUTTONS,[&](auto i){in.mouse_released|=1u<<i;});
        }
        for(auto field:{"text","composition","clipboard"}) if(auto v=value.get(field)) {
            auto text=std::get_if<std::string>(&v->data);
            if(!text||text->size()>=4096||text->find('\0')!=std::string::npos||!sc_utf8(*text)) throw std::runtime_error("invalid input UTF-8 text");
            auto& buffer=std::strcmp(field,"text")==0?in.text:std::strcmp(field,"composition")==0?in.composition:in.clipboard;
            std::memcpy(buffer.data(),text->data(),text->size());
        }
        const auto composition_end=std::strlen(in.composition.data())+1;
        auto position=[&](const ScValue& record,const char* name) {
            const auto n=numeric(required(record,name),1,static_cast<double>(composition_end));
            if(std::floor(n)!=n) throw std::runtime_error("composition positions must be integers");
            const auto byte=static_cast<std::size_t>(n)-1;
            if((static_cast<unsigned char>(in.composition[byte])&0xc0)==0x80)
                throw std::runtime_error("composition position splits a UTF-8 codepoint");
            return static_cast<int>(n);
        };
        if(auto edit=value.get("composition_edit")) {
            fields(*edit,{"cursor","start","finish"});
            in.composition_edit={position(*edit,"cursor"),position(*edit,"start"),position(*edit,"finish")};
            if(in.composition_edit.start>in.composition_edit.finish) throw std::runtime_error("composition_edit range is reversed");
        }
        if(auto value_segments=value.get("composition_segments")) {
            auto segments=std::get_if<ScValue::Array>(&value_segments->data);
            if(!segments||segments->size()>SC_COMPOSITION_SEGMENTS) throw std::runtime_error("composition_segments requires at most 128 segments");
            int previous=1;
            for(const auto& segment:*segments) {
                fields(segment,{"start","finish","kind"});
                const int first=position(segment,"start"),last=position(segment,"finish");
                const auto* kind=std::get_if<std::string>(&required(segment,"kind").data);
                std::size_t id=0;
                while(id<std::size(SC_COMPOSITION_KINDS)&&(!kind||SC_COMPOSITION_KINDS[id]!=*kind)) ++id;
                if(id==std::size(SC_COMPOSITION_KINDS)) throw std::runtime_error("unknown composition segment kind");
                if(first!=previous||last<=first) throw std::runtime_error("composition segments must be ordered, contiguous and nonempty");
                in.composition_segments[in.composition_segment_count++]={static_cast<std::uint16_t>(first),static_cast<std::uint16_t>(last),static_cast<std::uint8_t>(id)};
                previous=last;
            }
            if(!segments->empty()&&static_cast<std::size_t>(previous)!=composition_end)
                throw std::runtime_error("composition segments must cover the complete preedit");
        }
        if(auto truncated=value.get("composition_segments_truncated")) {
            auto flag=std::get_if<bool>(&truncated->data);
            if(!flag) throw std::runtime_error("composition_segments_truncated requires boolean");
            in.composition_segments_truncated=*flag;
            if(*flag&&(in.composition_segment_count||composition_end==1))
                throw std::runtime_error("truncated composition metadata requires a nonempty preedit and no segments");
        }
        if(auto devices=value.get("pads")) {
            auto entries=std::get_if<ScValue::Array>(&devices->data);
            if(!entries||entries->size()!=4) throw std::runtime_error("pads requires four snapshots");
            for(size_t i=0;i<4;++i) {
                const auto& device=(*entries)[i];
                fields(device,{"connected","buttons","axes","pressed","released"});
                ScValue::Object pad_fields,edge_fields;
                for(const auto& [key,item]:std::get<ScValue::Object>(device.data)) {
                    if(key=="pressed"||key=="released") edge_fields.emplace("button_"+key,item);
                    else pad_fields.emplace(key,item);
                }
                edge_fields.emplace("frame",ScValue{0.0});
                edge_fields.emplace("keys",ScValue{ScValue::Array{}});
                edge_fields.emplace("gamepad",ScValue{std::move(pad_fields)});
                auto nested=sc_device_replay_event(ScValue{std::move(edge_fields)});
                if(!nested) throw std::runtime_error(nested.error());
                auto& p=nested->input; in.pads[i]={p.connected,p.buttons,p.button_pressed,p.button_released,p.axes};
            }
        } else in.pads[0]={in.connected,in.buttons,in.button_pressed,in.button_released,in.axes};
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
    ScValue::Array pads;
    for(const auto& p:input.pads) {
        ScValue::Object a; for(const auto& axis:SC_AXES) a.emplace(axis.name,ScValue{double(p.axes[static_cast<size_t>(axis.id)])});
        pads.push_back(ScValue{ScValue::Object{
            {"connected",ScValue{p.connected}},
            {"buttons",names_json(SC_BUTTONS,[&](auto id){return p.buttons&(1u<<id);})},
            {"pressed",names_json(SC_BUTTONS,[&](auto id){return p.pressed&(1u<<id);})},
            {"released",names_json(SC_BUTTONS,[&](auto id){return p.released&(1u<<id);})},
            {"axes",ScValue{std::move(a)}}}});
    }
    ScValue::Array segments;
    for(std::size_t i=0;i<input.composition_segment_count&&i<SC_COMPOSITION_SEGMENTS;++i) {
        const auto& segment=input.composition_segments[i];
        const auto kind=segment.kind<std::size(SC_COMPOSITION_KINDS)?segment.kind:0;
        segments.emplace_back(ScValue::Object{{"start",ScValue{double(segment.start)}},{"finish",ScValue{double(segment.finish)}},
            {"kind",ScValue{std::string(SC_COMPOSITION_KINDS[kind])}}});
    }
    const auto edit=sc_composition_edit(input);
    return ScValue(ScValue::Object{
        {"text",ScValue{std::string(input.text.data())}},
        {"composition",ScValue{std::string(input.composition.data())}},
        {"composition_segments",ScValue{std::move(segments)}},
        {"composition_segments_truncated",ScValue{input.composition_segments_truncated}},
        {"composition_edit",ScValue{ScValue::Object{{"cursor",ScValue{double(edit.cursor)}},
            {"start",ScValue{double(edit.start)}},{"finish",ScValue{double(edit.finish)}}}}},
        {"clipboard",ScValue{std::string(input.clipboard.data())}},
        {"pads",ScValue{std::move(pads)}},
        {"mouse",ScValue{ScValue::Object{
            {"x",ScValue{double(input.mouse_x)}},{"y",ScValue{double(input.mouse_y)}},
            {"dx",ScValue{double(input.mouse_dx)}},{"dy",ScValue{double(input.mouse_dy)}},
            {"wheel_x",ScValue{double(input.wheel_x)}},{"wheel_y",ScValue{double(input.wheel_y)}},
            {"inside",ScValue{input.mouse_inside}},
            {"buttons",names_json(SC_MOUSE_BUTTONS,[&](auto i){return input.mouse_buttons&(1u<<i);})},
            {"pressed",names_json(SC_MOUSE_BUTTONS,[&](auto i){return input.mouse_pressed&(1u<<i);})},
            {"released",names_json(SC_MOUSE_BUTTONS,[&](auto i){return input.mouse_released&(1u<<i);})}}}},
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
