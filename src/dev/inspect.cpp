#include "inspect.h"
#include "shiny/script.h"
#include "shiny/projectiles.h"
#include <lua.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace {
using V=ScValue;
char registry_key,fields_key;
enum Field { nodes,focus,active,dirty,id,kind,parent,visible,disabled,modal,rect,x,y,w,h,
    value,group,page,items,scroll,editor,cursor,anchor,scroll_max,scroll_drag,tooltip,owner,truncated,layout_pending };
constexpr const char* fields[]={"authored_nodes","focus","active","dirty","id","kind","parent",
    "visible","disabled","modal","rect","x","y","w","h","value","group","page","items",
    "scroll","editor","cursor","anchor","scroll_max","scroll_drag","tooltip","owner","truncated","layout_pending"};
// Interned keys stay pinned in the registry. Inspection cannot allocate in Lua
// or raise a Lua error while C++ owns the result being assembled.
void field(lua_State* L,int table,Field key) {
    table=lua_absindex(L,table);
    lua_rawgetp(L,LUA_REGISTRYINDEX,&fields_key);
    lua_rawgeti(L,-1,static_cast<int>(key)+1); lua_remove(L,-2);
    lua_rawget(L,table);
}
V number(size_t n) { return V{static_cast<double>(n)}; }
V scalar(lua_State* L,int index) {
    if(lua_type(L,index)==LUA_TBOOLEAN) return V{lua_toboolean(L,index)!=0};
    if(lua_type(L,index)==LUA_TNUMBER) {
        const auto n=lua_tonumber(L,index); if(std::isfinite(n)) return V{double(n)};
    }
    if(lua_type(L,index)==LUA_TSTRING) {
        size_t bytes=0; const char* s=lua_tolstring(L,index,&bytes);
        size_t end=std::min(bytes,size_t(256));
        while(end<bytes&&end&&(static_cast<unsigned char>(s[end])&0xc0)==0x80) --end;
        return V{std::string(s,end)};
    }
    return {};
}
void property(lua_State* L,int table,Field key,V::Object& row,const char* name=nullptr) {
    field(L,table,key);
    auto result=scalar(L,-1); lua_pop(L,1);
    if(!std::holds_alternative<std::monostate>(result.data)) row.emplace(name?name:fields[key],std::move(result));
}
void bounds(lua_State* L,int table,V::Object& row,const char* name) {
    field(L,table,rect);
    if(lua_istable(L,-1)) {
        V::Object result; for(auto key:{x,y,w,h}) property(L,-1,key,result);
        row.emplace(name,V{std::move(result)});
    }
    lua_pop(L,1);
}
bool owns(lua_State* L,int table,Field container,Field key,int node) {
    field(L,table,container); bool result=false;
    if(lua_istable(L,-1)) {
        field(L,-1,key); field(L,node,id);
        result=!lua_isnil(L,-1)&&lua_rawequal(L,-1,-2)!=0;
        lua_pop(L,2);
    }
    lua_pop(L,1); return result;
}
bool flag(lua_State* L,int table,Field key,bool fallback=false) {
    field(L,table,key); const bool result=lua_isnil(L,-1)?fallback:lua_toboolean(L,-1)!=0; lua_pop(L,1); return result;
}
size_t integer(const V& request,const char* name,size_t fallback,size_t maximum) {
    const auto* value=request.get(name); if(!value) return fallback;
    const double n=value->number(NAN);
    if(!std::isfinite(n)||n<0||n>double(maximum)||std::floor(n)!=n) throw std::runtime_error(std::string("invalid ")+name);
    return static_cast<size_t>(n);
}
bool equal_string(lua_State* L,int index,std::string_view value) {
    if(lua_type(L,index)!=LUA_TSTRING) return false;
    size_t size=0; const char* text=lua_tolstring(L,index,&size); return std::string_view(text,size)==value;
}
struct StackScope {
    lua_State* L; int top;
    explicit StackScope(lua_State* state):L(state),top(lua_gettop(state)) {}
    ~StackScope() { lua_settop(L,top); }
};
}

void sc_debug_ui_init(lua_State* L) {
    lua_newtable(L); lua_newtable(L);
    lua_pushliteral(L,"v"); lua_setfield(L,-2,"__mode"); lua_setmetatable(L,-2);
    lua_rawsetp(L,LUA_REGISTRYINDEX,&registry_key);
    lua_createtable(L,static_cast<int>(std::size(fields)),0);
    for(size_t i=0;i<std::size(fields);++i) { lua_pushstring(L,fields[i]); lua_rawseti(L,-2,static_cast<lua_Integer>(i+1)); }
    lua_rawsetp(L,LUA_REGISTRYINDEX,&fields_key);
}
int sc_debug_ui_register(lua_State* L) {
    lua_settop(L,2);
    const auto* script=*static_cast<ScScript**>(lua_getextraspace(L));
    if(script->phase==2||script->phase==3) return luaL_error(L,"debug.ui requires init, update or ui_update");
    luaL_checktype(L,1,LUA_TSTRING);
    size_t size=0; const char* name=lua_tolstring(L,1,&size);
    if(size==0||size>128||std::memchr(name,0,size)) return luaL_error(L,"UI name requires 1..128 bytes without NUL");
    if(!lua_isnoneornil(L,2)) {
        luaL_checktype(L,2,LUA_TTABLE);
        field(L,2,nodes); const bool valid=lua_istable(L,-1); lua_pop(L,1);
        if(!valid) return luaL_error(L,"debug.ui requires a shiny.ui tree");
    }
    lua_rawgetp(L,LUA_REGISTRYINDEX,&registry_key);
    const int registry=lua_gettop(L);
    lua_pushvalue(L,1); lua_rawget(L,registry);
    const bool exists=!lua_isnil(L,-1); lua_pop(L,1);
    if(!exists&&!lua_isnoneornil(L,2)) {
        size_t count=0; lua_pushnil(L);
        while(lua_next(L,registry)) { ++count; lua_pop(L,1); }
        if(count>=64) return luaL_error(L,"UI debug registry capacity exhausted (64)");
    }
    lua_pushvalue(L,1);
    if(lua_isnoneornil(L,2)) lua_pushnil(L); else lua_pushvalue(L,2);
    lua_rawset(L,registry); return 0;
}
V sc_debug_ui_inspect(ScScript& script,const V& request) {
    auto* L=script.lua;
    if(!L||!lua_checkstack(L,32)) throw std::runtime_error("UI inspection stack unavailable");
    StackScope stack(L);
    const auto offset=integer(request,"offset",0,65536),limit=integer(request,"limit",64,128);
    if(!limit) throw std::runtime_error("limit must be 1..128");
    lua_rawgetp(L,LUA_REGISTRYINDEX,&registry_key); const int registry=lua_gettop(L);
    const auto* named=request.get("tree");
    if(!named) {
        V::Object trees; lua_pushnil(L);
        while(lua_next(L,registry)) {
            if(lua_type(L,-2)==LUA_TSTRING&&lua_istable(L,-1)) {
                V::Object tree; property(L,-1,focus,tree);
                field(L,-1,nodes); tree.emplace("total",number(lua_istable(L,-1)?lua_rawlen(L,-1):0)); lua_pop(L,1);
                trees.emplace(lua_tostring(L,-2),V{std::move(tree)});
            }
            lua_pop(L,1);
        }
        V::Array result; size_t at=0;
        for(auto& [name,tree]:trees) {
            if(at++<offset) continue;
            if(result.size()==limit) break;
            std::get<V::Object>(tree.data).emplace("name",V{name}); result.push_back(std::move(tree));
        }
        return V{V::Object{{"items",V{std::move(result)}},{"total",number(trees.size())},{"offset",number(offset)}}};
    }
    const auto* name=std::get_if<std::string>(&named->data);
    if(!name||name->empty()||name->size()>128) throw std::runtime_error("invalid UI tree name");
    bool found=false; lua_pushnil(L);
    while(lua_next(L,registry)) {
        if(equal_string(L,-2,*name)&&lua_istable(L,-1)) { found=true; break; }
        lua_pop(L,1);
    }
    if(!found) throw std::runtime_error("UI tree unavailable (released or unregistered)");
    const int ui=lua_gettop(L);
    field(L,ui,nodes); if(!lua_istable(L,-1)) throw std::runtime_error("UI authored_nodes is not a table");
    const int authored=lua_gettop(L); const size_t total=lua_rawlen(L,authored);
    if(total>65536) throw std::runtime_error("UI inspection node limit exceeded (65536)");
    V::Array rows;
    for(size_t at=offset;at<total&&rows.size()<limit;++at) {
        lua_rawgeti(L,authored,static_cast<lua_Integer>(at+1)); const int node=lua_gettop(L);
        if(!lua_istable(L,node)) throw std::runtime_error("UI authored node is not a table");
        V::Object row; property(L,node,id,row); property(L,node,kind,row);
        const bool pending=flag(L,ui,dirty)||flag(L,ui,layout_pending); row.emplace("layout_pending",V{pending});
        for(auto [key,label]:{std::pair{focus,"focused"},std::pair{active,"active"}}) {
            field(L,ui,key); field(L,node,id);
            row.emplace(label,V{!lua_isnil(L,-1)&&lua_rawequal(L,-1,-2)!=0}); lua_pop(L,2);
        }
        property(L,node,value,row); property(L,node,group,row); property(L,node,page,row);
        field(L,node,value);
        if(lua_type(L,-1)==LUA_TSTRING&&lua_rawlen(L,-1)>256) {
            row.emplace("value_bytes",number(lua_rawlen(L,-1))); row.emplace("value_truncated",V{true});
        }
        lua_pop(L,1);
        row.emplace("modal",V{flag(L,node,modal)});
        field(L,node,parent); if(lua_istable(L,-1)) property(L,-1,id,row,"parent"); lua_pop(L,1);
        field(L,node,items);
        if(lua_istable(L,-1)) {
            const auto count=lua_rawlen(L,-1); row.emplace("item_count",number(count));
            field(L,node,value); int valid=0;
            const auto selected=lua_type(L,-1)==LUA_TNUMBER?lua_tointegerx(L,-1,&valid):0;
            lua_pop(L,1);
            if(valid&&selected>0&&static_cast<lua_Unsigned>(selected)<=count) {
                lua_rawgeti(L,-1,selected);
                if(lua_istable(L,-1)) property(L,-1,id,row,"selected_item_id");
                lua_pop(L,1);
            }
        }
        lua_pop(L,1);
        field(L,node,editor);
        if(lua_istable(L,-1)) { property(L,-1,cursor,row); property(L,-1,anchor,row); } lua_pop(L,1);
        field(L,ui,scroll);
        if(lua_istable(L,-1)) { field(L,node,id); lua_rawget(L,-2); row.emplace("scroll",scalar(L,-1)); lua_pop(L,1); } lua_pop(L,1);
        property(L,node,scroll_max,row);
        row.emplace("scroll_capture",V{owns(L,ui,scroll_drag,id,node)});
        bool shown=true,enabled=true; size_t depth=0; lua_pushvalue(L,node);
        while(lua_istable(L,-1)&&depth++<total) {
            shown=shown&&flag(L,-1,visible,true); enabled=enabled&&!flag(L,-1,disabled);
            field(L,-1,parent); lua_remove(L,-2);
        }
        if(!lua_isnil(L,-1)) { shown=enabled=false; row.emplace("invalid_parent",V{true}); } lua_pop(L,1);
        row.emplace("visible",V{shown}); row.emplace("enabled",V{enabled});
        if(shown&&!pending) bounds(L,node,row,"rect");
        const bool tip=shown&&!pending&&owns(L,ui,tooltip,owner,node);
        row.emplace("tooltip_visible",V{tip});
        if(tip) {
            field(L,ui,tooltip);
            row.emplace("tooltip_truncated",V{flag(L,-1,truncated)});
            bounds(L,lua_gettop(L),row,"tooltip_rect");
            lua_pop(L,1);
        }
        rows.emplace_back(std::move(row)); lua_pop(L,1);
    }
    return V{V::Object{{"items",V{std::move(rows)}},{"total",number(total)},{"offset",number(offset)}}};
}

V sc_debug_inspect(ScScript& script,std::string_view section,const V& request) {
    if(section=="ui") return sc_debug_ui_inspect(script,request);
    const auto& world=*script.world;
    const auto offset=integer(request,"offset",0,65536),limit=integer(request,"limit",64,128);
    if(!limit) throw std::runtime_error("limit must be 1..128");
    if(section=="metrics") {
        size_t entities=0,font_bytes=0,glyphs=0;
        for(const auto& e:world.entities) if(e.alive) ++entities;
        for(const auto& r:world.resources) { font_bytes+=r.font_bytes.size(); glyphs+=r.glyphs.size(); }
        return V{V::Object{{"entities",number(entities)},{"entity_capacity",number(world.entities.size())},
            {"particles",number(world.particles.count)},{"particle_capacity",number(world.particles.capacity())},
            {"projectiles",number(world.projectiles?world.projectiles->count:0)},
            {"projectile_capacity",number(world.projectiles?world.projectiles->x.size():0)},
            {"draw_commands",number(static_cast<size_t>(world.draw_count))},{"draw_capacity",number(world.draws.size())},
            {"lua_bytes",number(script.memory_used)},{"font_bytes",number(font_bytes)},
            {"cached_glyphs",number(glyphs)},{"resources",number(world.resources.size())}}};
    }
    V::Array rows; size_t total=0;
    if(section=="entities") {
        for(const auto& e:world.entities) if(e.alive) {
            if(total++<offset||rows.size()==limit) continue;
            rows.emplace_back(V::Object{{"id",number(e.id)},{"persistent_id",V{std::string(e.persistent_id)}},
                {"tag",V{std::string(e.tag)}},{"x",V{double(e.x)}},{"y",V{double(e.y)}},
                {"vx",V{double(e.vx)}},{"vy",V{double(e.vy)}}});
        }
    } else if(section=="resources") {
        total=world.resources.size();
        for(size_t at=offset;at<total&&rows.size()<limit;++at) {
            const auto& r=world.resources[at];
            rows.emplace_back(V::Object{{"name",V{r.name}},{"type",V{r.type}},{"path",V{r.path}},
                {"width",V{double(r.image_width)}},{"height",V{double(r.image_height)}},
                {"font_bytes",number(r.font_bytes.size())},{"cached_glyphs",number(r.glyphs.size())}});
        }
    } else throw std::runtime_error("unknown inspection section");
    return V{V::Object{{"items",V{std::move(rows)}},{"total",number(total)},{"offset",number(offset)}}};
}
