#include "shiny/core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <limits>

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"input:%d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    ScGamepadSelection selector;
    CHECK(selector.sample({false,false,true,true})==2);
    CHECK(selector.sample({true,false,true,true})==2); // Keep the active pad.
    CHECK(selector.sample({true,false,false,true})==-1);
    CHECK(selector.sample({true,false,false,true})==-1); // Render-only iterations cannot reconnect.
    selector.consumed(); CHECK(selector.sample({true,false,false,true})==0);
    CHECK(selector.sample({false,false,false,true})==-1);
    selector.consumed(); CHECK(selector.sample({false,false,false,true})==3);
    CHECK(sc_normalize_gamepad_axis(-1,true)==0);
    CHECK(sc_normalize_gamepad_axis(0,true)==.5f);
    CHECK(sc_normalize_gamepad_axis(1,true)==1);
    CHECK(sc_normalize_gamepad_axis(-2,false)==-1);
    CHECK(sc_normalize_gamepad_axis(2,false)==1);
    CHECK(sc_normalize_gamepad_axis(std::numeric_limits<float>::quiet_NaN(),true)==0);
    CHECK(!std::signbit(sc_normalize_gamepad_axis(-0.0f,false)));
    auto owner=std::make_unique<ScWorld>(); auto& w=*owner;
    sc_world_init(&w,42);
    ScInputBuffer buffer;
    for(const auto& key:SC_KEYS) {
        CHECK(sc_input_id(SC_KEYS,key.name)==key.id);
        auto id=static_cast<std::size_t>(key.id);
        ScDeviceInput in; in.keys.set(id); buffer.push(in); buffer.consume(&w);
        CHECK(w.input.keys[id]&&w.input.key_pressed[id]&&!w.input.key_released[id]);
        auto hash=sc_state_hash(&w);
        buffer.consume(&w); CHECK(!w.input.key_pressed.any()&&w.input.keys[id]);
        CHECK(sc_state_hash(&w)!=hash);
        in.key_pressed.set(id); buffer.push(in); buffer.consume(&w); // OS repeat is suppressed.
        CHECK(!w.input.key_pressed.any());
        buffer.push({}); buffer.consume(&w); CHECK(w.input.key_released[id]&&!w.input.keys.any());
    }
    CHECK(sc_input_id(SC_KEYS,"A")==-1);
    CHECK(sc_input_id(SC_BUTTONS,"a")==-1);
    for(const auto& button:SC_BUTTONS) {
        auto bit=1u<<button.id;
        ScDeviceInput in; in.connected=true; in.buttons=bit;
        buffer.push(in); buffer.consume(&w); CHECK(w.input.buttons==bit&&w.input.button_pressed==bit);
        buffer.consume(&w); CHECK(w.input.button_pressed==0);
        buffer.push({}); buffer.consume(&w); CHECK(!w.input.connected&&w.input.button_released==bit);
        buffer.consume(&w); CHECK(w.input.button_released==0);
    }
    auto space=static_cast<std::size_t>(sc_input_id(SC_KEYS,"space"));
    ScDeviceInput tap; tap.keys.set(space); buffer.push(tap); buffer.push({}); buffer.consume(&w);
    CHECK(!w.input.keys.any()&&w.input.key_pressed[space]&&w.input.key_released[space]);
    CHECK(w.held==0&&w.pressed==SC_JUMP&&w.released==SC_JUMP);
    buffer.consume(&w); CHECK(!w.pressed&&!w.released);
    ScDeviceInput queued; queued.key_pressed.set(space); // Backend queue reports an already completed tap.
    buffer.push(queued); buffer.consume(&w);
    CHECK(w.input.key_pressed[space]&&w.input.key_released[space]&&!w.input.keys[space]);
    CHECK(w.pressed==SC_JUMP&&w.released==SC_JUMP);
    buffer.consume(&w);
    buffer.push(tap); buffer.consume(&w);
    buffer.push({}); buffer.push(tap); buffer.consume(&w);
    CHECK(w.input.keys[space]&&w.input.key_pressed[space]&&w.input.key_released[space]);
    CHECK(w.pressed==SC_JUMP&&w.released==SC_JUMP);
    buffer.consume(&w); CHECK(!w.pressed&&!w.released);
    // Same action on two devices: releasing one does not release the action.
    tap.connected=true; tap.buttons=1u<<6; buffer.push(tap); buffer.consume(&w);
    CHECK(!w.pressed&&!w.released);
    tap.keys.reset(); buffer.push(tap); buffer.consume(&w);
    CHECK(w.held==SC_JUMP&&!w.released);
    buffer.push({}); buffer.consume(&w); CHECK(w.released==SC_JUMP);
    tap={}; tap.keys.set(space); buffer.push(tap); buffer.consume(&w);
    tap.connected=true; tap.buttons=1u<<6; buffer.push(tap);
    tap.keys.reset(); buffer.push(tap); buffer.consume(&w);
    CHECK(w.held==SC_JUMP&&!w.pressed&&!w.released); // Overlapping sources between ticks.
    buffer.push({}); buffer.consume(&w);
    // Deadzone boundaries and all axis endpoints, including positive-zero neutral.
    ScDeviceInput axes; axes.connected=true;
    for(const auto& axis:SC_AXES) {
        auto id=static_cast<std::size_t>(axis.id);
        axes.axes[id]=.2f; CHECK(sc_gamepad_axis(axes,axis.id,.2f)==0);
        axes.axes[id]=.6f; CHECK(std::fabs(sc_gamepad_axis(axes,axis.id,.2f)-.5f)<1e-6f);
        CHECK(sc_gamepad_axis(axes,axis.id,0)==.6f);
        axes.axes[id]=1; CHECK(sc_gamepad_axis(axes,axis.id,.2f)==1);
        if(axis.id<4) { axes.axes[id]=-1; CHECK(sc_gamepad_axis(axes,axis.id,.2f)==-1); }
    }
    axes.axes[1]=-.3f; CHECK(sc_device_actions(axes)&SC_UP);
    buffer.push(axes); buffer.consume(&w); buffer.push({}); buffer.consume(&w);
    for(auto axis:w.input.axes) CHECK(axis==0);
    CHECK(sc_gamepad_axis(w.input,0,0)==0);
    std::puts("input contracts passed");
}
