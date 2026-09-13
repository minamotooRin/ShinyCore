#include "shiny/core.h"
#include <algorithm>
#include <cmath>

int ScGamepadSelection::sample(const std::array<bool,4>& available) {
    if(slot>=0 && !available[static_cast<std::size_t>(slot)]) { slot=-1; disconnect_pending=true; }
    if(slot<0 && !disconnect_pending)
        for(std::size_t i=0;i<available.size();++i) if(available[i]) { slot=static_cast<int>(i); break; }
    return slot;
}
float sc_normalize_gamepad_axis(float value,bool trigger) {
    if(!std::isfinite(value)) return 0;
    value=std::clamp(value,-1.0f,1.0f);
    value=trigger?(value+1.0f)*.5f:value;
    return value==0?0:value;
}
std::uint32_t sc_device_actions(const ScDeviceInput& in) {
    auto key=[&](const char* name) { return in.keys[static_cast<std::size_t>(sc_input_id(SC_KEYS,name))]; };
    auto button=[&](int id) { return in.connected && (in.buttons & (1u<<id)); };
    std::uint32_t mask=0;
    if(key("left")||key("a")||button(3)||(in.connected&&in.axes[0]<-.25f)) mask|=SC_LEFT;
    if(key("right")||key("d")||button(1)||(in.connected&&in.axes[0]>.25f)) mask|=SC_RIGHT;
    if(key("up")||key("w")||button(0)||(in.connected&&in.axes[1]<-.25f)) mask|=SC_UP;
    if(key("down")||key("s")||button(2)||(in.connected&&in.axes[1]>.25f)) mask|=SC_DOWN;
    if(key("space")||key("z")||button(6)) mask|=SC_JUMP;
    if(key("e")||key("x")||button(5)) mask|=SC_INTERACT;
    return mask;
}
float sc_gamepad_axis(const ScDeviceInput& input,int axis,float deadzone) {
    if(!input.connected) return 0;
    float value=input.axes[static_cast<std::size_t>(axis)], magnitude=std::fabs(value);
    return magnitude<=deadzone ? 0 : std::copysign((magnitude-deadzone)/(1-deadzone),value);
}
namespace {
ScDeviceInput transitions(const ScDeviceInput& previous,ScDeviceInput next) {
    // Native callers must provide normalized finite axes. Disconnection always clears held state.
    if(!next.connected) { next.buttons=next.button_pressed=0; next.axes.fill(0); }
    next.key_pressed=(next.key_pressed & (~previous.keys | next.key_released)) | (next.keys & ~previous.keys);
    next.key_released|=(previous.keys | next.key_pressed) & ~next.keys;
    next.button_pressed=(next.button_pressed & (~previous.buttons | next.button_released)) | (next.buttons & ~previous.buttons);
    next.button_released|=(previous.buttons | next.button_pressed) & ~next.buttons;
    return next;
}
std::uint32_t pulses(const ScDeviceInput& input) {
    ScDeviceInput pulse{};
    pulse.keys=input.key_pressed; pulse.connected=input.connected; pulse.buttons=input.button_pressed;
    return sc_device_actions(pulse);
}
}
void ScInputBuffer::push(const ScDeviceInput& sample) {
    auto next=transitions(pending,sample);
    auto previous=sc_device_actions(pending), held=sc_device_actions(next);
    auto low=next; low.keys&=~next.key_pressed; low.buttons&=~next.button_pressed;
    auto remaining=sc_device_actions(low);
    auto short_press=pulses(next)&~remaining;
    action_pressed|=(held&~previous)|short_press;
    action_released|=(previous&~remaining)|(short_press&~held);
    next.key_pressed|=pending.key_pressed; next.key_released|=pending.key_released;
    next.button_pressed|=pending.button_pressed; next.button_released|=pending.button_released;
    pending=next;
}
void ScInputBuffer::consume(ScWorld* world) {
    // Edges were resolved in sample order. Re-deriving them from accumulated
    // device edges would invent an action release when two sources overlap.
    world->input=pending;
    sc_input(world,sc_device_actions(pending));
    world->pressed|=action_pressed; world->released|=action_released;
    pending.clear_edges(); action_pressed=action_released=0;
}
