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
float number(lua_State* L,int index,float lo=-1e6f,float hi=1e6f) {
    if(lua_type(L,index)!=LUA_TNUMBER) luaL_error(L,"expected number");
    auto n=lua_tonumber(L,index); if(!std::isfinite(n)||n<lo||n>hi) luaL_error(L,"number outside range"); return static_cast<float>(n);
}
ScEntity* entity(lua_State* L,int index) {
    int valid=0; auto id=lua_tointegerx(L,index,&valid);
    auto* e=valid&&id>0&&id<=UINT32_MAX?sc_entity(script(L)->world,static_cast<uint32_t>(id)):nullptr;
    if(!e) luaL_error(L,"stale or unknown entity");
    return e;
}
void mutable_phase(lua_State* L) { if(script(L)->phase>=2) luaL_error(L,"mutation is forbidden in draw() or migration"); }
void field(lua_State* L,const char* key,double value) { lua_pushnumber(L,value); lua_setfield(L,-2,key); }
int apply_force(lua_State* L,bool impulse) {
    mutable_phase(L); auto* e=entity(L,1); float x=number(L,2),y=number(L,3);
    if(impulse) { e->impulse_x+=x; e->impulse_y+=y; }
    else { e->force_x+=x; e->force_y+=y; } return 0;
}
int force(lua_State* L) { return apply_force(L,false); }
int impulse(lua_State* L) { return apply_force(L,true); }
int drop(lua_State* L) { mutable_phase(L); auto* e=entity(L,1); e->drop_time=lua_isnoneornil(L,2)?.2f:number(L,2,0,10); return 0; }
int contacts(lua_State* L) {
    auto* w=script(L)->world; lua_createtable(L,w->contact_count,0);
    for(int i=0;i<w->contact_count;++i) {
        const auto& c=w->contacts[i]; lua_createtable(L,0,5);
        field(L,"a",c.a); field(L,"b",c.b); field(L,"nx",c.nx); field(L,"ny",c.ny);
        lua_pushboolean(L,c.sensor); lua_setfield(L,-2,"sensor");
        lua_pushstring(L,c.phase==1?"begin":c.phase==2?"end":"contact"); lua_setfield(L,-2,"phase"); lua_rawseti(L,-2,i+1);
    } return 1;
}
int ray(lua_State* L) {
    mutable_phase(L); float x=number(L,1),y=number(L,2),dx=number(L,3),dy=number(L,4); ScRay hit; bool ok=false;
    try { hit=sc_physics_ray(script(L)->world,x,y,dx,dy); ok=true; }
    catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); }
    if(!ok) return luaL_error(L,"physics: %s",script(L)->error);
    if(!hit.hit) { lua_pushnil(L); return 1; }
    lua_createtable(L,0,6); field(L,"id",hit.id); field(L,"x",hit.x); field(L,"y",hit.y); field(L,"nx",hit.nx); field(L,"ny",hit.ny); field(L,"fraction",hit.fraction); return 1;
}
int query(lua_State* L) {
    float x=number(L,1),y=number(L,2),w=number(L,3,0),h=number(L,4,0); lua_newtable(L); int n=0;
    for(const auto& e:script(L)->world->entities) if(e.alive&&e.x<x+w&&e.x+e.w>x&&e.y<y+h&&e.y+e.h>y) {
        lua_pushinteger(L,e.id); lua_rawseti(L,-2,++n);
    } return 1;
}
int joint(lua_State* L) {
    mutable_phase(L); static const char* const names[]={"distance","revolute","prismatic",nullptr};
    int kind=luaL_checkoption(L,1,nullptr,names); auto a=entity(L,2)->id,b=entity(L,3)->id;
    float x=number(L,4),y=number(L,5),length=lua_isnoneornil(L,6)?8:number(L,6,.01f,4096); uint32_t id=0;
    try { id=sc_physics_joint(script(L)->world,kind,a,b,x,y,length); }
    catch(const std::exception& e) { std::snprintf(script(L)->error,SC_ERROR_MAX,"%s",e.what()); }
    if(!id) return luaL_error(L,"cannot create joint: bodies required or capacity exhausted");
    lua_pushinteger(L,id); return 1;
}
int unjoint(lua_State* L) {
    mutable_phase(L); auto id=luaL_checkinteger(L,1);
    if(id<=0||id>UINT32_MAX||!sc_physics_joint_destroy(script(L)->world,static_cast<uint32_t>(id))) return luaL_error(L,"stale joint ID");
    return 0;
}
const ScLuaApi api[]={
    {"force",sc_lua_guard<force>,"force(id,x,y)","Queue center force for the next sync; authored mass*pixel/second^2, components -1e6..1e6."},
    {"impulse",sc_lua_guard<impulse>,"impulse(id,x,y)","Queue center impulse; authored mass*pixel/second, components -1e6..1e6."},
    {"drop",sc_lua_guard<drop>,"drop(id[,seconds])","Ignore one-way contacts for 0..10 seconds (default .2)."},
    {"contacts",sc_lua_guard<contacts>,"contacts() -> array","Previous step contacts, stable by entity IDs. Terrain ID 0; nx/ny points a to b. Sensor phases begin/end, solid phase contact."},
    {"ray",sc_lua_guard<ray>,"ray(x,y,dx,dy) -> hit|nil","Closest Box2D hit with id,x,y,nx,ny,fraction. Syncs pending bodies; forbidden in draw/migration."},
    {"query",sc_lua_guard<query>,"query(x,y,w,h) -> ids","Stable entity AABB overlap query; includes artwork, ignores rotation. Nonnegative dimensions."},
    {"joint",sc_lua_guard<joint>,"joint(type,a,b,x,y[,length]) -> id","Distance, revolute or vertical prismatic joint; world anchor in pixels, length .01..4096 (default 8). Capacity 256. Rebuilding a body destroys its joints."},
    {"unjoint",sc_lua_guard<unjoint>,"unjoint(id)","Destroy a generation-checked joint; stale handles error."},
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
    field(L,"angle",e->angle); field(L,"angular_velocity",e->angular_velocity); field(L,"support",e->support);
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
