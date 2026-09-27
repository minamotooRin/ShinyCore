#include "script_navigation.h"
#include "script_api.h"
#include "shiny/script.h"
#include "shiny/script_data.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

std::unique_ptr<ScNavigationRegion> sc_prepare_navigation_region(double x,double y,const ScValue& data,double tile,
                                                               std::span<const ScTerrainShape> shapes) {
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1e6||std::fabs(y)>1e6||!std::isfinite(tile)||tile<1||tile>256||std::floor(tile)!=tile)
        throw std::runtime_error("navigation region origin or cell size outside range");
    const auto* rows=std::get_if<ScValue::Array>(&data.data);
    if(!rows||rows->empty()||rows->size()>SC_MAX_TILES) throw std::runtime_error("navigation region requires nonempty rows");
    auto candidate=std::make_unique<ScNavigationRegion>();
    auto& map=candidate->map; map.height=static_cast<int>(rows->size()); map.width=0; map.tile_size=static_cast<int>(tile);
    candidate->x=static_cast<float>(x); candidate->y=static_cast<float>(y);
    for(std::size_t row=0;row<rows->size();++row) {
        const auto* text=std::get_if<std::string>(&(*rows)[row].data);
        if(!text||text->empty()||text->size()>SC_MAX_TILES/rows->size()) throw std::runtime_error("navigation region exceeds 16384 cells");
        if(row==0) map.width=static_cast<int>(text->size());
        if(text->size()!=static_cast<std::size_t>(map.width)) throw std::runtime_error("navigation region rows require equal width");
        for(std::size_t col=0;col<text->size();++col) {
            const char value=(*text)[col];
            if(value!='.'&&value!='#') throw std::runtime_error("navigation region cells require . or #");
            map.tiles[row*text->size()+col]=value;
        }
    }
    if(std::fabs(x+map.width*tile)>1e6||std::fabs(y+map.height*tile)>1e6) throw std::runtime_error("navigation region endpoint outside range");
    map.navigation_blocked=sc_navigation_obstacles(map,shapes,candidate->x,candidate->y);
    return candidate;
}

namespace {
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"navigation mutation requires load, init or update"); }
void arguments(lua_State* L,int minimum,int maximum) {
    if(lua_gettop(L)<minimum||lua_gettop(L)>maximum) luaL_error(L,"unexpected navigation argument count");
}
lua_Integer integer(lua_State* L,int index) {
    if(lua_type(L,index)!=LUA_TNUMBER) luaL_error(L,"navigation argument must be an integer");
    return luaL_checkinteger(L,index);
}
double number(lua_State* L,int index) {
    if(lua_type(L,index)!=LUA_TNUMBER) luaL_error(L,"navigation argument must be a number");
    return lua_tonumber(L,index);
}
float radius(lua_State* L,int index) {
    const double value=lua_isnoneornil(L,index)?0:number(L,index);
    if(!std::isfinite(value)||value<0||value>4096) luaL_error(L,"navigation radius outside 0..4096 pixels");
    return static_cast<float>(value);
}
lua_Integer optional_integer(lua_State* L,int index,lua_Integer initial) {
    return lua_isnoneornil(L,index)?initial:integer(L,index);
}
void read(lua_State* L,int index) {
    auto value=sc_lua_read(L,index); if(!value) throw std::invalid_argument(value.error()); script(L)->scratch=std::move(*value);
}
const ScMap& navigation_map(const ScScript* s) { return s->navigation_region?s->navigation_region->map:s->world->map; }
std::pair<float,float> navigation_direction(const ScScript* s,const ScFlowField* field,float x,float y) {
    if(s->navigation_region) { x-=s->navigation_region->x; y-=s->navigation_region->y; }
    return field->direction(navigation_map(s),x,y);
}
int navigation_region(lua_State* L) {
    mutable_phase(L); auto* s=script(L);
    if(lua_gettop(L)==0) {
        s->navigation_region.reset(); for(auto& field:s->flow_fields) field.reset(); return 0;
    }
    arguments(L,3,4);
    const double x=number(L,1),y=number(L,2);
    const auto tile=optional_integer(L,4,8);
    read(L,3);
    s->navigation_region=sc_prepare_navigation_region(x,y,s->scratch,static_cast<double>(tile),s->world->terrain_shapes);
    for(auto& field:s->flow_fields) field.reset();
    return 0;
}
int path(lua_State* L) {
    arguments(L,4,6);
    const auto sx=integer(L,1),sy=integer(L,2);
    const auto gx=integer(L,3),gy=integer(L,4);
    auto budget=optional_integer(L,5,16384); auto* s=script(L); const auto& map=navigation_map(s);
    const float body_radius=radius(L,6);
    if(sx<0||sy<0||gx<0||gy<0||sx>=map.width||gx>=map.width||sy>=map.height||gy>=map.height||budget<1||budget>1048576)
        return luaL_error(L,"navigation coordinates or budget outside range");
    {
        auto result=sc_path(map,static_cast<int>(sy*map.width+sx),static_cast<int>(gy*map.width+gx),static_cast<std::size_t>(budget),body_radius); ScValue::Array points;
        for(int cell:result.cells) points.push_back(ScValue{ScValue::Object{{"x",ScValue{double(cell%map.width)}},{"y",ScValue{double(cell/map.width)}}}});
        s->scratch=ScValue{ScValue::Object{{"status",ScValue{std::string(result.status)}},{"visited",ScValue{double(result.visited)}},{"points",ScValue{std::move(points)}}}};
    }
    sc_lua_push(L,s->scratch); return 1;
}
int mask(lua_State* L) {
    arguments(L,0,1);
    const auto body_radius=radius(L,1);
    auto* s=script(L); const auto& map=navigation_map(s);
    {
        const auto blocked=sc_navigation_blocked(map,body_radius);
        ScValue::Array rows;
        rows.reserve(static_cast<std::size_t>(map.height));
        for(int y=0;y<map.height;++y) {
            std::string row(static_cast<std::size_t>(map.width),'.');
            for(int x=0;x<map.width;++x)
                if(blocked[static_cast<std::size_t>(y*map.width+x)]) row[static_cast<std::size_t>(x)]='#';
            rows.push_back(ScValue{std::move(row)});
        }
        const double origin_x=s->navigation_region?s->navigation_region->x:0;
        const double origin_y=s->navigation_region?s->navigation_region->y:0;
        s->scratch=ScValue{ScValue::Object{{"x",ScValue{origin_x}},{"y",ScValue{origin_y}},
            {"cell_size",ScValue{double(map.tile_size)}},{"width",ScValue{double(map.width)}},
            {"height",ScValue{double(map.height)}},{"radius",ScValue{double(body_radius)}},
            {"rows",ScValue{std::move(rows)}}}};
    }
    sc_lua_push(L,s->scratch); return 1;
}
int flow(lua_State* L) {
    arguments(L,2,5);
    mutable_phase(L); auto* s=script(L); const auto& map=navigation_map(s);
    auto x=integer(L,1),y=integer(L,2);
    auto budget=optional_integer(L,3,16384),slot=optional_integer(L,4,1);
    const float body_radius=radius(L,5);
    if(x<0||y<0||x>=map.width||y>=map.height||budget<1||budget>1048576||slot<1||slot>16)
        return luaL_error(L,"flow coordinates, budget or slot outside range");
    auto& field=s->flow_fields[static_cast<size_t>(slot-1)];
    auto& generation=s->flow_generations[static_cast<size_t>(slot-1)];
    if(generation==0x07ffffffu) return luaL_error(L,"flow field generation exhausted");
    {
        auto candidate=std::make_unique<ScFlowField>();
        candidate->build(map,static_cast<int>(y*map.width+x),static_cast<size_t>(budget),body_radius);
        field=std::move(candidate); ++generation;
    }
    lua_pushinteger(L,(static_cast<lua_Integer>(s->world->epoch)<<32)|(static_cast<lua_Integer>(generation)<<5)|slot);
    lua_pushlstring(L,field->status.data(),field->status.size());
    lua_pushinteger(L,static_cast<lua_Integer>(field->visited)); return 3;
}
ScFlowField* flow_at(lua_State* L,int argument) {
    auto id=integer(L,argument); auto* s=script(L); auto slot=id&31;
    if(id<=0||id>0xfffffffffffffLL||(id>>32)!=s->world->epoch||slot<1||slot>16||
       !s->flow_fields[static_cast<size_t>(slot-1)]||
       ((id>>5)&0x07ffffff)!=s->flow_generations[static_cast<size_t>(slot-1)])
        luaL_error(L,"invalid or stale flow field handle");
    return s->flow_fields[static_cast<size_t>(slot-1)].get();
}
int flow_direction(lua_State* L) {
    arguments(L,3,3);
    auto* field=flow_at(L,1); double x=number(L,2),y=number(L,3);
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>1e6||std::fabs(y)>1e6) return luaL_error(L,"flow position outside range");
    auto [dx,dy]=navigation_direction(script(L),field,static_cast<float>(x),static_cast<float>(y));
    lua_pushnumber(L,dx); lua_pushnumber(L,dy);
    auto status=field->state(navigation_map(script(L)));
    lua_pushlstring(L,status.data(),status.size()); return 3;
}
int flow_refresh(lua_State* L) {
    arguments(L,2,2);
    mutable_phase(L); auto* field=flow_at(L,1); auto budget=integer(L,2);
    if(budget<1||budget>1048576) return luaL_error(L,"flow budget outside 1..1048576");
    field->refresh(navigation_map(script(L)),static_cast<size_t>(budget));
    lua_pushlstring(L,field->status.data(),field->status.size());
    lua_pushinteger(L,static_cast<lua_Integer>(field->visited)); return 2;
}
int steer(lua_State* L) {
    arguments(L,3,3);
    mutable_phase(L); auto* field=flow_at(L,1); auto* s=script(L);
    double speed=number(L,3);
    if(!std::isfinite(speed)||speed<0||speed>1e6) return luaL_error(L,"steering speed outside range");
    luaL_checktype(L,2,LUA_TTABLE); const size_t length=lua_rawlen(L,2);
    if(length>s->world->entities.size()) return luaL_error(L,"steering batch exceeds entity capacity");
    if(lua_getmetatable(L,2)) return luaL_error(L,"steering entities require a plain dense array");
    size_t count=0;
    lua_pushnil(L);
    while(lua_next(L,2)) {
        if(!lua_isinteger(L,-2)||lua_tointeger(L,-2)<1||static_cast<lua_Unsigned>(lua_tointeger(L,-2))>length)
            return luaL_error(L,"steering entities require a dense array");
        ++count; lua_pop(L,1);
    }
    if(count!=length) return luaL_error(L,"steering entities require a dense array");
    s->batch_seen.reset();
    // Validate the complete batch before changing any velocity.
    s->batch_entities.resize(length);
    for(size_t i=0;i<length;++i) {
        lua_rawgeti(L,2,static_cast<lua_Integer>(i+1)); auto id=integer(L,-1); lua_pop(L,1);
        auto* entity=sc_entity(s->world,static_cast<ScEntityId>(id));
        if(!entity) return luaL_error(L,"invalid entity in steering batch");
        const auto slot=sc_entity_slot(entity->id);
        if(s->batch_seen.test(slot)) return luaL_error(L,"duplicate entity in steering batch");
        s->batch_seen.set(slot);
        s->batch_entities[i]=*entity;
    }
    for(const auto& value:s->batch_entities) {
        auto* entity=sc_entity(s->world,value.id);
        auto [dx,dy]=navigation_direction(s,field,entity->x+entity->w*.5f,entity->y+entity->h*.5f);
        entity->vx=dx*static_cast<float>(speed); entity->vy=dy*static_cast<float>(speed);
    }
    lua_pushinteger(L,static_cast<lua_Integer>(length)); return 1;
}

const ScValue default_budget{16384.0},default_slot{1.0},default_cell{8.0},default_radius{0.0};
constexpr const char* query_status="'ok'|'unreachable'|'budget_exhausted'";
constexpr const char* field_status="'ok'|'unreachable'|'budget_exhausted'|'stale'";
constexpr ScLuaParameter region_parameters[]={
    {"x","number",false,"World-pixel origin; x,y,rows are required together. No arguments restores the room grid.",nullptr,-1e6,1e6},
    {"y","number",false,"World-pixel origin.",nullptr,-1e6,1e6},
    {"rows","string[]",false,"Nonempty equal-width . / # rows, at most 16384 cells; endpoints must stay within +/-1000000 pixels."},
    {"cell_size","integer",false,"World pixels per cell.",&default_cell,1,256}};
constexpr ScLuaParameter path_parameters[]={
    {"sx","integer",true,"Zero-based start column within the selected grid."},
    {"sy","integer",true,"Zero-based start row within the selected grid."},
    {"gx","integer",true,"Zero-based goal column within the selected grid."},
    {"gy","integer",true,"Zero-based goal row within the selected grid."},
    {"budget","integer",false,"Maximum expanded nodes in this query.",&default_budget,1,1048576},
    {"radius","number",false,"Circular body clearance in world pixels at path cell centers. Zero uses point navigation.",&default_radius,0,4096}};
constexpr ScLuaParameter mask_parameters[]={
    {"radius","number",false,"Circular body clearance in world pixels; use the same value as path/flow.",&default_radius,0,4096}};
constexpr ScLuaParameter flow_parameters[]={
    {"gx","integer",true,"Zero-based goal column within the selected grid."},
    {"gy","integer",true,"Zero-based goal row within the selected grid."},
    {"budget","integer",false,"Maximum initial BFS expansions.",&default_budget,1,1048576},
    {"slot","integer",false,"Replace this room-owned slot; successful replacement invalidates its old handle.",&default_slot,1,16},
    {"radius","number",false,"Circular body clearance in world pixels, retained by refresh; share fields between same-size agents.",&default_radius,0,4096}};
constexpr ScLuaParameter direction_parameters[]={
    {"handle","integer",true,"Live flow handle returned by flow, not a slot number."},
    {"x","number",true,"World-pixel position, including a selected region's origin.",nullptr,-1e6,1e6},
    {"y","number",true,"World-pixel position.",nullptr,-1e6,1e6}};
constexpr ScLuaParameter refresh_parameters[]={
    {"handle","integer",true,"Live flow handle; refresh preserves its generation."},
    {"budget","integer",true,"Maximum additional expansions this call.",nullptr,1,1048576}};
constexpr ScLuaParameter steer_parameters[]={
    {"handle","integer",true,"Live flow handle."},
    {"entities","ScEntityId[]",true,"Dense plain array of distinct live entities. Empty is valid; errors change no velocity."},
    {"speed","number",true,"World pixels per second; both velocity components are replaced.",nullptr,0,1e6}};
constexpr ScLuaReturn flow_results[]={
    {"handle","integer","Generation-checked room handle, lossless within 52 bits; never persist it."},
    {"status",query_status,"A blocked goal produces unreachable with zero visits."},
    {"visited","integer","Cumulative nodes expanded in this build."}};
constexpr ScLuaReturn direction_results[]={
    {"dx","number","Normalized X direction, or zero if no published direction exists."},
    {"dy","number","Normalized Y direction."},
    {"status",field_status,"Global field status; ok may still yield zero at the goal, outside the grid, or in a disconnected cell."}};
constexpr ScLuaReturn refresh_results[]={
    {"status",query_status,"A stale field restarts; a partial build resumes; an unchanged finished build does no work."},
    {"visited","integer","Cumulative since the last restart, at most the number of grid cells."}};
constexpr ScLuaContract region_contract{region_parameters,nullptr,ScLuaPhases::mutate,"16384 cells"};
constexpr ScLuaContract path_contract{path_parameters,"ScNavigationPath",ScLuaPhases::read,"16384 cells"};
constexpr ScLuaContract mask_contract{mask_parameters,"ScNavigationMask",ScLuaPhases::read,"16384 cells; explicit snapshot only"};
constexpr ScLuaContract flow_contract{.parameters=flow_parameters,.result=nullptr,.phases=ScLuaPhases::mutate,
    .capacity="16 flow slots; 134217727 generations per slot per room, no wrap",.results=flow_results};
constexpr ScLuaContract direction_contract{.parameters=direction_parameters,.result=nullptr,.phases=ScLuaPhases::read,.results=direction_results};
constexpr ScLuaContract refresh_contract{.parameters=refresh_parameters,.result=nullptr,.phases=ScLuaPhases::mutate,.results=refresh_results};
constexpr ScLuaContract steer_contract{steer_parameters,"integer",ScLuaPhases::mutate,"project.limits.entities"};
const ScLuaApi api[]={
    {"region",sc_lua_guard<navigation_region>,"region() / region(x,y,rows,cell_size?)","Atomically select a local navigation grid; terrain overlaps block cells. No arguments restores the room grid. Every successful replacement invalidates existing flow handles.",&region_contract},
    {"path",sc_lua_guard<path>,"path(sx,sy,gx,gy,budget?,radius?) -> result","Deterministic four-neighbor A-star with optional circular clearance from blocked cells and grid edges. Blocked endpoints return unreachable; out-of-bounds coordinates error. Returned paths are snapshots.",&path_contract},
    {"mask",sc_lua_guard<mask>,"mask(radius?) -> result","Read the selected grid's current walkability after terrain and optional body clearance. Rows are . for passable and # for blocked; this allocates a bounded snapshot only when called.",&mask_contract},
    {"flow",sc_lua_guard<flow>,"flow(gx,gy,budget?,slot?,radius?) -> handle,status,visited","Build a shared target field with optional circular clearance. Each successful slot replacement creates a new handle; invalid arguments or failed preparation retain the old field.",&flow_contract},
    {"direction",sc_lua_guard<flow_direction>,"direction(handle,x,y) -> dx,dy,status","Read world-pixel steering without advancing the search. Stale or incomplete fields, blocked/unreachable cells and outside positions return zero direction; stale handles error.",&direction_contract},
    {"refresh",sc_lua_guard<flow_refresh>,"refresh(handle,budget) -> status,visited","Restart after passability changes or resume bounded BFS. Only a complete field publishes directions. Node budget is not a CPU time limit.",&refresh_contract},
    {"steer",sc_lua_guard<steer>,"steer(handle,entities,speed) -> count","Validate the entire distinct-entity batch, then steer each entity center. Unpublished fields set velocities to zero. Apply each update; invalidation alone does not stop entities.",&steer_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
} // namespace

void sc_script_navigation_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"navigation");
}
void sc_script_navigation_describe() { sc_api_describe(api,"sc.navigation."); }
ScValue sc_script_navigation_contracts() {
    auto field=[](const char* name,const char* type,const char* description) {
        return ScValue{ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"description",ScValue{std::string(description)}},{"required",ScValue{true}},{"readonly",ScValue{true}}}};
    };
    auto type=[](ScValue::Array fields,const char* constraint) {
        return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},
            {"constraints",ScValue{ScValue::Array{ScValue{std::string(constraint)}}}}}};
    };
    return ScValue{ScValue::Object{
        {"ScNavigationPoint",type({field("x","integer","Zero-based grid column."),field("y","integer","Zero-based grid row.")},
            "Points use local grid cells, not world pixels; region origin and cell size must be applied for display.")},
        {"ScNavigationPath",type({field("status",query_status,"Search outcome."),
            field("visited","integer","Expanded node count; blocked endpoints visit zero nodes."),
            field("points","ScNavigationPoint[]","Inclusive start-to-goal sequence on success; empty for unreachable or exhausted queries.")},
            "Independent snapshot. Stable four-neighbor A-star ties use cost and cell order; queries do not mutate the grid or flow fields.")},
        {"ScNavigationMask",type({field("x","number","World-pixel origin of the selected grid."),
            field("y","number","World-pixel origin of the selected grid."),
            field("cell_size","integer","World pixels per cell."),
            field("width","integer","Columns per row."),field("height","integer","Number of rows."),
            field("radius","number","Requested circular body clearance in world pixels."),
            field("rows","string[]","Equal-width . passable / # blocked rows in local grid order.")},
            "Read-only independent snapshot of the same clearance mask used by path and flow; terrain edits require a fresh call.")}}};
}
