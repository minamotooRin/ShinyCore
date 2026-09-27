#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>

// Stable engine names and IDs. No platform headers or device ownership.
struct ScInputName { std::string_view name; int id; };
inline constexpr ScInputName SC_KEYS[] = {
    {"apostrophe",39},
    {"comma",44},
    {"minus",45},
    {"period",46},
    {"slash",47},
    {"0",48},
    {"1",49},
    {"2",50},
    {"3",51},
    {"4",52},
    {"5",53},
    {"6",54},
    {"7",55},
    {"8",56},
    {"9",57},
    {"semicolon",59},
    {"equal",61},
    {"a",65},
    {"b",66},
    {"c",67},
    {"d",68},
    {"e",69},
    {"f",70},
    {"g",71},
    {"h",72},
    {"i",73},
    {"j",74},
    {"k",75},
    {"l",76},
    {"m",77},
    {"n",78},
    {"o",79},
    {"p",80},
    {"q",81},
    {"r",82},
    {"s",83},
    {"t",84},
    {"u",85},
    {"v",86},
    {"w",87},
    {"x",88},
    {"y",89},
    {"z",90},
    {"left_bracket",91},
    {"backslash",92},
    {"right_bracket",93},
    {"grave",96},
    {"space",32},
    {"escape",256},
    {"enter",257},
    {"tab",258},
    {"backspace",259},
    {"insert",260},
    {"delete",261},
    {"right",262},
    {"left",263},
    {"down",264},
    {"up",265},
    {"page_up",266},
    {"page_down",267},
    {"home",268},
    {"end",269},
    {"caps_lock",280},
    {"scroll_lock",281},
    {"num_lock",282},
    {"print_screen",283},
    {"pause",284},
    {"f1",290},
    {"f2",291},
    {"f3",292},
    {"f4",293},
    {"f5",294},
    {"f6",295},
    {"f7",296},
    {"f8",297},
    {"f9",298},
    {"f10",299},
    {"f11",300},
    {"f12",301},
    {"left_shift",340},
    {"left_control",341},
    {"left_alt",342},
    {"left_super",343},
    {"right_shift",344},
    {"right_control",345},
    {"right_alt",346},
    {"right_super",347},
    {"kb_menu",348},
    {"kp_0",320},
    {"kp_1",321},
    {"kp_2",322},
    {"kp_3",323},
    {"kp_4",324},
    {"kp_5",325},
    {"kp_6",326},
    {"kp_7",327},
    {"kp_8",328},
    {"kp_9",329},
    {"kp_decimal",330},
    {"kp_divide",331},
    {"kp_multiply",332},
    {"kp_subtract",333},
    {"kp_add",334},
    {"kp_enter",335},
    {"kp_equal",336},
};
inline constexpr ScInputName SC_BUTTONS[] = {
    {"dpad_up",0},
    {"dpad_right",1},
    {"dpad_down",2},
    {"dpad_left",3},
    {"north",4},
    {"east",5},
    {"south",6},
    {"west",7},
    {"left_shoulder",8},
    {"left_trigger",9},
    {"right_shoulder",10},
    {"right_trigger",11},
    {"back",12},
    {"guide",13},
    {"start",14},
    {"left_thumb",15},
    {"right_thumb",16},
};
inline constexpr ScInputName SC_AXES[] = {
    {"left_x",0},
    {"left_y",1},
    {"right_x",2},
    {"right_y",3},
    {"left_trigger",4},
    {"right_trigger",5},
};
template<std::size_t N> int sc_input_id(const ScInputName (&names)[N], std::string_view name) {
    for (const auto& entry:names) if(entry.name==name) return entry.id;
    return -1;
}
inline constexpr ScInputName SC_MOUSE_BUTTONS[] = {{"left",0},{"right",1},{"middle",2},{"side",3},{"extra",4}};
struct ScPadInput {
    bool connected{};
    std::uint32_t buttons{},pressed{},released{};
    std::array<float,6> axes{};
};
// One-based UTF-8 insertion positions; finish is exclusive. Zero means unavailable.
struct ScCompositionEdit { int cursor{},start{},finish{}; };
inline constexpr std::string_view SC_COMPOSITION_KINDS[]={"input","target_converted","converted","target_unconverted","error","fixed"};
struct ScCompositionSegment { std::uint16_t start{},finish{}; std::uint8_t kind{}; };
inline constexpr std::size_t SC_COMPOSITION_SEGMENTS=128;
struct ScDeviceInput {
    std::array<ScPadInput,4> pads{};
    float mouse_x{},mouse_y{},mouse_dx{},mouse_dy{},wheel_x{},wheel_y{};
    bool mouse_inside{};
    std::uint32_t mouse_buttons{},mouse_pressed{},mouse_released{};
    std::array<char,4096> text{},composition{},clipboard{};
    ScCompositionEdit composition_edit{};
    std::array<ScCompositionSegment,SC_COMPOSITION_SEGMENTS> composition_segments{};
    std::size_t composition_segment_count{};
    bool composition_segments_truncated{};

    std::bitset<512> keys{}, key_pressed{}, key_released{};
    bool connected{};
    std::uint32_t buttons{}, button_pressed{}, button_released{};
    std::array<float,6> axes{};
    void clear_edges() noexcept {
        key_pressed.reset(); key_released.reset(); button_pressed=button_released=0;
        mouse_pressed=mouse_released=0; mouse_dx=mouse_dy=wheel_x=wheel_y=0; text.fill(0); clipboard.fill(0);
        for(auto& pad:pads) pad.pressed=pad.released=0;
    }
};
ScCompositionEdit sc_composition_edit(const ScDeviceInput&) noexcept;
struct ScWorld;
std::uint32_t sc_device_actions(const ScDeviceInput& input);
float sc_gamepad_axis(const ScDeviceInput& input,int axis,float deadzone);
float sc_normalize_gamepad_axis(float value,bool trigger);
// Native backend owns selection; a disconnect is acknowledged only after a tick.
struct ScGamepadSelection {
    int slot{-1};
    bool disconnect_pending{};
    int sample(const std::array<bool,4>& available);
    void consumed() noexcept { disconnect_pending=false; }
};
// Host-owned accumulator: edges survive displayed frames without a simulation tick.
struct ScInputBuffer {
    ScDeviceInput pending{};
    std::uint32_t action_pressed{},action_released{};
    void push(const ScDeviceInput& sample);
    void consume(ScWorld* world);
};
