#pragma once
#include "shiny/core.h"
#include "shiny/state.h"
#include <iterator>

// Concrete entity fields, not a general reflection or serialization framework.
// Typed member pointers keep validation, defaults, reads and metadata together.
template<class T> struct ScEntityNumberField {
    const char* name;
    T ScEntity::*member;
    double minimum, maximum;
    T initial;
    const char* description;
};
inline constexpr ScEntityNumberField<float> SC_ENTITY_NUMBERS[]={
    {"x",&ScEntity::x,-1e6,1e6,0,"Left world position in pixels."},
    {"y",&ScEntity::y,-1e6,1e6,0,"Top world position in pixels; positive Y points down."},
    {"w",&ScEntity::w,.001,4096,8,"Unrotated width in pixels."},
    {"h",&ScEntity::h,.001,4096,8,"Unrotated height in pixels."},
    {"vx",&ScEntity::vx,-1e6,1e6,0,"Horizontal velocity in pixels/second."},
    {"vy",&ScEntity::vy,-1e6,1e6,0,"Vertical velocity in pixels/second."},
    {"gravity",&ScEntity::gravity,-100,100,1,"Multiplier of scene gravity."},
    {"glow",&ScEntity::glow,0,1024,0,"Glow radius in pixels."},
    {"angle",&ScEntity::angle,-1e6,1e6,0,"Rotation about bounds center, in radians."},
    {"angular_velocity",&ScEntity::angular_velocity,-1000,1000,0,"Angular velocity in radians/second."},
};
inline constexpr ScEntityNumberField<int> SC_ENTITY_INTEGERS[]={
    {"frame",&ScEntity::frame,0,65535,0,"Zero-based sprite frame; validated against the image grid when loading assets."},
    {"frame_w",&ScEntity::frame_w,0,4096,0,"Sprite frame width; zero uses the whole image."},
    {"frame_h",&ScEntity::frame_h,0,4096,0,"Sprite frame height; zero uses the whole image."},
    {"layer",&ScEntity::layer,-32768,32767,0,"Stable draw layer."},
};
struct ScEntityBoolField { const char* name; bool ScEntity::*member; bool initial; const char* description; };
inline constexpr ScEntityBoolField SC_ENTITY_BOOLEANS[]={
    {"dynamic",&ScEntity::dynamic,false,"Shorthand for a dynamic box body; explicit body configuration takes precedence."},
    {"solid",&ScEntity::solid,true,"Enable body collision filtering and projectile targeting."},
    {"flip_x",&ScEntity::flip_x,false,"Flip the sprite horizontally."},
    {"flip_y",&ScEntity::flip_y,false,"Flip the sprite vertically."},
};
struct ScEntityTextField { const char* name; std::size_t maximum_bytes; const char* description; };
inline constexpr ScEntityTextField SC_ENTITY_TAG{"tag",sizeof(ScEntity::tag)-1,"Exact-match label; NUL bytes are forbidden."};
inline constexpr ScEntityTextField SC_ENTITY_SPRITE{"sprite",sizeof(ScEntity::sprite)-1,"Image resource name or project-relative image path; NUL bytes are forbidden."};
inline constexpr ScEntityTextField SC_ENTITY_IDENTITY{"persistent_id",sizeof(ScEntity::persistent_id)-1,"Stable room-local persistent object ID. Empty is unnamed; nonempty uses ASCII letters, digits, underscore, dash, dot, slash or colon without empty/dot segments. Immutable after spawn."};
inline constexpr std::uint32_t SC_ENTITY_COLOR=0xffffffffu;
inline constexpr auto SC_ENTITY_KEYS=[] {
    std::array<const char*,std::size(SC_ENTITY_NUMBERS)+std::size(SC_ENTITY_INTEGERS)+std::size(SC_ENTITY_BOOLEANS)+6> keys{};
    std::size_t i=0;
    for(const auto& field:SC_ENTITY_NUMBERS) keys[i++]=field.name;
    for(const auto& field:SC_ENTITY_INTEGERS) keys[i++]=field.name;
    for(const auto& field:SC_ENTITY_BOOLEANS) keys[i++]=field.name;
    keys[i++]=SC_ENTITY_TAG.name; keys[i++]=SC_ENTITY_SPRITE.name;
    keys[i++]=SC_ENTITY_IDENTITY.name;
    keys[i++]="color"; keys[i]="body";
    return keys;
}();

ScValue sc_entity_contract();
ScValue sc_entity_read_contract();
ScValue sc_entity_edit_contract();
