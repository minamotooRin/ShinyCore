#include "shiny/physics.h"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr float scale=32;
struct Tag { ScEntityId id{}; bool one_way{}; float top{},bottom{},vy{},drop{},vx{}; };
struct Joint { b2JointId native{}; std::uint32_t generation{1}; ScEntityId a{},b{}; };
}
struct ScPhysics {
    b2WorldId world{};
    std::vector<b2BodyId> bodies;
    std::vector<ScEntity> previous;
    std::vector<Tag> tags;
    std::array<Joint,256> joints{};
    b2BodyId terrain{};
    std::vector<Tag> tiles;
    ScMap map{};
    bool map_loaded{};
    std::uint64_t terrain_revision{};
    explicit ScPhysics(std::size_t capacity) : bodies(capacity), previous(capacity), tags(capacity) { auto def=b2DefaultWorldDef(); def.workerCount=0; world=b2CreateWorld(&def); }
    ~ScPhysics() { if(b2World_IsValid(world)) b2DestroyWorld(world); }
};
void ScPhysicsDeleter::operator()(ScPhysics* p) const noexcept { delete p; }
bool sc_physics_polygon_valid(const float* vertices,int count) {
    if(count<3||count>8) return false;
    b2Vec2 points[8];
    for(int j=0;j<count;++j) {
        if(!std::isfinite(vertices[2*j])||!std::isfinite(vertices[2*j+1])) return false;
        points[j]={vertices[2*j]/scale,vertices[2*j+1]/scale};
    }
    auto hull=b2ComputeHull(points,count);
    if(hull.count!=count||!b2ValidateHull(&hull)) return false;
    // Rendering and the solver consume the same cyclic, convex outline.
    for(int i=0;i<count;++i) {
        auto a=points[i],b=points[(i+1)%count]; float side=0;
        for(int j=0;j<count;++j) {
            float cross=(b.x-a.x)*(points[j].y-a.y)-(b.y-a.y)*(points[j].x-a.x);
            if(side*cross<0) return false;
            if(cross) side=cross;
        }
    }
    return true;
}
bool sc_physics_body_valid(const ScEntity& e) {
    if(e.body_type<0||e.body_type>3||e.shape<0||e.shape>3||e.shape_count<0||e.shape_count>4) return false;
    if(!std::isfinite(e.angle)||!std::isfinite(e.angular_velocity)||!std::isfinite(e.density)||e.density<=0||
       !std::isfinite(e.friction)||e.friction<0||!std::isfinite(e.restitution)||e.restitution<0||e.restitution>1) return false;
    if(!e.shape_count&&e.shape==3&&!sc_physics_polygon_valid(e.vertices.data(),e.vertex_count)) return false;
    for(int i=0;i<e.shape_count;++i) {
        const auto& s=e.shapes[static_cast<size_t>(i)];
        if(s.kind<0||s.kind>3||!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.w)||!std::isfinite(s.h)||s.w<=0||s.h<=0) return false;
        if(s.kind==3&&!sc_physics_polygon_valid(s.vertices.data(),s.vertex_count)) return false;
    }
    return true;
}
namespace {
bool contact_filter(b2ShapeId a,b2ShapeId b,b2Manifold*,void*) {
    auto* ta=static_cast<Tag*>(b2Shape_GetUserData(a));
    auto* tb=static_cast<Tag*>(b2Shape_GetUserData(b));
    if(!ta||!tb) return true;
    if(ta->one_way) return tb->drop<=0 && tb->bottom<=ta->top+1.0f && tb->vy>=ta->vy-1;
    if(tb->one_way) return ta->drop<=0 && ta->bottom<=tb->top+1.0f && ta->vy>=tb->vy-1;
    return true;
}
b2ShapeDef shape_def(Tag* tag,const ScEntity* entity=nullptr) {
    auto def=b2DefaultShapeDef(); def.userData=tag; def.enableContactEvents=true;
    def.enableSensorEvents=true; def.enablePreSolveEvents=true;
    if(entity) {
        def.density=entity->density; def.material.friction=entity->body_type?entity->friction:0;
        def.material.restitution=entity->restitution; def.isSensor=entity->sensor;
        def.filter.categoryBits=entity->category; def.filter.maskBits=entity->mask;
    }
    return def;
}
void terrain(ScWorld* w,ScPhysics& p) {
    if(p.map_loaded && p.terrain_revision==w->terrain_revision && p.map.width==w->map.width && p.map.height==w->map.height && p.map.tile_size==w->map.tile_size && p.map.tiles==w->map.tiles) return;
    if(b2Body_IsValid(p.terrain)) b2DestroyBody(p.terrain);
    p.map=w->map; p.map_loaded=true;
    p.terrain_revision=w->terrain_revision;
    p.tiles.clear(); p.tiles.reserve(2*SC_MAX_TILES+4);
    auto body=b2DefaultBodyDef(); p.terrain=b2CreateBody(p.world,&body);
    auto add=[&](float x,float y,float width,float height,bool one_way) {
        p.tiles.push_back({0,one_way,y,y+height,0,0});
        auto def=shape_def(&p.tiles.back());
        auto box=b2MakeOffsetBox(width/(2*scale),height/(2*scale),{(x+width/2)/scale,(y+height/2)/scale},b2Rot_identity);
        b2CreatePolygonShape(p.terrain,&def,&box);
    };
    float tile=static_cast<float>(w->map.tile_size);
    for(int y=0;y<w->map.height;++y) for(int x=0;x<w->map.width;) {
        char value=sc_tile(w,x,y); if(value=='.') { ++x; continue; }
        int end=x+1; while(end<w->map.width&&sc_tile(w,end,y)==value) ++end;
        add(static_cast<float>(x)*tile,static_cast<float>(y)*tile,static_cast<float>(end-x)*tile,value=='='?1:tile,value=='=');
        x=end;
    }
    float width=static_cast<float>(w->map.width)*tile,height=static_cast<float>(w->map.height)*tile;
    for(const auto& shape:w->terrain_shapes) {
        if(!shape.vertex_count) { add(shape.x,shape.y,shape.w,shape.one_way?1:shape.h,shape.one_way); continue; }
        p.tiles.push_back({0,shape.one_way,shape.y,shape.y+shape.h,0,0});
        auto def=shape_def(&p.tiles.back()); b2Vec2 points[8];
        for(int j=0;j<shape.vertex_count;++j) points[j]={(shape.x+shape.vertices[2*j])/scale,(shape.y+shape.vertices[2*j+1])/scale};
        auto hull=b2ComputeHull(points,shape.vertex_count);
        if(hull.count<3) throw std::runtime_error("invalid map collision polygon");
        auto polygon=b2MakePolygon(&hull,0); b2CreatePolygonShape(p.terrain,&def,&polygon);
    }
    add(-tile,-tile,width+2*tile,tile,false); add(-tile,height,width+2*tile,tile,false);
    add(-tile,0,tile,height,false); add(width,0,tile,height,false);
}
int type(const ScEntity& e) { return e.body_type?e.body_type:(e.dynamic?3:0); }
bool shape_changed(const ScEntity& a,const ScEntity& b) {
    return a.id!=b.id||type(a)!=type(b)||a.w!=b.w||a.h!=b.h||a.shape!=b.shape||a.vertices!=b.vertices||a.vertex_count!=b.vertex_count||a.shapes!=b.shapes||a.shape_count!=b.shape_count||
        a.sensor!=b.sensor||a.solid!=b.solid||a.density!=b.density||a.friction!=b.friction||a.restitution!=b.restitution||a.category!=b.category||a.mask!=b.mask;
}
}
void sc_physics_sync(ScWorld* w) {
    if(!w->physics) { w->physics.reset(new ScPhysics(w->entities.size())); b2World_SetPreSolveCallback(w->physics->world,contact_filter,nullptr); }
    auto& p=*w->physics;
    terrain(w,p); b2World_SetGravity(p.world,{0,w->gravity/scale});
    for(size_t i=0;i<w->entities.size();++i) {
        auto& e=w->entities[i]; auto& old=p.previous[i]; auto& id=p.bodies[i];
        bool present=b2Body_IsValid(id);
        if(present && (!e.alive||!type(e)||shape_changed(e,old))) {
            for(auto& joint:p.joints) if(b2Joint_IsValid(joint.native)&&(joint.a==old.id||joint.b==old.id)) {
                b2DestroyJoint(joint.native); joint.native={}; ++joint.generation;
            }
            b2DestroyBody(id); id={}; present=false;
        }
        if(!e.alive||!type(e)) continue;
        if(!sc_physics_body_valid(e)) throw std::runtime_error("invalid body geometry or material");
        p.tags[i]={e.id,e.one_way,e.y,e.y+e.h,e.vy,e.drop_time,e.vx};
        if(!present) {
            auto def=b2DefaultBodyDef(); def.type=type(e)==1?b2_staticBody:type(e)==2?b2_kinematicBody:b2_dynamicBody;
            def.position={(e.x+e.w/2)/scale,(e.y+e.h/2)/scale}; def.rotation=b2MakeRot(e.angle);
            def.fixedRotation=e.fixed_rotation; def.isBullet=e.bullet; def.gravityScale=e.gravity;
            def.userData=&p.tags[i]; id=b2CreateBody(p.world,&def);
            auto shape=shape_def(&p.tags[i],&e); if(!e.solid) shape.filter.maskBits=0;
            if(e.shape_count) {
                for(int j=0;j<e.shape_count;++j) {
                    const auto& s=e.shapes[static_cast<size_t>(j)];
                    b2Vec2 center{(s.x+s.w/2-e.w/2)/scale,(s.y+s.h/2-e.h/2)/scale};
                    if(s.kind==1) { b2Circle circle{center,std::min(s.w,s.h)/(2*scale)}; b2CreateCircleShape(id,&shape,&circle); }
                    else if(s.kind==2) {
                        float radius=std::min(s.w,s.h)/(2*scale),half=std::max(s.w,s.h)/(2*scale)-radius;
                        b2Vec2 axis=s.w>s.h?b2Vec2{half,0}:b2Vec2{0,half};
                        b2Capsule capsule{{center.x-axis.x,center.y-axis.y},{center.x+axis.x,center.y+axis.y},radius}; b2CreateCapsuleShape(id,&shape,&capsule);
                    } else {
                        auto polygon=b2MakeOffsetBox(s.w/(2*scale),s.h/(2*scale),center,b2Rot_identity);
                        if(s.kind==3) {
                            b2Vec2 points[8]; for(int k=0;k<s.vertex_count;++k) points[k]={(s.x+s.vertices[2*k]-e.w/2)/scale,(s.y+s.vertices[2*k+1]-e.h/2)/scale};
                            auto hull=b2ComputeHull(points,s.vertex_count); if(hull.count<3) throw std::runtime_error("invalid compound polygon"); polygon=b2MakePolygon(&hull,0);
                        }
                        b2CreatePolygonShape(id,&shape,&polygon);
                    }
                }
            } else if(e.shape==1) {
                b2Circle circle{{0,0},std::min(e.w,e.h)/(2*scale)}; b2CreateCircleShape(id,&shape,&circle);
            } else if(e.shape==2) {
                float radius=std::min(e.w,e.h)/(2*scale),half=std::max(e.w,e.h)/(2*scale)-radius;
                b2Vec2 axis=e.w>e.h?b2Vec2{half,0}:b2Vec2{0,half};
                b2Capsule capsule{{-axis.x,-axis.y},axis,radius}; b2CreateCapsuleShape(id,&shape,&capsule);
            } else {
                b2Polygon polygon=b2MakeBox(e.w/(2*scale),e.h/(2*scale));
                if(e.shape==3) {
                    b2Vec2 points[8]; for(int j=0;j<e.vertex_count;++j) points[j]={(e.vertices[2*j]-e.w/2)/scale,(e.vertices[2*j+1]-e.h/2)/scale};
                    auto hull=b2ComputeHull(points,e.vertex_count);
                    if(hull.count<3) throw std::runtime_error("invalid convex body polygon");
                    polygon=b2MakePolygon(&hull,0);
                }
                b2CreatePolygonShape(id,&shape,&polygon);
            }
        } else if(e.x!=old.x||e.y!=old.y||e.angle!=old.angle) b2Body_SetTransform(id,{(e.x+e.w/2)/scale,(e.y+e.h/2)/scale},b2MakeRot(e.angle));
        if(!present||e.vx!=old.vx||e.vy!=old.vy) b2Body_SetLinearVelocity(id,{e.vx/scale,e.vy/scale});
        if(!present||e.angular_velocity!=old.angular_velocity) b2Body_SetAngularVelocity(id,e.angular_velocity);
        b2Body_SetGravityScale(id,e.gravity); b2Body_SetFixedRotation(id,e.fixed_rotation); b2Body_SetBullet(id,e.bullet);
        if(e.force_x||e.force_y) b2Body_ApplyForceToCenter(id,{e.force_x/scale,e.force_y/scale},true);
        if(e.impulse_x||e.impulse_y) b2Body_ApplyLinearImpulseToCenter(id,{e.impulse_x/scale,e.impulse_y/scale},true);
        e.force_x=e.force_y=e.impulse_x=e.impulse_y=0;
        old=e;
    }
}
void sc_physics_step(ScWorld* w) {
    sc_physics_sync(w); auto& p=*w->physics;
    b2World_Step(p.world,SC_DT,4); w->contact_count=0;
    for(size_t i=0;i<w->entities.size();++i) {
        auto& e=w->entities[i]; auto id=p.bodies[i];
        e.grounded=false; e.support=0; e.normal_x=e.normal_y=0;
        if(!e.alive||!b2Body_IsValid(id)) continue;
        auto position=b2Body_GetPosition(id), velocity=b2Body_GetLinearVelocity(id);
        e.x=position.x*scale-e.w/2; e.y=position.y*scale-e.h/2;
        e.vx=velocity.x*scale; e.vy=velocity.y*scale;
        e.angle=b2Rot_GetAngle(b2Body_GetRotation(id)); e.angular_velocity=b2Body_GetAngularVelocity(id);
        if(b2Body_GetContactCapacity(id)>64) throw std::runtime_error("body contact capacity exceeded (64)");
        b2ContactData contacts[64]; int count=b2Body_GetContactData(id,contacts,64);
        for(int j=0;j<count;++j) {
            const auto& c=contacts[j];
            auto* a=static_cast<Tag*>(b2Shape_GetUserData(c.shapeIdA)); auto* b=static_cast<Tag*>(b2Shape_GetUserData(c.shapeIdB));
            if(!a||!b) continue;
            bool first=a->id==e.id; float nx=c.manifold.normal.x*(first?-1:1),ny=c.manifold.normal.y*(first?-1:1);
            const auto* support=first?b:a;
            float separating_speed=(e.vx-support->vx)*nx+(e.vy-support->vy)*ny;
            if(ny<-.5f && e.drop_time<=0 && separating_speed<=1) { e.grounded=true; e.normal_x=nx; e.normal_y=ny; e.support=support->id; }
            if(first||a->id==0) {
                if(w->contact_count==static_cast<int>(w->contacts.size())) throw std::runtime_error("contact capacity exceeded (1024)");
                w->contacts[w->contact_count++]={a->id,b->id,c.manifold.normal.x,c.manifold.normal.y,false};
            }
        }
        e.drop_time=std::max(0.0f,e.drop_time-SC_DT); p.previous[i]=e;
    }
    auto sensors=b2World_GetSensorEvents(p.world);
    auto sensor=[&](b2ShapeId first,b2ShapeId second,int phase) {
        if(!b2Shape_IsValid(first)||!b2Shape_IsValid(second)) return;
        auto a=static_cast<Tag*>(b2Shape_GetUserData(first)),b=static_cast<Tag*>(b2Shape_GetUserData(second));
        if(!a||!b) return;
        if(w->contact_count==static_cast<int>(w->contacts.size())) throw std::runtime_error("contact capacity exceeded (1024)");
        w->contacts[w->contact_count++]={a->id,b->id,0,0,true,phase};
    };
    for(int i=0;i<sensors.beginCount;++i) sensor(sensors.beginEvents[i].sensorShapeId,sensors.beginEvents[i].visitorShapeId,1);
    for(int i=0;i<sensors.endCount;++i) sensor(sensors.endEvents[i].sensorShapeId,sensors.endEvents[i].visitorShapeId,2);
    std::stable_sort(w->contacts.begin(),w->contacts.begin()+w->contact_count,[](const ScContact& a,const ScContact& b){ return a.a!=b.a?a.a<b.a:a.b!=b.b?a.b<b.b:a.phase<b.phase; });
}
ScRay sc_physics_ray(ScWorld* w,float x,float y,float dx,float dy) {
    sc_physics_sync(w);
    auto hit=b2World_CastRayClosest(w->physics->world,{x/scale,y/scale},{dx/scale,dy/scale},b2DefaultQueryFilter());
    if(!hit.hit) return {};
    auto* tag=static_cast<Tag*>(b2Shape_GetUserData(hit.shapeId));
    return {tag?tag->id:0,hit.point.x*scale,hit.point.y*scale,hit.normal.x,hit.normal.y,hit.fraction,true};
}
std::uint32_t sc_physics_joint(ScWorld* w,int kind,ScEntityId a,ScEntityId b,float x,float y,float length) {
    sc_physics_sync(w); auto& p=*w->physics;
    if(!sc_entity(w,a)||!sc_entity(w,b)||a==b) return 0;
    auto first=p.bodies[sc_entity_slot(a)],second=p.bodies[sc_entity_slot(b)];
    if(!b2Body_IsValid(first)||!b2Body_IsValid(second)) return 0;
    for(size_t i=0;i<p.joints.size();++i) if(!b2Joint_IsValid(p.joints[i].native)) {
        auto& slot=p.joints[i]; b2Vec2 point{x/scale,y/scale};
        if(kind==0) {
            auto def=b2DefaultDistanceJointDef(); def.bodyIdA=first; def.bodyIdB=second;
            def.localAnchorA=b2Body_GetLocalPoint(first,point); def.localAnchorB=b2Body_GetLocalPoint(second,point);
            def.length=std::max(length/scale,.005f); slot.native=b2CreateDistanceJoint(p.world,&def);
        } else if(kind==1) {
            auto def=b2DefaultRevoluteJointDef(); def.bodyIdA=first; def.bodyIdB=second;
            def.localAnchorA=b2Body_GetLocalPoint(first,point); def.localAnchorB=b2Body_GetLocalPoint(second,point);
            slot.native=b2CreateRevoluteJoint(p.world,&def);
        } else {
            auto def=b2DefaultPrismaticJointDef(); def.bodyIdA=first; def.bodyIdB=second;
            def.localAnchorA=b2Body_GetLocalPoint(first,point); def.localAnchorB=b2Body_GetLocalPoint(second,point);
            def.localAxisA={0,1}; def.enableLimit=true; def.lowerTranslation=0; def.upperTranslation=length/scale;
            slot.native=b2CreatePrismaticJoint(p.world,&def);
        }
        slot.a=a; slot.b=b; return (slot.generation<<8)|static_cast<std::uint32_t>(i);
    }
    return 0;
}
bool sc_physics_joint_destroy(ScWorld* w,std::uint32_t id) {
    if(!w->physics) return false;
    auto& slot=w->physics->joints[id&255];
    if((id>>8)!=slot.generation||!b2Joint_IsValid(slot.native)) return false;
    b2DestroyJoint(slot.native); slot.native={}; ++slot.generation; return true;
}
