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
    {"advanced_render",false},{"devtools",false}
};
