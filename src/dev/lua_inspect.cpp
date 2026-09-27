#include "lua_inspect.h"
#include "shiny/script.h"
#include <lua.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <stdexcept>

std::string sc_debug_source_path(const ScScript& script,const char* source) {
    if(!source||source[0]!='@') return {};
    auto relative=std::filesystem::path(source+1).lexically_normal().lexically_relative(std::filesystem::path(script.root).lexically_normal()).generic_string();
    if(!sc_script_validate_path(relative.c_str())) return {};
    return relative;
}
namespace {
using V=ScValue;
V text(const char* value) { return V{std::string(value)}; }
V number(std::uint64_t value) { return V{static_cast<double>(value)}; }
std::size_t integer(const V& request,const char* name,std::size_t fallback,std::size_t maximum) {
    const auto* value=request.get(name); if(!value) return fallback;
    const double n=value->number(NAN);
    if(!std::isfinite(n)||n<0||n>static_cast<double>(maximum)||std::floor(n)!=n)
        throw std::runtime_error(std::string("invalid ")+name);
    return static_cast<std::size_t>(n);
}
V lua_preview(lua_State* L,int index) {
    switch(lua_type(L,index)) {
    case LUA_TNIL: return {};
    case LUA_TBOOLEAN: return V{lua_toboolean(L,index)!=0};
    case LUA_TSTRING: {
        size_t length=0; const char* value=lua_tolstring(L,index,&length);
        size_t end=std::min(length,size_t(512));
        while(end<length&&end&&((static_cast<unsigned char>(value[end])&0xc0)==0x80)) --end;
        if(end==length) return V{std::string(value,length)};
        return V{V::Object{{"type",text("string")},{"preview",V{std::string(value,end)}},{"bytes",number(length)}}};
    }
    case LUA_TNUMBER: {
        if(lua_isinteger(L,index)) return V{V::Object{{"type",text("integer")},{"value",V{std::to_string(lua_tointeger(L,index))}}}};
        const auto value=lua_tonumber(L,index);
        return std::isfinite(value)?V{double(value)}:V{V::Object{{"type",text("number")},{"value",text("nonfinite")}}};
    }
    default: return V{V::Object{{"type",text(lua_typename(L,lua_type(L,index)))}}};
    }
}
struct StackScope {
    lua_State* vm;
    int top;
    ~StackScope() { lua_settop(vm,top); }
};
struct Key {
    const std::string* string=nullptr;
    int type=LUA_TNIL;
    lua_Integer integer=0;
    double number=0;
    bool boolean=false,integral=false;
    explicit Key(const V& value) {
        if((string=std::get_if<std::string>(&value.data))) type=LUA_TSTRING;
        else if(const auto* b=std::get_if<bool>(&value.data)) { type=LUA_TBOOLEAN; boolean=*b; }
        else if(const auto* n=std::get_if<double>(&value.data)) {
            if(!std::isfinite(*n)||std::abs(*n)>4503599627370495.0)
                throw std::runtime_error("numeric path key exceeds exact JSON range; use typed integer");
            type=LUA_TNUMBER; number=*n; integral=std::floor(*n)==*n;
            if(integral) integer=static_cast<lua_Integer>(*n);
        } else if(const auto* object=std::get_if<V::Object>(&value.data);object&&object->size()==2) {
            const auto* kind=value.get("type"); const auto* digits=value.get("value");
            const auto* s=digits?std::get_if<std::string>(&digits->data):nullptr;
            if(kind&&kind->text()=="integer"&&s) {
                const auto parsed=std::from_chars(s->data(),s->data()+s->size(),integer);
                if(parsed.ec==std::errc{}&&parsed.ptr==s->data()+s->size()) {
                    type=LUA_TNUMBER; integral=true;
                }
            }
        }
        if(type==LUA_TNIL) throw std::runtime_error("invalid local path key");
    }
    bool matches(lua_State* L,int index) const {
        if(lua_type(L,index)!=type) return false;
        if(type==LUA_TBOOLEAN) return (lua_toboolean(L,index)!=0)==boolean;
        if(type==LUA_TSTRING) {
            size_t size=0; const char* bytes=lua_tolstring(L,index,&size);
            return std::string_view(bytes,size)==*string;
        }
        return integral?(lua_isinteger(L,index)&&lua_tointeger(L,index)==integer):
            (!lua_isinteger(L,index)&&lua_tonumber(L,index)==number);
    }
};
bool next(lua_State* L,int table,std::size_t& scanned) {
    if(!lua_next(L,table)) return false;
    if(scanned==SC_DEBUG_SCAN_LIMIT) throw std::runtime_error("local table scan budget exhausted (65536 entries)");
    ++scanned; return true;
}
V inspect_local(lua_State* L,lua_Debug& frame,const V& request,std::size_t offset,std::size_t limit) {
    const auto variable=integer(request,"variable",0,1023);
    if(!lua_getlocal(L,&frame,static_cast<int>(variable+1))) throw std::runtime_error("local variable unavailable");
    std::size_t scanned=0;
    if(const auto* path=request.get("path")) {
        const auto* parts=std::get_if<V::Array>(&path->data);
        if(!parts||parts->size()>SC_DEBUG_PATH_LIMIT) throw std::runtime_error("local path requires at most 16 Lua keys");
        for(const auto& part:*parts) {
            const Key key(part);
            if(!lua_istable(L,-1)) throw std::runtime_error("local path traverses a non-table");
            const int table=lua_gettop(L); bool found=false;
            // Compare existing keys: pushing a new Lua string could allocate/raise here.
            lua_pushnil(L);
            while(next(L,table,scanned)) {
                if(key.matches(L,-2)) { lua_remove(L,-2); lua_remove(L,table); found=true; break; }
                lua_pop(L,1);
            }
            if(!found) throw std::runtime_error("local path not found");
        }
    }
    if(!lua_istable(L,-1)) return V{V::Object{{"value",lua_preview(L,-1)}}};
    V::Array items; V next_offset; std::size_t at=0;
    const int table=lua_gettop(L); lua_pushnil(L);
    while(next(L,table,scanned)) {
        if(at++>=offset) {
            if(items.size()==limit) { next_offset=number(offset+items.size()); break; }
            items.emplace_back(V::Object{{"key",lua_preview(L,-2)},{"value",lua_preview(L,-1)}});
        }
        lua_pop(L,1);
    }
    return V{V::Object{{"type",text("table")},{"items",V{std::move(items)}},
                      {"offset",number(offset)},{"next_offset",std::move(next_offset)}}};
}
} // namespace
ScValue sc_debug_stack(ScScript& script,lua_State* L,const V& request,bool locals) {
    if(!lua_checkstack(L,4)) throw std::runtime_error("Lua inspection stack capacity exhausted");
    const StackScope stack{L,lua_gettop(L)};
    const bool expand=locals&&request.get("variable");
    if(request.get("path")&&!expand) throw std::runtime_error("local path requires variable");
    if(!locals&&request.get("variable")) throw std::runtime_error("variable requires locals command");
    const auto offset=integer(request,"offset",0,expand?SC_DEBUG_SCAN_LIMIT-1:1024),limit=integer(request,"limit",32,128);
    if(!limit) throw std::runtime_error("limit must be 1..128");
    V::Array items; lua_Debug frame{};
    if(locals) {
        const auto level=integer(request,"level",0,1023);
        if(!lua_getstack(L,static_cast<int>(level),&frame)) throw std::runtime_error("stack level unavailable");
        if(expand) return inspect_local(L,frame,request,offset,limit);
        for(size_t at=offset;at<offset+limit;++at) {
            const char* name=lua_getlocal(L,&frame,static_cast<int>(at+1)); if(!name) break;
            V value=lua_preview(L,-1);
            lua_pop(L,1);
            items.emplace_back(V::Object{{"index",number(at)},{"name",text(name)},{"value",std::move(value)}});
        }
    } else {
        for(size_t level=offset;level<offset+limit&&level<1024;++level) {
            if(!lua_getstack(L,static_cast<int>(level),&frame)) break;
            lua_getinfo(L,"nSl",&frame);
            V::Object item{{"level",number(level)},{"kind",text(frame.what?frame.what:"")}};
            if(frame.name) item.emplace("name",text(frame.name));
            const auto path=sc_debug_source_path(script,frame.source);
            if(!path.empty()) item.emplace("file",V{path});
            if(frame.currentline>0) item.emplace("line",number(static_cast<uint64_t>(frame.currentline)));
            items.emplace_back(std::move(item));
        }
    }
    return V{V::Object{{"items",V{std::move(items)}},{"offset",number(offset)}}};
}
