#pragma once
#include "script_binding.h"

// One entry drives registration and machine-readable documentation.
struct ScLuaApi { const char* name; lua_CFunction function; const char* signature; const char* description; };
inline void sc_api_register(lua_State* L,const ScLuaApi* api) {
    for(auto* entry=api;entry->name;++entry) {
        lua_pushcfunction(L,entry->function); lua_setfield(L,-2,entry->name);
    }
}
inline void sc_api_describe(const ScLuaApi* api,const char* prefix,bool comma=true) {
    for(auto* entry=api;entry->name;++entry) {
        std::printf("%s{\"name\":\"%s%s\",\"signature\":\"%s\",\"description\":\"%s\"}",
            comma?",":"",prefix,entry->name,entry->signature,entry->description);
        comma=true;
    }
}
