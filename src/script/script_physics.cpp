#include "script_api.h"
#include "shiny/script_physics.h"
#include "shiny/script_data.h"
#include "shiny/physics.h"
#include <lua.hpp>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
namespace {
void compound(const ScValue& value,ScEntity* entity) {
    const auto* shapes=std::get_if<ScValue::Array>(&value.data);
    if(!shapes||shapes->empty()||shapes->size()>4) throw std::runtime_error("body.shapes requires 1..4 shapes");
    entity->shape_count=static_cast<int>(shapes->size());
    for(size_t i=0;i<shapes->size();++i) {
        auto& out=entity->shapes[i]; out={};
        const auto* object=std::get_if<ScValue::Object>(&(*shapes)[i].data);
        if(!object) throw std::runtime_error("shape must be an object");
        for(const auto& [key,v]:*object) {
            if(key=="shape") {
                auto name=v.text(); out.kind=name=="box"?0:name=="circle"?1:name=="capsule"?2:name=="polygon"?3:-1;
                if(out.kind<0) throw std::runtime_error("unknown compound shape");
            } else if(key=="x"||key=="y"||key=="w"||key=="h") {
                auto n=std::get_if<double>(&v.data);
                if(!n||*n>4096||*n<(key=="w"||key=="h"?.001:-4096)) throw std::runtime_error("invalid compound bounds");
                if(key=="x") out.x=static_cast<float>(*n); else if(key=="y") out.y=static_cast<float>(*n);
                else if(key=="w") out.w=static_cast<float>(*n); else out.h=static_cast<float>(*n);
            } else if(key=="vertices") {
                const auto* points=std::get_if<ScValue::Array>(&v.data);
                if(!points||points->size()<6||points->size()>16||points->size()%2) throw std::runtime_error("compound polygon requires 3..8 points");
                out.vertex_count=static_cast<int>(points->size()/2);
                for(size_t j=0;j<points->size();++j) {
                    auto n=std::get_if<double>(&(*points)[j].data); if(!n||std::fabs(*n)>4096) throw std::runtime_error("invalid compound vertex");
                    out.vertices[j]=static_cast<float>(*n);
                }
            } else throw std::runtime_error("unknown compound shape field: "+key);
        }
        if(out.kind==3&&out.vertex_count<3) throw std::runtime_error("compound polygon needs vertices");
    }
}
ScScript* script(lua_State* L) { return *static_cast<ScScript**>(lua_getextraspace(L)); }
void arguments(lua_State* L,int minimum,int maximum) {
    if(lua_gettop(L)<minimum||lua_gettop(L)>maximum) luaL_error(L,"invalid physics argument count");
}
lua_Integer handle(lua_State* L,int index) {
    int valid=0; auto id=lua_tointegerx(L,index,&valid);
    if(lua_type(L,index)!=LUA_TNUMBER||!valid||id<=0||id>static_cast<lua_Integer>(SC_ID_MAX))
        luaL_error(L,"expected a positive physics handle within 52 bits");
    return id;
}
float number(lua_State* L,int index,float lo=-1e6f,float hi=1e6f) {
    if(lua_type(L,index)!=LUA_TNUMBER) luaL_error(L,"expected number");
    auto n=lua_tonumber(L,index); if(!std::isfinite(n)||n<lo||n>hi) luaL_error(L,"number outside range"); return static_cast<float>(n);
}
ScEntity* entity(lua_State* L,int index) {
    auto id=handle(L,index);
    auto* e=sc_entity(script(L)->world,static_cast<ScEntityId>(id));
    if(!e) luaL_error(L,"stale or unknown entity");
    return e;
}
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"physics requires load, init or update"); }
void id_field(lua_State* L,const char* key,ScEntityId value) { lua_pushinteger(L,static_cast<lua_Integer>(value)); lua_setfield(L,-2,key); }
void field(lua_State* L,const char* key,double value) { lua_pushnumber(L,value); lua_setfield(L,-2,key); }
int apply_force(lua_State* L,bool impulse) {
    arguments(L,3,3); mutable_phase(L); auto* e=entity(L,1); float x=number(L,2),y=number(L,3);
    if(impulse) { e->impulse_x+=x; e->impulse_y+=y; }
    else { e->force_x+=x; e->force_y+=y; } return 0;
}
int force(lua_State* L) { return apply_force(L,false); }
int impulse(lua_State* L) { return apply_force(L,true); }
int drop(lua_State* L) { arguments(L,1,2); mutable_phase(L); auto* e=entity(L,1); e->drop_time=lua_isnoneornil(L,2)?.2f:number(L,2,0,10); return 0; }
int contacts(lua_State* L) {
    arguments(L,0,0);
    auto* w=script(L)->world; lua_createtable(L,w->contact_count,0);
    for(int i=0;i<w->contact_count;++i) {
        const auto& c=w->contacts[i]; lua_createtable(L,0,5);
        id_field(L,"a",c.a); id_field(L,"b",c.b); field(L,"nx",c.nx); field(L,"ny",c.ny);
        lua_pushboolean(L,c.sensor); lua_setfield(L,-2,"sensor");
        lua_pushstring(L,c.phase==1?"begin":c.phase==2?"end":"contact"); lua_setfield(L,-2,"phase"); lua_rawseti(L,-2,i+1);
    } return 1;
}
int push_hit(lua_State* L,const ScRay& hit) {
    if(!hit.hit) { lua_pushnil(L); return 1; }
    lua_createtable(L,0,6); id_field(L,"id",hit.id); field(L,"x",hit.x); field(L,"y",hit.y); field(L,"nx",hit.nx); field(L,"ny",hit.ny); field(L,"fraction",hit.fraction); return 1;
}
int ray(lua_State* L) {
    arguments(L,4,4); mutable_phase(L); float x=number(L,1),y=number(L,2),dx=number(L,3),dy=number(L,4); ScRay hit; bool ok=false;
    try { hit=sc_physics_ray(script(L)->world,x,y,dx,dy); ok=true; }
    catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); }
    if(!ok) return luaL_error(L,"physics: %s",script(L)->error);
    return push_hit(L,hit);
}
ScQueryShape read_shape(lua_State* L) {
    luaL_checktype(L,1,LUA_TTABLE);
    if(lua_getmetatable(L,1)) luaL_error(L,"query points require a plain table");
    auto size=lua_rawlen(L,1);
    if(size<2||size>16||size%2) luaL_error(L,"query points require 1..8 x,y pairs");
    lua_pushnil(L);
    while(lua_next(L,1)) {
        if(!lua_isinteger(L,-2)||lua_tointeger(L,-2)<1||static_cast<lua_Unsigned>(lua_tointeger(L,-2))>size)
            luaL_error(L,"query points require a dense array without named fields");
        lua_pop(L,1);
    }
    ScQueryShape shape; shape.count=static_cast<int>(size/2);
    for(std::size_t i=0;i<size;++i) {
        lua_rawgeti(L,1,static_cast<lua_Integer>(i+1)); shape.points[i]=number(L,-1); lua_pop(L,1);
    }
    shape.radius=number(L,2,0,4096); return shape;
}
int overlap_shape(lua_State* L,const ScQueryShape& shape) {
    std::span<const ScEntityId> ids; bool ok=false;
    try { ids=sc_physics_overlap(script(L)->world,shape); ok=true; }
    catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); }
    if(!ok) return luaL_error(L,"physics: %s",script(L)->error);
    lua_createtable(L,static_cast<int>(ids.size()),0);
    for(std::size_t i=0;i<ids.size();++i) { lua_pushinteger(L,static_cast<lua_Integer>(ids[i])); lua_rawseti(L,-2,static_cast<lua_Integer>(i+1)); }
    return 1;
}
int overlap(lua_State* L) {
    arguments(L,2,2); mutable_phase(L); auto shape=read_shape(L); return overlap_shape(L,shape);
}
int sweep(lua_State* L) {
    arguments(L,4,4); mutable_phase(L); auto shape=read_shape(L); float dx=number(L,3),dy=number(L,4); ScRay hit; bool ok=false;
    try { hit=sc_physics_sweep(script(L)->world,shape,dx,dy); ok=true; }
    catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); }
    if(!ok) return luaL_error(L,"physics: %s",script(L)->error);
    return push_hit(L,hit);
}
int query(lua_State* L) {
    arguments(L,4,5); mutable_phase(L); float x=number(L,1),y=number(L,2),w=number(L,3,0),h=number(L,4,0);
    float angle=lua_isnoneornil(L,5)?0:number(L,5);
    if(w==0||h==0) { lua_newtable(L); return 1; }
    ScQueryShape shape; shape.count=4;
    float c=std::cos(angle),s=std::sin(angle);
    const float px[]={-w/2,w/2,w/2,-w/2},py[]={-h/2,-h/2,h/2,h/2};
    for(int i=0;i<4;++i) { shape.points[2*i]=x+w/2+c*px[i]-s*py[i]; shape.points[2*i+1]=y+h/2+s*px[i]+c*py[i]; }
    return overlap_shape(L,shape);
}
int joint(lua_State* L) {
    arguments(L,5,8); mutable_phase(L);
    luaL_checktype(L,1,LUA_TSTRING); size_t name_size=0; const char* name=lua_tolstring(L,1,&name_size);
    const std::string_view type{name,name_size};
    int kind=type=="distance"?0:type=="revolute"?1:type=="prismatic"?2:-1;
    if(kind<0) return luaL_error(L,"unknown joint type");
    auto a=entity(L,2)->id,b=entity(L,3)->id;
    float x=number(L,4),y=number(L,5),length=lua_isnoneornil(L,6)?8:number(L,6,.01f,4096); ScJointId id=0;
    std::optional<std::array<float,2>> anchor_b;
    if(!lua_isnoneornil(L,7)||!lua_isnoneornil(L,8)) {
        if(kind!=0) return luaL_error(L,"second anchor is only supported for distance joints");
        anchor_b=std::array{number(L,7),number(L,8)};
    }
    try { id=sc_physics_joint(script(L)->world,kind,a,b,x,y,length,anchor_b); }
    catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); }
    if(!id) return luaL_error(L,"cannot create joint: bodies required or capacity exhausted");
    lua_pushinteger(L,static_cast<lua_Integer>(id)); return 1;
}
int unjoint(lua_State* L) {
    arguments(L,1,1); mutable_phase(L); auto id=handle(L,1);
    if(!sc_physics_joint_destroy(script(L)->world,static_cast<ScJointId>(id))) return luaL_error(L,"stale joint ID");
    return 0;
}
int joint_control(lua_State* L) {
    arguments(L,1,2); mutable_phase(L); auto id=handle(L,1);
    ScJointControl c; bool ok=false;
    try {
        auto current=sc_physics_joint_control(script(L)->world,static_cast<ScJointId>(id));
        if(!current) throw std::runtime_error(current.error());
        c=*current;
        if(!lua_isnoneornil(L,2)) {
            auto value=sc_lua_read(L,2); if(!value) throw std::runtime_error(value.error());
            auto* object=std::get_if<ScValue::Object>(&value->data);
            if(!object) throw std::runtime_error("joint control requires an object");
            for(const auto& [key,v]:*object) {
                if(key=="limit"||key=="motor") {
                    auto b=std::get_if<bool>(&v.data); if(!b) throw std::runtime_error("joint control flag requires boolean");
                    if(key=="limit") c.limit=*b; else c.motor=*b;
                } else {
                    auto n=std::get_if<double>(&v.data);
                    if(!n||!std::isfinite(*n)||std::fabs(*n)>1e9) throw std::runtime_error("joint control requires finite number");
                    float f=static_cast<float>(*n);
                    if(key=="lower") c.lower=f; else if(key=="upper") c.upper=f;
                    else if(key=="speed") c.speed=f; else if(key=="max_effort") c.max_effort=f;
                    else throw std::runtime_error("unknown joint control field: "+key);
                }
            }
            auto result=sc_physics_joint_control(script(L)->world,static_cast<ScJointId>(id),c);
            if(!result) throw std::runtime_error(result.error());
        }
        ok=true;
    } catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); }
    if(!ok) return luaL_error(L,"physics: %s",script(L)->error);
    lua_createtable(L,0,6);
    lua_pushboolean(L,c.limit); lua_setfield(L,-2,"limit");
    lua_pushboolean(L,c.motor); lua_setfield(L,-2,"motor");
    field(L,"lower",c.lower); field(L,"upper",c.upper); field(L,"speed",c.speed); field(L,"max_effort",c.max_effort);
    return 1;
}
const ScValue default_drop{.2},default_angle{0.0},default_length{8.0};
constexpr ScLuaParameter entity_parameter{"id","ScEntityId",true,"Live room entity handle; never persist runtime handles.",nullptr,1,SC_ID_MAX};
constexpr ScLuaParameter joint_parameter{"id","ScJointId",true,"Live room joint handle; rebuilding either body invalidates it.",nullptr,1,SC_ID_MAX};
constexpr ScLuaParameter x_parameter{"x","number",true,"World-pixel X coordinate.",nullptr,-1e6,1e6};
constexpr ScLuaParameter y_parameter{"y","number",true,"World-pixel Y coordinate.",nullptr,-1e6,1e6};
constexpr ScLuaParameter dx_parameter{"dx","number",true,"World-pixel translation, not an endpoint.",nullptr,-1e6,1e6};
constexpr ScLuaParameter dy_parameter{"dy","number",true,"World-pixel translation, not an endpoint.",nullptr,-1e6,1e6};
constexpr ScLuaParameter points_parameter{"points","number[]",true,"Plain dense 1..8 world x,y pairs, each coordinate -1e6..1e6; circle, distinct capsule endpoints, or cyclic convex polygon."};
constexpr ScLuaParameter radius_parameter{"radius","number",true,"World-pixel rounding radius; positive for circles/capsules, may be zero for polygons.",nullptr,0,4096};
constexpr ScLuaParameter force_parameters[]={entity_parameter,
    {"x","number",true,"Center force X, mass*pixel/second^2.",nullptr,-1e6,1e6},
    {"y","number",true,"Center force Y, mass*pixel/second^2.",nullptr,-1e6,1e6}};
constexpr ScLuaParameter impulse_parameters[]={entity_parameter,
    {"x","number",true,"Center impulse X, mass*pixel/second.",nullptr,-1e6,1e6},
    {"y","number",true,"Center impulse Y, mass*pixel/second.",nullptr,-1e6,1e6}};
constexpr ScLuaParameter drop_parameters[]={entity_parameter,
    {"seconds","number",false,"Replace the remaining one-way exclusion time; nil uses the default.",&default_drop,0,10}};
constexpr ScLuaParameter ray_parameters[]={x_parameter,y_parameter,dx_parameter,dy_parameter};
constexpr ScLuaParameter query_parameters[]={x_parameter,y_parameter,
    {"w","number",true,"Unrotated rectangle width; zero returns an empty array.",nullptr,0,1e6},
    {"h","number",true,"Unrotated rectangle height; zero returns an empty array.",nullptr,0,1e6},
    {"angle","number",false,"Radians about the rectangle center; transformed corners must remain within +/-1e6.",&default_angle,-1e6,1e6}};
constexpr ScLuaParameter overlap_parameters[]={points_parameter,radius_parameter};
constexpr ScLuaParameter sweep_parameters[]={points_parameter,radius_parameter,dx_parameter,dy_parameter};
constexpr ScLuaParameter joint_parameters[]={
    {"type","'distance'|'revolute'|'prismatic'",true,"Prismatic axis is A's local vertical axis."},
    {"a","ScEntityId",true,"First live entity with a body.",nullptr,1,SC_ID_MAX},
    {"b","ScEntityId",true,"Distinct second live entity with a body.",nullptr,1,SC_ID_MAX},
    x_parameter,y_parameter,
    {"length","number",false,"Distance rest length (clamped to .16 minimum) or initial prismatic upper limit; unused for revolute.",&default_length,.01,4096},
    {"bx","number",false,"Distance-only second anchor X; supply bx/by together, otherwise use x/y.",nullptr,-1e6,1e6},
    {"by","number",false,"Distance-only second anchor Y.",nullptr,-1e6,1e6}};
constexpr ScLuaParameter unjoint_parameters[]={joint_parameter};
constexpr ScLuaParameter control_parameters[]={joint_parameter,
    {"patch","ScJointControlPatch",false,"Plain partial object; omitted fields retain current values. Nil reads without changing controls."}};
constexpr ScLuaContract force_contract{force_parameters,nullptr,ScLuaPhases::mutate};
constexpr ScLuaContract impulse_contract{impulse_parameters,nullptr,ScLuaPhases::mutate};
constexpr ScLuaContract drop_contract{drop_parameters,nullptr,ScLuaPhases::mutate};
constexpr ScLuaContract contacts_contract{{},"ScContact[]",ScLuaPhases::read,"project.limits.contacts; at most 64 contacts per body"};
constexpr ScLuaContract ray_contract{ray_parameters,"ScRayHit|nil",ScLuaPhases::mutate};
constexpr ScLuaContract query_contract{query_parameters,"ScEntityId[]",ScLuaPhases::mutate,"project.limits.entities + one terrain record"};
constexpr ScLuaContract overlap_contract{overlap_parameters,"ScEntityId[]",ScLuaPhases::mutate,"project.limits.entities + one terrain record"};
constexpr ScLuaContract sweep_contract{sweep_parameters,"ScRayHit|nil",ScLuaPhases::mutate};
constexpr ScLuaContract joint_contract{joint_parameters,"ScJointId",ScLuaPhases::mutate,"256 room joints; generation exhaustion retires a slot"};
constexpr ScLuaContract unjoint_contract{unjoint_parameters,nullptr,ScLuaPhases::mutate};
constexpr ScLuaContract control_contract{control_parameters,"ScJointControl",ScLuaPhases::mutate};
const ScLuaApi api[]={
    {"force",sc_lua_guard<force>,"force(id,x,y)","Queue center force for the next sync; repeated calls accumulate. Only dynamic bodies respond.",&force_contract},
    {"impulse",sc_lua_guard<impulse>,"impulse(id,x,y)","Queue center impulse for the next sync; repeated calls accumulate. Only dynamic bodies respond.",&impulse_contract},
    {"drop",sc_lua_guard<drop>,"drop(id[,seconds])","Ignore one-way contacts for a bounded time; zero cancels the exclusion.",&drop_contract},
    {"contacts",sc_lua_guard<contacts>,"contacts() -> array","Independent snapshot of previous completed step contacts, sorted by a,b,phase. Does not synchronize bodies. Destroyed shapes may omit sensor end events.",&contacts_contract},
    {"ray",sc_lua_guard<ray>,"ray(x,y,dx,dy) -> hit|nil","Closest Box2D hit; initial overlap ignored, ties by ID then point/normal. Includes sensors, excludes bodyless art, solid=false and zero category/mask. One-way platforms use full geometry. Syncs bodies without advancing time.",&ray_contract},
    {"query",sc_lua_guard<query>,"query(x,y,w,h[,angle]) -> ids","Actual overlap against a center-rotated rectangle. Independent sorted unique IDs, terrain 0. Same filters as ray; syncs bodies without advancing time, except zero-size queries return empty immediately.",&query_contract},
    {"overlap",sc_lua_guard<overlap>,"overlap(points,radius) -> ids","Actual convex shape overlap. Independent sorted unique IDs, terrain 0. Same filters as ray; rejects concave/degenerate shapes and syncs bodies without advancing time.",&overlap_contract},
    {"sweep",sc_lua_guard<sweep>,"sweep(points,radius,dx,dy) -> hit|nil","Translation-only convex sweep, closest hit with ties by ID then point/normal. Initial overlap returns fraction=0 and zero normal. Same filters as ray; syncs bodies without advancing time.",&sweep_contract},
    {"joint",sc_lua_guard<joint>,"joint(type,a,b,x,y[,length,bx,by]) -> id","Create a distance, revolute or local-vertical prismatic joint after synchronizing bodies. Body destruction/rebuild and room replacement invalidate attached joints. Missing bodies or exhausted capacity raise an error.",&joint_contract},
    {"unjoint",sc_lua_guard<unjoint>,"unjoint(id)","Synchronize bodies, then destroy a generation-checked joint; stale handles error.",&unjoint_contract},
    {"joint_control",sc_lua_guard<joint_control>,"joint_control(id[,patch]) -> controls","Read or atomically patch joint controls; returns all six fields as an independent snapshot. Errors retain controls; unchanged patches do not wake bodies. Even reads synchronize bodies and require load/init/update.",&control_contract},
    {nullptr,nullptr,nullptr,nullptr}
};
}
void sc_script_body_patch(lua_State* L,int index,ScEntity* e) {
    lua_getfield(L,index,"body"); if(lua_isnil(L,-1)) { lua_pop(L,1); return; }
    if(lua_isboolean(L,-1)&&!lua_toboolean(L,-1)) { e->body_type=0; e->dynamic=false; lua_pop(L,1); return; }
    bool ok=false;
    try {
        auto value=sc_lua_read(L,-1); if(!value) throw std::runtime_error(value.error());
        auto* object=std::get_if<ScValue::Object>(&value->data); if(!object) throw std::runtime_error("body must be a table");
        if(!e->body_type) e->body_type=3;
        for(const auto& [key,v]:*object) {
            auto n=[&](double lo,double hi) { auto p=std::get_if<double>(&v.data); if(!p||*p<lo||*p>hi) throw std::runtime_error("invalid body."+key); return static_cast<float>(*p); };
            auto b=[&]() { auto p=std::get_if<bool>(&v.data); if(!p) throw std::runtime_error("invalid body."+key); return *p; };
            if(key=="type") { auto name=v.text(); e->body_type=name=="static"?1:name=="kinematic"?2:name=="dynamic"?3:0; if(!e->body_type) throw std::runtime_error("unknown body type"); }
            else if(key=="shape") { auto name=v.text(); e->shape=name=="box"?0:name=="circle"?1:name=="capsule"?2:name=="polygon"?3:-1; if(e->shape<0) throw std::runtime_error("unknown body shape"); }
            else if(key=="shapes") compound(v,e);
            else if(key=="density") e->density=n(.001,10000);
            else if(key=="friction") e->friction=n(0,10);
            else if(key=="restitution") e->restitution=n(0,1);
            else if(key=="fixed_rotation") e->fixed_rotation=b();
            else if(key=="sensor") e->sensor=b();
            else if(key=="bullet") e->bullet=b();
            else if(key=="one_way") e->one_way=b();
            else if(key=="category"||key=="mask") {
                double bits=v.number(-1); if(bits<0||bits>UINT32_MAX||std::floor(bits)!=bits) throw std::runtime_error("invalid collision bits");
                if(key=="category") e->category=static_cast<uint32_t>(bits); else e->mask=static_cast<uint32_t>(bits);
            } else if(key=="vertices") {
                auto* points=std::get_if<ScValue::Array>(&v.data);
                if(!points||points->size()<6||points->size()>16||points->size()%2) throw std::runtime_error("vertices requires 3..8 x,y pairs");
                e->vertex_count=static_cast<int>(points->size()/2);
                for(size_t j=0;j<points->size();++j) {
                    auto p=std::get_if<double>(&(*points)[j].data); if(!p||std::fabs(*p)>4096) throw std::runtime_error("invalid polygon vertex"); e->vertices[j]=static_cast<float>(*p);
                }
            } else throw std::runtime_error("unknown body field: "+key);
        }
        if(e->shape==3&&e->vertex_count<3) throw std::runtime_error("polygon requires vertices");
        e->dynamic=e->body_type==3; ok=true;
    } catch(const std::exception& ex) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",ex.what()); }
    lua_pop(L,1); if(!ok) luaL_error(L,"%s",script(L)->error);
}
void sc_script_body_push(lua_State* L,const ScEntity* e) {
    id_field(L,"support",e->support);
    field(L,"normal_x",e->normal_x); field(L,"normal_y",e->normal_y);
    if(!e->body_type&&!e->dynamic) { lua_pushboolean(L,0); lua_setfield(L,-2,"body"); return; }
    static const char* const types[]={"none","static","kinematic","dynamic"};
    static const char* const shapes[]={"box","circle","capsule","polygon"};
    lua_createtable(L,0,14);
    lua_pushstring(L,types[e->body_type?e->body_type:3]); lua_setfield(L,-2,"type");
    lua_pushstring(L,shapes[e->shape]); lua_setfield(L,-2,"shape");
    field(L,"density",e->density); field(L,"friction",e->body_type?e->friction:0); field(L,"restitution",e->restitution);
    field(L,"category",e->category); field(L,"mask",e->mask);
    lua_pushboolean(L,e->fixed_rotation); lua_setfield(L,-2,"fixed_rotation");
    lua_pushboolean(L,e->sensor); lua_setfield(L,-2,"sensor");
    lua_pushboolean(L,e->bullet); lua_setfield(L,-2,"bullet");
    lua_pushboolean(L,e->one_way); lua_setfield(L,-2,"one_way");
    auto vertices=[&](const float* points,int count) {
        lua_createtable(L,count*2,0);
        for(int j=0;j<count*2;++j) { lua_pushnumber(L,points[j]); lua_rawseti(L,-2,j+1); }
        lua_setfield(L,-2,"vertices");
    };
    if(e->vertex_count) vertices(e->vertices.data(),e->vertex_count);
    if(e->shape_count) {
        lua_createtable(L,e->shape_count,0);
        for(int j=0;j<e->shape_count;++j) {
            const auto& shape=e->shapes[static_cast<size_t>(j)]; lua_createtable(L,0,6);
            lua_pushstring(L,shapes[shape.kind]); lua_setfield(L,-2,"shape");
            field(L,"x",shape.x); field(L,"y",shape.y); field(L,"w",shape.w); field(L,"h",shape.h);
            if(shape.vertex_count) vertices(shape.vertices.data(),shape.vertex_count);
            lua_rawseti(L,-2,j+1);
        }
        lua_setfield(L,-2,"shapes");
    }
    lua_setfield(L,-2,"body");
}
void sc_script_physics_register(lua_State* L) {
    lua_newtable(L); sc_api_register(L,api); lua_setfield(L,-2,"physics");
}
void sc_script_physics_describe() { sc_api_describe(api,"sc.physics."); }
ScValue sc_script_physics_contracts() {
    auto field=[](const char* name,const char* type,bool required,bool readonly,const char* description,
                  std::optional<double> minimum={},std::optional<double> maximum={}) {
        ScValue::Object value{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{required}},{"readonly",ScValue{readonly}},{"description",ScValue{std::string(description)}}};
        if(minimum) value.emplace("minimum",ScValue{*minimum});
        if(maximum) value.emplace("maximum",ScValue{*maximum});
        return ScValue{std::move(value)};
    };
    auto defaulted=[&](const char* name,const char* kind,ScValue initial,const char* description,
                       std::optional<double> minimum={},std::optional<double> maximum={}) {
        auto value=field(name,kind,false,false,description,minimum,maximum);
        std::get<ScValue::Object>(value.data).emplace("default",std::move(initial));
        return value;
    };
    auto type=[](ScValue::Array fields,const char* constraint) {
        return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},
            {"constraints",ScValue{ScValue::Array{ScValue{std::string(constraint)}}}}}};
    };
    auto controls=[&](bool snapshot) {
        return type({field("limit","boolean",snapshot,snapshot,"Initially true only for prismatic joints."),
            field("motor","boolean",snapshot,snapshot,"Initially false. Enabling with zero max_effort produces no drive."),
            field("lower","number",snapshot,snapshot,"Distance .16..4096 pixels (initial .16); prismatic -4096..4096 pixels (initial 0); revolute -pi..pi radians (initial -pi).",-4096,4096),
            field("upper","number",snapshot,snapshot,"Same kind-dependent range as lower. Initial distance 4096, prismatic creation length, revolute pi.",-4096,4096),
            field("speed","number",snapshot,snapshot,"Initial 0. Pixels/second for distance and prismatic; radians/second for revolute.",-1e6,1e6),
            field("max_effort","number",snapshot,snapshot,"Initial 0. Mass*pixel/second^2 for linear joints; mass*pixel^2/second^2 for revolute.",0,1e9)},
            snapshot?"Independent six-field snapshot; editing it has no effect. Reading synchronizes bodies but does not step simulation.":
                "Plain partial object, no unknown fields or coercion. All numbers finite, lower <= upper even when limit is disabled. Omitted fields retain values; validation failure changes no controls.");
    };
    return ScValue{ScValue::Object{
        {"ScBodyShape",type({
            defaulted("shape","'box'|'circle'|'capsule'|'polygon'",ScValue{std::string("box")},"Capsules follow their longer axis."),
            defaulted("x","number",ScValue{0.0},"Local top-left X in pixels.",-4096,4096),
            defaulted("y","number",ScValue{0.0},"Local top-left Y in pixels.",-4096,4096),
            defaulted("w","number",ScValue{8.0},"Local width in pixels.",.001,4096),
            defaulted("h","number",ScValue{8.0},"Local height in pixels.",.001,4096),
            field("vertices","number[]",false,false,"Polygon: 3..8 distinct convex x,y pairs, each -4096..4096.")},
            "Plain shape table; unknown fields rejected. Polygon requires vertices; compound bodies contain 1..4 shapes.")},
        {"ScBody",type({
            defaulted("type","'static'|'kinematic'|'dynamic'",ScValue{std::string("dynamic")},"New bodies default to dynamic; patches retain an existing type."),
            defaulted("shape","'box'|'circle'|'capsule'|'polygon'",ScValue{std::string("box")},"Single shape uses entity dimensions."),
            field("vertices","number[]",false,false,"Single polygon: 3..8 distinct convex x,y pairs, local to entity top-left, each -4096..4096."),
            field("shapes","ScBodyShape[]",false,false,"1..4 compound shapes override the single shape; all share material and collision filters."),
            defaulted("density","number",ScValue{1.0},"Authored mass scales with area / 32².",.001,10000),
            defaulted("friction","number",ScValue{.3},"Legacy dynamic shorthand without a body uses zero.",0,10),
            defaulted("restitution","number",ScValue{0.0},"Bounciness.",0,1),
            defaulted("fixed_rotation","boolean",ScValue{true},"Prevent solver rotation."),
            defaulted("sensor","boolean",ScValue{false},"Overlap events without collision response."),
            defaulted("bullet","boolean",ScValue{false},"Continuous collision for dynamic bodies."),
            defaulted("one_way","boolean",ScValue{false},"Top-face platform; intended unrotated."),
            defaulted("category","integer",ScValue{1.0},"Unsigned 32-bit collision category bits.",0,4294967295.0),
            defaulted("mask","integer",ScValue{4294967295.0},"Unsigned 32-bit collision mask bits.",0,4294967295.0)},
            "Plain body table; unknown fields rejected. Defaults apply when creating a body; omitted patch fields retain current values. Body=false removes it. Polygon vertices are required.")},
        {"ScContact",type({field("a","ScEntityId",true,true,"First runtime entity ID; 0 denotes terrain. May already be destroyed when reading.",0,SC_ID_MAX),
            field("b","ScEntityId",true,true,"Second runtime entity ID; 0 denotes terrain.",0,SC_ID_MAX),
            field("nx","number",true,true,"Normal X from a to b; sensors use zero.",-1,1),
            field("ny","number",true,true,"Normal Y from a to b; sensors use zero.",-1,1),
            field("sensor","boolean",true,true,"True for sensor overlap edges."),
            field("phase","'contact'|'begin'|'end'",true,true,"Solids use contact; sensors use begin/end.")},
            "Previous completed step, sorted by a,b,phase (contact,begin,end). Destroyed shapes may omit sensor end. Contacts are copied; reading does not synchronize or step bodies.")},
        {"ScRayHit",type({field("id","ScEntityId",true,true,"Hit entity, or 0 for terrain and map boundaries.",0,SC_ID_MAX),
            field("x","number",true,true,"World-pixel contact point X, subject to solver tolerance."),
            field("y","number",true,true,"World-pixel contact point Y."),
            field("nx","number",true,true,"Surface normal X; zero for an initially overlapping sweep.",-1,1),
            field("ny","number",true,true,"Surface normal Y; zero for an initially overlapping sweep.",-1,1),
            field("fraction","number",true,true,"Fraction along the supplied translation, not distance in pixels.",0,1)},
            "Closest hit; equal fractions sort by ID, contact point and normal. Ray ignores initial overlap; sweep returns fraction 0 and zero normal. No angular sweep.")},
        {"ScJointControl",controls(true)},{"ScJointControlPatch",controls(false)}}};
}
