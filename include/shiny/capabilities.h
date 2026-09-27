#pragma once
#include <string_view>

struct ScCapability { std::string_view name; bool available; };
inline constexpr ScCapability SC_CAPABILITIES[]={
    {"core",true},{"physics",true},{"input",true},{"text",true},{"audio",true},
    {"projectiles",true},{"navigation",true},{"save",true},{"settings",true},
#ifdef SC_GRAPHICS_CAPABILITY
    {"graphics",true},
#else
    {"graphics",false},
#endif
#ifdef SC_HAS_NETWORK
    {"network",true},
#else
    {"network",false},
#endif
#ifdef SC_HAS_STREAMING
    {"streaming",true},
#else
    {"streaming",false},
#endif
// Aggregate build capability; acceptance evidence is recorded separately.
#ifdef SC_HAS_ADVANCED_RENDER
    {"advanced_render",true},
    {"materials",true},
    {"postprocessing",true},
    {"geometry_shadows",true},
    {"normal_maps",true},
#else
    {"advanced_render",false},
    {"materials",false},
    {"postprocessing",false},
    {"geometry_shadows",false},
    {"normal_maps",false},
#endif
#ifdef SC_HAS_DEVTOOLS
    {"devtools",true}
#else
    {"devtools",false}
#endif
};
