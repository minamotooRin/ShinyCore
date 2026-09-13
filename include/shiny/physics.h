#pragma once
#include "shiny/core.h"
bool sc_physics_polygon_valid(const float* vertices,int count);
bool sc_physics_body_valid(const ScEntity&);
void sc_physics_sync(ScWorld*);
void sc_physics_step(ScWorld*);
struct ScRay { std::uint32_t id{}; float x{},y{},nx{},ny{},fraction{}; bool hit{}; };
ScRay sc_physics_ray(ScWorld*,float x,float y,float dx,float dy);
std::uint32_t sc_physics_joint(ScWorld*,int kind,std::uint32_t a,std::uint32_t b,float x,float y,float length);
bool sc_physics_joint_destroy(ScWorld*,std::uint32_t id);
