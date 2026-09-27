#include "core_contract.h"
#include "shiny/core.h"
#include <optional>
#include <string>
#include <utility>

namespace sc_core_api {
namespace {
const ScValue zero{0.0}, off{false}, empty{std::string{}}, deadzone{.2}, speed{30.0}, life{.5}, duration{.1}, volume{.2};
constexpr ScLuaParameter x{"x","number",true,"Finite world/screen X in pixels.",nullptr,-1e6,1e6};
constexpr ScLuaParameter y{"y","number",true,"Finite world/screen Y in pixels.",nullptr,-1e6,1e6};
constexpr ScLuaParameter color{"color","ScColor",true,"#RRGGBB or #RRGGBBAA string, not packed integer."};
const ScLuaParameter screen{"screen","boolean",false,"Logical viewport coordinates when true; omitted uses world coordinates. Explicit nil is rejected.",&off};
constexpr ScLuaParameter clip_parameters[]={
    {"x","number",false,"All four coordinates required together; no arguments pops.",nullptr,-1e6,1e6},
    {"y","number",false,"Screen Y.",nullptr,-1e6,1e6},
    {"w","number",false,"Clip width.",nullptr,0,1e6},{"h","number",false,"Clip height.",nullptr,0,1e6}};
const ScLuaParameter measure_parameters[]={
    {"text","string",true,"Valid UTF-8, at most 4096 bytes; no NUL."},
    {"size","number",true,"Logical pixel size.",nullptr,1,512},
    {"font","string|nil",false,"Declared font resource; empty/omitted/nil uses default font.",&empty},
    {"wrap","number|nil",false,"Maximum line width; zero/omitted/nil disables wrapping.",&zero,0,4096}};
constexpr ScLuaReturn measured[]={{"width","number","Logical width in pixels."},{"height","number","Logical height in pixels."}};
constexpr ScLuaParameter map_parameters[]={
    {"layer","string",true,"Existing finite Tiled tile-layer name, 1..128 bytes without NUL."},
    {"x","integer",true,"Zero-based cell column, inside map width.",nullptr,0,SC_MAX_TILES-1},
    {"y","integer",true,"Zero-based cell row, inside map height.",nullptr,0,SC_MAX_TILES-1},
    {"gid","integer",false,"Supplying a fourth argument writes; nil is rejected. Zero clears, otherwise a known Tiled GID with supported flip flags.",nullptr,0,UINT32_MAX}};
constexpr ScLuaParameter key_parameters[]={{"name","ScKey",true,"Exact key name from --api keys."}};
constexpr ScLuaParameter pad_parameters[]={{"name","ScGamepadButton",true,"Exact button name from --api gamepad_buttons; selected controller."}};
const ScLuaParameter axis_parameters[]={
    {"axis","ScGamepadAxis",true,"Selected controller axis; disconnected returns zero."},
    {"deadzone","number|nil",false,"Finite, representable as float below 1; rescales the remaining magnitude. Omitted/nil uses 0.2.",&deadzone,0,1,true}};
constexpr ScLuaParameter action_parameters[]={{"action","ScAction",true,"Built-in left/right/up/down/jump/action bit; distinct from Lua named actions."}};
constexpr ScLuaParameter tile_parameters[]={
    {"x","integer",true,"Zero-based cell column.",nullptr,-1e6,1e6},
    {"y","integer",true,"Zero-based cell row.",nullptr,-1e6,1e6},
    {"tile","ScTile",false,"Supplying the third argument writes an in-bounds cell; nil is rejected."}};
constexpr ScLuaParameter random_parameters[]={
    {"min","number",false,"Both bounds required together; no arguments uses [0,1).",nullptr,-1e6,1e6},
    {"max","number",false,"Must be >= min. Both Lua integers select an inclusive integer interval; otherwise floating interpolation.",nullptr,-1e6,1e6}};
const ScLuaParameter emit_parameters[]={x,y,
    {"count","integer",true,"Atomic burst, limited further by remaining project particle capacity.",nullptr,0,SC_MAX_PARTICLES},color,
    {"speed","number",false,"Omitted uses 30; explicit nil rejected.",&speed,0,1e6},
    {"life","number",false,"Seconds; omitted uses 0.5, explicit nil rejected.",&life,.001,60}};
const ScLuaParameter tone_parameters[]={
    {"frequency","number",true,"Hertz.",nullptr,20,20000},
    {"duration","number",false,"Seconds; omitted uses 0.1, explicit nil rejected.",&duration,.001,10},
    {"volume","number",false,"Gain; omitted uses 0.2, explicit nil rejected.",&volume,0,1}};
constexpr ScLuaParameter message_parameters[]={{"text","string",true,"At most 191 bytes without NUL; empty clears the room HUD message."}};
constexpr ScLuaParameter scene_parameters[]={
    {"path","string",true,"Project-relative .lua path, 1..511 bytes; no empty, dot or parent segments, backslashes or NUL."},
    {"state","table<string,ScData>",false,"Omitted inherits current shared state; supplied plain object replaces it on successful commit. Explicit nil rejected."}};
const ScLuaParameter rect_parameters[]={x,y,
    {"w","number",true,"Filled width.",nullptr,0,1e6},{"h","number",true,"Filled height.",nullptr,0,1e6},color,screen};
const ScLuaParameter circle_parameters[]={x,y,{"radius","number",true,"Circle radius.",nullptr,0,4096},color,screen};
const ScLuaParameter text_parameters[]={
    {"text","string",true,"Valid UTF-8, at most 511 bytes; no NUL."},x,y,
    {"size","number",true,"Logical pixel size.",nullptr,1,512},color,screen,
    {"options","ScTextOptions|nil",false,"Omitted/nil uses defaults. To pass options, provide an explicit screen boolean first."}};
constexpr ScLuaParameter log_parameters[]={{"text","string",true,"At most 4096 bytes without NUL; exactly one string, not Lua print-style varargs."}};
}
const ScLuaContract clip{clip_parameters,nullptr,ScLuaPhases::draw,"Exactly zero or four arguments; 32 nested clips. Balanced stack validated after draw; each push/pop consumes a draw command"};
const ScLuaContract measure{measure_parameters,nullptr,ScLuaPhases::read,nullptr,nullptr,"core",nullptr,ScLuaPhases::mutate,measured};
const ScLuaContract map{map_parameters,"integer",ScLuaPhases::read,"Finite Tiled cell access; failed terrain/navigation rebuild retains previous map",nullptr,"core","gid",ScLuaPhases::mutate,{},ScLuaMutationWhen::present};
const ScLuaContract objects{{},"table<string,ScData>[]",ScLuaPhases::read,"At most 16384 finite Tiled objects; raw fields retained, layer offsets applied to x/y. Empty without an imported map"};
const ScLuaContract key{key_parameters,"boolean",ScLuaPhases::read};
const ScLuaContract pad{pad_parameters,"boolean",ScLuaPhases::read};
const ScLuaContract connected{{},"boolean",ScLuaPhases::read};
const ScLuaContract axis{axis_parameters,"number",ScLuaPhases::read,"Selected controller; use sc.input.gamepad_axis for explicit slots"};
const ScLuaContract action{action_parameters,"boolean",ScLuaPhases::read,"Built-in action snapshot; press/release bits are zero in ui_update"};
const ScLuaContract tile{tile_parameters,"ScTile",ScLuaPhases::read,"Out-of-map reads: # for bounded rooms, . for unbounded streamed worlds",nullptr,"core","tile",ScLuaPhases::mutate,{},ScLuaMutationWhen::present};
const ScLuaContract random{random_parameters,"number",ScLuaPhases::mutate,"Exactly zero or two arguments; advances gameplay RNG, independent of particle/visual RNG"};
const ScLuaContract emit{emit_parameters,nullptr,ScLuaPhases::mutate,"project.limits.particles; failure preserves live particles and visual RNG"};
const ScLuaContract tone{tone_parameters,nullptr,ScLuaPhases::mutate,"32 queued tones per tick; no audio device required for validation"};
const ScLuaContract message{message_parameters,nullptr,ScLuaPhases::mutate};
const ScLuaContract scene{scene_parameters,nullptr,ScLuaPhases::mutate,"Optional state: 256 KiB, depth 16. Release async save and commit/cancel pending image changes first"};
const ScLuaContract rect{rect_parameters,nullptr,ScLuaPhases::draw,"project.limits.draws"};
const ScLuaContract circle{circle_parameters,nullptr,ScLuaPhases::draw,"project.limits.draws"};
const ScLuaContract text{text_parameters,nullptr,ScLuaPhases::draw,"project.limits.draws; font metrics shared with measure"};
const ScLuaContract tick{{},"integer",ScLuaPhases::read,"Completed room ticks; advances while app.pause is true, resets on room replacement"};
const ScLuaContract time{{},"number",ScLuaPhases::read,"Room tick / 60; not wall-clock or network time"};
const ScLuaContract log{log_parameters,nullptr,ScLuaPhases::read};
ScValue types() {
    using V=ScValue;V::Array fields;
    auto field=[](const char* name,const char* kind,bool required,const char* description,
                  std::optional<V> initial={},std::optional<double> minimum={},std::optional<double> maximum={}) {
        V::Object item{{"name",V{std::string(name)}},{"type",V{std::string(kind)}},
            {"required",V{required}},{"description",V{std::string(description)}}};
        if(initial) item.emplace("default",std::move(*initial));
        if(minimum) item.emplace("minimum",V{*minimum});
        if(maximum) item.emplace("maximum",V{*maximum});
        return V{std::move(item)};
    };
    auto type=[](V::Array items,const char* constraint) {
        return V{V::Object{{"fields",V{std::move(items)}},
            {"constraints",V{V::Array{V{std::string(constraint)}}}},
            {"unknown_fields",V{std::string("reject")}}}};
    };
    fields.emplace_back(V::Object{{"name",V{std::string("font")}},{"type",V{std::string("string")}},
        {"required",V{false}},{"default",V{std::string{}}},{"maximum_bytes",V{127.0}},
        {"description",V{std::string("Declared font name; empty uses default font. No NUL.")}}});
    fields.emplace_back(V::Object{{"name",V{std::string("wrap")}},{"type",V{std::string("number")}},
        {"required",V{false}},{"default",V{0.0}},{"minimum",V{0.0}},{"maximum",V{4096.0}},
        {"finite",V{true}},{"description",V{std::string("Logical maximum line width; zero disables wrapping.")}}});
    fields.emplace_back(V::Object{{"name",V{std::string("align")}},{"type",V{std::string("integer")}},
        {"required",V{false}},{"default",V{0.0}},{"minimum",V{0.0}},{"maximum",V{2.0}},
        {"description",V{std::string("0 left, 1 center, 2 right.")}}});
    V::Array constraints{V{std::string("Plain table; unknown fields and metatables rejected. Nil/omitted option fields retain defaults.")}};
    V::Object record{{"fields",V{std::move(fields)}},{"constraints",V{std::move(constraints)}},{"unknown_fields",V{std::string("reject")}}};
    return V{V::Object{{"ScTextOptions",V{std::move(record)}},
        {"ScMap",type({
            field("rows","string[]",true,"Dense equal-length ASCII rows of '.', '#' and '='; at least one row, at most 16384 cells."),
            field("tile_size","integer",false,"Pixels per cell.",V{8.0},1,256),
            field("color","ScColor",false,"Tile fill.",V{std::string("#183244FF")}),
            field("accent","ScColor",false,"Tile edge/accent.",V{std::string("#28566FFF")}),
            field("background","ScColor",false,"Scene background.",V{std::string("#070B19FF")})},
            "Plain table; unknown fields and metatables rejected. Zero-width rows are accepted, but every row must have the same byte length.")},
        {"ScScene",type({
            field("title","string",false,"Room title, at most 127 UTF-8 bytes.",V{std::string("ShinyCore")}),
            field("width","integer",false,"Logical viewport width.",V{384.0},64,4096),
            field("height","integer",false,"Logical viewport height.",V{216.0},64,4096),
            field("gravity","number",false,"Pixels per second squared; finite.",V{600.0},-1000000,1000000),
            field("ambient","number",false,"Ambient light; finite.",V{0.4},0,1),
            field("map","ScMap|string",false,"ASCII map or project-relative Tiled .tmj path; omitted uses an empty 48x27 map with solid virtual bounds."),
            field("entities","ScEntityPatch[]",false,"Dense initial entity array bounded by project.limits.entities; omitted is empty."),
            field("preload_images","string[]",false,"At most 128 distinct declared image names or paths; requires streaming; committed before first draw."),
            field("init","fun()",false,"Called once after scene fields and entities load; may initialize room state."),
            field("update","fun(dt: number)",false,"Fixed gameplay update before physics; dt is exactly 1/60 second."),
            field("draw","fun(alpha: number)",false,"Draw command submission; alpha is 0..1; gameplay mutation is forbidden."),
            field("ui_update","fun(dt: number)",false,"UI and device update before gameplay, including loading waits; dt is 0..0.25 second; gameplay mutation is forbidden.")},
            "Scene file returns a plain table; unknown fields and metatables rejected. Seed belongs to the host. Candidate load and init must not change active application services.")}}};
}
}
