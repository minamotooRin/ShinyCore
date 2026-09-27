#include <algorithm>
#include "script_input.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include "shiny/input_replay.h"
#include <cmath>
#include <cstring>
#include <utf8proc.h>

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void arguments(lua_State* L,int minimum,int maximum) {
    if(lua_gettop(L)<minimum||lua_gettop(L)>maximum) luaL_error(L,"unexpected input argument count");
}
void mutable_phase(lua_State* L) {
    if(script(L)->phase!=0&&script(L)->phase!=1&&script(L)->phase!=4)
        luaL_error(L,"input mutation requires load, init, update or ui_update");
}
const char* string(lua_State* L,int index,size_t* length=nullptr) {
    if(lua_type(L,index)!=LUA_TSTRING) luaL_error(L,"input argument requires a string");
    return lua_tolstring(L,index,length);
}
void utf8(lua_State* L,const char* bytes,size_t size) {
    for(size_t offset=0;offset<size;) {
        utf8proc_int32_t code=0;
        const auto length=utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(bytes+offset),
            static_cast<utf8proc_ssize_t>(size-offset),&code);
        if(length<=0) luaL_error(L,"invalid UTF-8 text");
        offset+=static_cast<size_t>(length);
    }
}
int input_snapshot(lua_State* L) { arguments(L,0,0); auto* s=script(L); s->scratch=sc_input_snapshot(s->world->input); sc_lua_push(L,s->scratch); return 1; }
int mouse(lua_State* L) {
    arguments(L,0,0);
    const auto& i=script(L)->world->input;
    lua_pushnumber(L,i.mouse_x); lua_pushnumber(L,i.mouse_y); lua_pushboolean(L,i.mouse_inside); return 3;
}
int text_input(lua_State* L) {
    if(lua_gettop(L)!=0) return luaL_error(L,"input.text expects no arguments");
    const auto& i=script(L)->world->input; const auto edit=sc_composition_edit(i);
    lua_pushstring(L,i.text.data()); lua_pushstring(L,i.composition.data());
    lua_pushinteger(L,edit.cursor); lua_pushinteger(L,edit.start); lua_pushinteger(L,edit.finish);
    lua_createtable(L,static_cast<int>(std::min(i.composition_segment_count,SC_COMPOSITION_SEGMENTS)),0);
    for(std::size_t index=0;index<i.composition_segment_count&&index<SC_COMPOSITION_SEGMENTS;++index) {
        const auto& segment=i.composition_segments[index];
        const auto kind=segment.kind<std::size(SC_COMPOSITION_KINDS)?segment.kind:0;
        lua_createtable(L,0,3);
        lua_pushinteger(L,segment.start); lua_setfield(L,-2,"start");
        lua_pushinteger(L,segment.finish); lua_setfield(L,-2,"finish");
        lua_pushstring(L,SC_COMPOSITION_KINDS[kind].data()); lua_setfield(L,-2,"kind");
        lua_rawseti(L,-2,static_cast<lua_Integer>(index+1));
    }
    lua_pushboolean(L,i.composition_segments_truncated); return 7;
}
template<int Edge> int mouse_button(lua_State* L) {
    arguments(L,1,1); size_t length=0; const char* name=string(L,1,&length);
    int id=sc_input_id(SC_MOUSE_BUTTONS,std::string_view(name,length)); if(id<0) return luaL_error(L,"unknown mouse button");
    const auto& in=script(L)->world->input; auto bits=Edge==0?in.mouse_buttons:Edge==1?in.mouse_pressed:in.mouse_released;
    lua_pushboolean(L,(bits&(1u<<id))!=0); return 1;
}
template<int Edge> int keyboard(lua_State* L) {
    size_t length=0; const char* name=string(L,1,&length);
    int id=sc_input_id(SC_KEYS,std::string_view(name,length));
    if(lua_gettop(L)!=1||id<0) return luaL_error(L,"expected one keyboard control name");
    const auto& in=script(L)->world->input;
    const auto& bits=Edge==0?in.keys:Edge==1?in.key_pressed:in.key_released;
    lua_pushboolean(L,bits[static_cast<size_t>(id)]); return 1;
}
ScPadInput pad_input(lua_State* L,int argument) {
    const auto& in=script(L)->world->input;
    if(lua_isnoneornil(L,argument)) return {in.connected,in.buttons,in.button_pressed,in.button_released,in.axes};
    if(lua_type(L,argument)!=LUA_TNUMBER) luaL_error(L,"gamepad slot requires an integer");
    auto slot=luaL_checkinteger(L,argument);
    if(slot<1||slot>4) luaL_error(L,"gamepad slot must be 1..4");
    return in.pads[static_cast<size_t>(slot-1)];
}
template<int Edge> int pad_button(lua_State* L) {
    size_t length=0; const char* name=string(L,1,&length);
    int id=sc_input_id(SC_BUTTONS,std::string_view(name,length));
    if(lua_gettop(L)>2||id<0) return luaL_error(L,"unknown gamepad button or argument");
    const auto pad=pad_input(L,2);
    auto bits=Edge==0?pad.buttons:Edge==1?pad.pressed:pad.released;
    lua_pushboolean(L,(bits&(1u<<id))!=0); return 1;
}
int pad_connected(lua_State* L) {
    if(lua_gettop(L)>1) return luaL_error(L,"expected optional gamepad slot");
    lua_pushboolean(L,pad_input(L,1).connected); return 1;
}
int pad_axis(lua_State* L) {
    size_t length=0; const char* name=string(L,1,&length);
    int id=sc_input_id(SC_AXES,std::string_view(name,length));
    if(!lua_isnoneornil(L,2)&&lua_type(L,2)!=LUA_TNUMBER) return luaL_error(L,"deadzone requires a number");
    double deadzone=luaL_optnumber(L,2,.2);
    if(lua_gettop(L)>3||id<0||!std::isfinite(deadzone)||deadzone<0||deadzone>=1) return luaL_error(L,"invalid axis, deadzone or argument");
    const auto pad=pad_input(L,3);
    double value=pad.connected?pad.axes[static_cast<size_t>(id)]:0;
    lua_pushnumber(L,std::fabs(value)<=deadzone?0:std::copysign((std::fabs(value)-deadzone)/(1-deadzone),value)); return 1;
}
int wheel(lua_State* L) { arguments(L,0,0); const auto& in=script(L)->world->input; lua_pushnumber(L,in.wheel_x); lua_pushnumber(L,in.wheel_y); return 2; }
int boundaries(lua_State* L) {
    arguments(L,1,1); size_t size=0; const char* bytes=string(L,1,&size);
    if(size>65536) return luaL_error(L,"text exceeds 65536 bytes");
    lua_newtable(L); lua_Integer count=0; utf8proc_int32_t previous=0,state=0; size_t offset=0;
    while(offset<size) {
        utf8proc_int32_t code=0;
        auto length=utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(bytes+offset),static_cast<utf8proc_ssize_t>(size-offset),&code);
        if(length<=0) return luaL_error(L,"invalid UTF-8 text");
        if(offset==0||utf8proc_grapheme_break_stateful(previous,code,&state)) { lua_pushinteger(L,static_cast<lua_Integer>(offset+1)); lua_rawseti(L,-2,++count); }
        previous=code; offset+=static_cast<size_t>(length);
    }
    lua_pushinteger(L,static_cast<lua_Integer>(size+1)); lua_rawseti(L,-2,++count); return 1;
}
int focus_text(lua_State* L) {
    mutable_phase(L); auto* w=script(L)->world;
    if(lua_isboolean(L,1)&&!lua_toboolean(L,1)) { arguments(L,1,1); w->text_focus=false; return 0; }
    arguments(L,2,2);
    if(lua_type(L,1)!=LUA_TNUMBER||lua_type(L,2)!=LUA_TNUMBER) return luaL_error(L,"text focus coordinates require numbers");
    double x=luaL_checknumber(L,1),y=luaL_checknumber(L,2);
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1e6||std::fabs(y)>1e6) return luaL_error(L,"text focus coordinates outside range");
    w->text_x=static_cast<float>(x); w->text_y=static_cast<float>(y); w->text_focus=true; return 0;
}
int clipboard(lua_State* L) {
    arguments(L,0,1);
    auto* w=script(L)->world;
    if(lua_isnoneornil(L,1)) { lua_pushstring(L,w->input.clipboard.data()); return 1; }
    mutable_phase(L); size_t length=0; const char* text=string(L,1,&length);
    if(length>=w->clipboard_out.size()||std::memchr(text,0,length)) return luaL_error(L,"clipboard requires at most 4095 UTF-8 bytes");
    utf8(L,text,length);
    std::memcpy(w->clipboard_out.data(),text,length); w->clipboard_out[length]=0; w->clipboard_write=true; return 0;
}
const ScLuaReturn input_text_results[]={
    {"committed","string","UTF-8 text committed in this snapshot."},
    {"composition","string","Uncommitted UTF-8 preedit text."},
    {"cursor","integer","One-based UTF-8 insertion position within composition; unavailable metadata defaults to end."},
    {"selection_start","integer","Start of the IME target segment, one-based UTF-8 position."},
    {"selection_end","integer","Exclusive end of first contiguous target range; an empty range means no target selection."},
    {"segments","ScInputCompositionSegment[]","Ordered preedit clause/attribute runs; at most 128. Empty when metadata is unavailable or exceeds capacity."},
    {"segments_truncated","boolean","True when native segmentation exceeds capacity; text and cursor remain intact, segments is empty."}
};
const ScLuaContract input_text_contract{{},nullptr,ScLuaPhases::read,"4095 UTF-8 bytes per string; 128 composition segments",nullptr,"core",nullptr,ScLuaPhases::mutate,input_text_results};
const ScValue deadzone_default{.2}, selected_default{};
constexpr ScLuaParameter key_parameters[]={{"name","ScKey"}};
constexpr ScLuaParameter mouse_parameters[]={{"name","ScMouseButton"}};
const ScLuaParameter slot_parameter{"slot","integer",false,"Omitted or nil reads the selected controller, not necessarily slot 1.",&selected_default,1,4};
const ScLuaParameter pad_parameters[]={{"name","ScGamepadButton"},slot_parameter};
const ScLuaParameter connected_parameters[]={slot_parameter};
const ScLuaParameter axis_parameters[]={
    {"name","ScGamepadAxis"},
    {"deadzone","number",false,"Rescale magnitude outside the deadzone to 0..1; disconnected pads return zero.",&deadzone_default,0,1,true},
    slot_parameter};
constexpr ScLuaParameter focus_parameters[]={
    {"x","number|false",true,"Logical viewport X, or false as the only argument to end focus.",nullptr,-1e6,1e6},
    {"y","number",false,"Required with numeric x; forbidden with false.",nullptr,-1e6,1e6}};
constexpr ScLuaParameter clipboard_parameters[]={{"text","string",false,"Omitted/nil reads the snapshot; a UTF-8 string without NUL queues a write and returns no values."}};
constexpr ScLuaParameter boundaries_parameters[]={{"text","string",true,"Valid UTF-8, including an empty string; byte positions include the end sentinel."}};
constexpr ScLuaReturn mouse_results[]={
    {"x","number","Logical viewport X; may be outside its bounds."},
    {"y","number","Logical viewport Y; may be outside its bounds."},
    {"inside","boolean","False for black bars and positions outside the viewport."}};
constexpr ScLuaReturn wheel_results[]={
    {"x","number","Accumulated horizontal wheel delta."},
    {"y","number","Accumulated vertical wheel delta."}};
const ScLuaContract key_contract{key_parameters,"boolean",ScLuaPhases::read};
const ScLuaContract mouse_button_contract{mouse_parameters,"boolean",ScLuaPhases::read};
const ScLuaContract pad_contract{pad_parameters,"boolean",ScLuaPhases::read,"4 stable slots"};
const ScLuaContract connected_contract{connected_parameters,"boolean",ScLuaPhases::read,"4 stable slots"};
const ScLuaContract axis_contract{axis_parameters,"number",ScLuaPhases::read,"4 stable slots"};
const ScLuaContract focus_contract{focus_parameters,nullptr,ScLuaPhases::ui_mutate};
const ScLuaContract clipboard_contract{clipboard_parameters,"string|nil",ScLuaPhases::read,
    "4095 UTF-8 bytes; last successful queued write wins",nullptr,"core","text",ScLuaPhases::ui_mutate};
const ScLuaContract boundaries_contract{boundaries_parameters,"integer[]",ScLuaPhases::read,"65536 UTF-8 bytes"};
const ScLuaContract snapshot_contract{{},"ScInputSnapshot",ScLuaPhases::read,"4 pad slots; 4095 UTF-8 bytes per text field; 128 composition segments"};
const ScLuaContract mouse_contract{.parameters={},.result=nullptr,.phases=ScLuaPhases::read,.results=mouse_results};
const ScLuaContract wheel_contract{.parameters={},.result=nullptr,.phases=ScLuaPhases::read,.results=wheel_results};
const ScLuaApi input_api[]={
    {"key_down",sc_lua_guard<keyboard<0>>,"key_down(name) -> boolean","Fixed snapshot keyboard held state.",&key_contract},
    {"key_pressed",sc_lua_guard<keyboard<1>>,"key_pressed(name) -> boolean","Fixed snapshot keyboard press edge.",&key_contract},
    {"key_released",sc_lua_guard<keyboard<2>>,"key_released(name) -> boolean","Fixed snapshot keyboard release edge.",&key_contract},
    {"gamepad_down",sc_lua_guard<pad_button<0>>,"gamepad_down(name,slot?) -> boolean","Held button; optional stable slot 1..4, default selected controller.",&pad_contract},
    {"gamepad_pressed",sc_lua_guard<pad_button<1>>,"gamepad_pressed(name,slot?) -> boolean","Press edge, including taps between ticks.",&pad_contract},
    {"gamepad_released",sc_lua_guard<pad_button<2>>,"gamepad_released(name,slot?) -> boolean","Release edge, including device disconnection.",&pad_contract},
    {"gamepad_connected",sc_lua_guard<pad_connected>,"gamepad_connected(slot?) -> boolean","Read connection of selected controller or explicit slot 1..4.",&connected_contract},
    {"gamepad_axis",sc_lua_guard<pad_axis>,"gamepad_axis(name,deadzone?,slot?) -> number","Read normalized axis with deadzone in [0,1); default .2.",&axis_contract},
    {"focus_text",sc_lua_guard<focus_text>,"focus_text(x,y) / focus_text(false)","Set logical-screen IME candidate position or end text focus.",&focus_contract},
    {"clipboard",sc_lua_guard<clipboard>,"clipboard(text?) -> text?","Queue a clipboard write or read the recorded paste input for this tick.",&clipboard_contract},
    {"boundaries",sc_lua_guard<boundaries>,"boundaries(text) -> byte_offsets","UTF-8 grapheme starts plus end position; Lua one-based byte offsets.",&boundaries_contract},
    {"snapshot",sc_lua_guard<input_snapshot>,"snapshot() -> input","Fixed-tick input data including four pad slots, pointer and text.",&snapshot_contract},
    {"mouse",sc_lua_guard<mouse>,"mouse() -> x,y,inside","Pointer in logical viewport coordinates.",&mouse_contract},
    {"text",sc_lua_guard<text_input>,"text() -> committed,composition,cursor,selection_start,selection_end,segments,segments_truncated","Snapshot text, IME positions and bounded clause/conversion segments.",&input_text_contract},
    {"mouse_down",sc_lua_guard<mouse_button<0>>,"mouse_down(name) -> boolean","Mouse held state.",&mouse_button_contract},
    {"mouse_pressed",sc_lua_guard<mouse_button<1>>,"mouse_pressed(name) -> boolean","Mouse press edge.",&mouse_button_contract},
    {"mouse_released",sc_lua_guard<mouse_button<2>>,"mouse_released(name) -> boolean","Mouse release edge.",&mouse_button_contract},
    {"wheel",sc_lua_guard<wheel>,"wheel() -> x,y","Accumulated fixed-tick wheel deltas.",&wheel_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
} // namespace
void sc_script_input_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,input_api); lua_setfield(L,-2,"input");
}
void sc_script_input_describe() { sc_api_describe(input_api,"sc.input."); }

ScValue sc_script_input_contracts() {
    const auto field=[](std::string_view name,const char* type,const char* description) {
        return ScValue{ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"description",ScValue{std::string(description)}},{"required",ScValue{true}},{"readonly",ScValue{true}}}};
    };
    const auto type=[](ScValue::Array fields,const char* constraint) {
        return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},
            {"constraints",ScValue{ScValue::Array{ScValue{std::string(constraint)}}}}}};
    };
    ScValue::Array axes;
    for(const auto& axis:SC_AXES) {
        auto item=field(axis.name,"number","Raw normalized axis, before any deadzone.");
        auto& data=std::get<ScValue::Object>(item.data);
        data.emplace("minimum",ScValue{axis.id<4?-1.0:0.0}); data.emplace("maximum",ScValue{1.0});
        axes.push_back(std::move(item));
    }
    ScValue::Array pad={field("connected","boolean","Connected in this snapshot."),
        field("buttons","ScGamepadButton[]","Held button names in catalog order."),
        field("axes","ScInputAxes","All six axes, including zero values.")};
    auto selected=type(pad,"Selected-controller view; edges are snapshot.button_pressed/button_released.");
    pad.push_back(field("pressed","ScGamepadButton[]","Press edges; a quick tap may be both pressed and released."));
    pad.push_back(field("released","ScGamepadButton[]","Release edges, including disconnection releases."));
    ScValue::Array mouse_fields;
    for(const char* name:{"x","y","dx","dy"}) mouse_fields.push_back(field(name,"number","Logical viewport position or movement delta."));
    for(const char* name:{"wheel_x","wheel_y"}) mouse_fields.push_back(field(name,"number","Accumulated wheel delta."));
    mouse_fields.push_back(field("inside","boolean","False outside the viewport, including black bars."));
    for(const char* name:{"buttons","pressed","released"}) mouse_fields.push_back(field(name,"ScMouseButton[]","Mouse button names in catalog order."));
    ScValue::Array snapshot={
        field("keys","ScKey[]","Held keyboard keys in catalog order."),
        field("key_pressed","ScKey[]","Press edges, including taps between fixed updates."),
        field("key_released","ScKey[]","Release edges, including focus-loss releases."),
        field("gamepad","ScInputSelectedGamepad","Selected controller; use pads for explicit stable slots."),
        field("button_pressed","ScGamepadButton[]","Selected-controller press edges."),
        field("button_released","ScGamepadButton[]","Selected-controller release edges."),
        field("pads","ScInputPad[]","Exactly four records; Lua indices 1..4 are stable slots."),
        field("mouse","ScInputMouse","Pointer, button and wheel snapshot."),
        field("composition_edit","ScInputCompositionEdit","Resolved positions within composition, even if replay omitted metadata."),
        field("composition_segments","ScInputCompositionSegment[]","Ordered nonempty contiguous runs covering preedit; an empty array means unavailable metadata."),
        field("composition_segments_truncated","boolean","Native segmentation exceeded 128 runs; preserves preedit but clears segments.")};
    for(const char* name:{"text","composition","clipboard"}) {
        auto item=field(name,"string","UTF-8 committed text, preedit text or recorded paste payload; empty when absent.");
        std::get<ScValue::Object>(item.data).emplace("maximum_bytes",ScValue{4095.0}); snapshot.push_back(std::move(item));
    }
    return ScValue{ScValue::Object{
        {"ScInputAxes",type(std::move(axes),"Names match the gamepad_axes catalog; disconnected slots contain zero axes.")},
        {"ScInputSelectedGamepad",std::move(selected)},
        {"ScInputPad",type(std::move(pad),"A fixed controller slot, including press/release edges.")},
        {"ScInputMouse",type(std::move(mouse_fields),"Positions are logical viewport coordinates, not physical pixels or world coordinates.")},
        {"ScInputCompositionSegment",type({field("start","integer","One-based UTF-8 start; at a codepoint boundary."),
            field("finish","integer","Exclusive end, greater than start; at a codepoint boundary."),
            field("kind","'input'|'target_converted'|'converted'|'target_unconverted'|'error'|'fixed'","IME conversion status; adjacent same-kind runs can be separate clauses.")},
            "At most 128 segments. Nonempty arrays partition the entire composition without gaps or overlap; UI aligns presentation to whole graphemes.")},
        {"ScInputCompositionEdit",type({field("cursor","integer","One-based UTF-8 insertion position."),
            field("start","integer","Start of first contiguous target range."),field("finish","integer","Exclusive target end.")},
            "Positions are 1..#composition+1 at UTF-8 codepoint boundaries; start <= finish. Missing native metadata resolves to an empty range at the end.")},
        {"ScInputSnapshot",type(std::move(snapshot),"Independent plain data. In ui_update reads the presentation device snapshot; otherwise reads the current fixed-update snapshot. Real devices are isolated during replay.")}}};
}
