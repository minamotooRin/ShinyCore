#include "shiny/physics.h"
#include "shiny/navigation.h"
#include "shiny/attachment.h"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <map>
#include <climits>

namespace {
constexpr float scale=32;
struct Tag { ScEntityId id{}; bool one_way{}; float top{},bottom{},vy{},drop{},vx{}; };
struct Joint { b2JointId native{}; std::uint32_t generation{1}; ScEntityId a{},b{}; int kind{}; float length{}; ScJointControl control{}; };
struct TerrainChunk {
    b2BodyId body{};
    std::vector<ScTerrainShape> shapes;
    std::vector<Tag> tags;
    ~TerrainChunk() { if(b2Body_IsValid(body)) b2DestroyBody(body); }
};
using ChunkKey=std::pair<int,int>;
}
struct ScPhysics {
    b2WorldId world{};
    std::vector<b2BodyId> bodies;
    std::vector<ScEntity> previous;
    std::vector<Tag> tags;
    std::vector<unsigned char> query_seen;
    std::vector<ScEntityId> query_ids;
    std::size_t query_count{};
    std::array<Joint,256> joints{};
    std::map<ChunkKey,std::unique_ptr<TerrainChunk>> terrain;
    std::uint64_t terrain_replacements{};
    ScMap map{};
    bool map_loaded{};
    std::uint64_t terrain_revision{};
    explicit ScPhysics(std::size_t capacity) : bodies(capacity), previous(capacity), tags(capacity),
        query_seen(capacity+1), query_ids(capacity+1) { auto def=b2DefaultWorldDef(); def.workerCount=0; world=b2CreateWorld(&def); }
    ~ScPhysics() { terrain.clear(); if(b2World_IsValid(world)) b2DestroyWorld(world); }
};
void ScPhysicsDeleter::operator()(ScPhysics* p) const noexcept { delete p; }
ScTerrainStats sc_physics_terrain_stats(const ScWorld& w) noexcept {
    return w.physics?ScTerrainStats{w.physics->terrain.size(),w.physics->terrain_replacements}:ScTerrainStats{};
}
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
    if(p.map_loaded && p.terrain_revision==w->terrain_revision && p.map.bounded==w->map.bounded && p.map.width==w->map.width && p.map.height==w->map.height && p.map.tile_size==w->map.tile_size && p.map.tiles==w->map.tiles) return;
    // Build proposed geometry first. Chunk identity is independent of solver IDs;
    // unchanged chunks retain their bodies and contacts across local map edits.
    std::map<ChunkKey,std::vector<ScTerrainShape>> desired;
    const float tile=static_cast<float>(w->map.tile_size), span=tile*32;
    if(tile<=0) throw std::runtime_error("terrain requires positive tile size");
    auto key=[&](float x,float y) { return ChunkKey{static_cast<int>(std::floor(x/span)),static_cast<int>(std::floor(y/span))}; };
    auto add=[&](ChunkKey chunk,float x,float y,float width,float height,bool one_way) {
        desired[chunk].push_back({x,y,width,height,one_way});
    };
    for(int y=0;y<w->map.height;++y) for(int x=0;x<w->map.width;) {
        char value=sc_tile(w,x,y); if(value=='.') { ++x; continue; }
        int end=x+1,limit=std::min(w->map.width,(x/32+1)*32);
        while(end<limit&&sc_tile(w,end,y)==value) ++end;
        add({x/32,y/32},x*tile,y*tile,(end-x)*tile,value=='='?1:tile,value=='=');
        x=end;
    }
    for(const auto& shape:w->terrain_shapes) {
        for(float value:{shape.x,shape.y,shape.w,shape.h})
            if(!std::isfinite(value)||std::fabs(value)>1e7f)
                throw std::runtime_error("map collision bounds outside supported range");
        if(shape.w<=0||shape.h<=0) throw std::runtime_error("map collision requires positive dimensions");
        desired[key(shape.x,shape.y)].push_back(shape);
    }
    const float width=w->map.width*tile,height=w->map.height*tile;
    // Room bounds have a separate owner, unaffected by interior edits.
    const ChunkKey bounds{INT_MAX,INT_MAX};
    if(w->map.bounded) {
        add(bounds,-tile,-tile,width+2*tile,tile,false); add(bounds,-tile,height,width+2*tile,tile,false);
        add(bounds,-tile,0,tile,height,false); add(bounds,width,0,tile,height,false);
    }
    std::map<ChunkKey,std::unique_ptr<TerrainChunk>> prepared;
    for(auto& [chunk,shapes]:desired) {
        auto old=p.terrain.find(chunk);
        if(old!=p.terrain.end()&&old->second->shapes==shapes) continue;
        auto candidate=std::make_unique<TerrainChunk>();
        candidate->shapes=std::move(shapes); candidate->tags.reserve(candidate->shapes.size());
        auto body=b2DefaultBodyDef(); candidate->body=b2CreateBody(p.world,&body);
        for(const auto& shape:candidate->shapes) {
            candidate->tags.push_back({0,shape.one_way,shape.y,shape.y+shape.h,0,0});
            auto def=shape_def(&candidate->tags.back()); b2Polygon polygon;
            if(!shape.vertex_count) {
                const float h=shape.one_way?1:shape.h;
                polygon=b2MakeOffsetBox(shape.w/(2*scale),h/(2*scale),
                    {(shape.x+shape.w/2)/scale,(shape.y+h/2)/scale},b2Rot_identity);
            } else {
                if(!sc_physics_polygon_valid(shape.vertices.data(),shape.vertex_count))
                    throw std::runtime_error("invalid map collision polygon");
                b2Vec2 points[8];
                for(int j=0;j<shape.vertex_count;++j)
                    points[j]={(shape.x+shape.vertices[2*j])/scale,(shape.y+shape.vertices[2*j+1])/scale};
                auto hull=b2ComputeHull(points,shape.vertex_count);
                if(hull.count<3||!b2ValidateHull(&hull))
                    throw std::runtime_error("map collision polygon loses precision at world position");
                polygon=b2MakePolygon(&hull,0);
            }
            b2CreatePolygonShape(candidate->body,&def,&polygon);
        }
        prepared.emplace(chunk,std::move(candidate));
    }
    // All allocating preparation has succeeded. Node transfer and removal commit
    // without allocating, and no simulation step observes both generations.
    p.terrain_replacements+=prepared.size();
    for(auto it=p.terrain.begin();it!=p.terrain.end();)
        if(!desired.contains(it->first)||prepared.contains(it->first)) it=p.terrain.erase(it);
        else ++it;
    p.terrain.merge(prepared);
    p.map=w->map; p.map_loaded=true; p.terrain_revision=w->terrain_revision;
}

int type(const ScEntity& e) { return e.body_type?e.body_type:(e.dynamic?3:0); }
bool shape_changed(const ScEntity& a,const ScEntity& b) {
    return a.id!=b.id||type(a)!=type(b)||a.w!=b.w||a.h!=b.h||a.shape!=b.shape||a.vertices!=b.vertices||a.vertex_count!=b.vertex_count||a.shapes!=b.shapes||a.shape_count!=b.shape_count||
        a.sensor!=b.sensor||a.solid!=b.solid||a.density!=b.density||a.friction!=b.friction||a.restitution!=b.restitution||a.category!=b.category||a.mask!=b.mask;
}
}
std::expected<void,std::string> sc_physics_replace_terrain(ScWorld& w,std::span<const ScTerrainShape> shapes,
                                                            std::span<ScEntity> entering,std::span<const std::size_t> parents) {
    if(shapes.size()>SC_MAX_TILES) return std::unexpected("stream terrain exceeds 16384 shapes");
    try {
        if(auto result=sc_spawn_preflight(w,entering);!result) return std::unexpected(std::string(result.error()));
        if(auto result=sc_attachment_batch_preflight(entering,parents);!result) return std::unexpected(std::string(result.error()));
        for(const auto& shape:shapes) {
            for(float value:{shape.x,shape.y,shape.w,shape.h})
                if(!std::isfinite(value)||std::fabs(value)>1e6f) throw std::runtime_error("stream terrain bounds outside range");
            if(shape.w<.16f||shape.h<.16f||std::fabs(shape.x+shape.w)>1e6f||std::fabs(shape.y+shape.h)>1e6f)
                throw std::runtime_error("invalid stream terrain dimensions");
            if(shape.vertex_count>=3&&shape.vertex_count<=8) for(int i=0;i<shape.vertex_count*2;++i) {
                const float point=shape.vertices[static_cast<std::size_t>(i)];
                if(!std::isfinite(point)||std::fabs(point)>1e6f||std::fabs(point+(i%2?shape.y:shape.x))>1e6f)
                    throw std::runtime_error("stream terrain vertex outside range");
            }
            if(shape.vertex_count&&!sc_physics_polygon_valid(shape.vertices.data(),shape.vertex_count))
                throw std::runtime_error("invalid stream terrain polygon");
        }
        const auto blocked=sc_navigation_patch(w.map,w.terrain_shapes,shapes).blocked;
        if(blocked!=w.map.navigation_blocked&&w.map.navigation_revision==UINT64_MAX)
            throw std::runtime_error("navigation revision exhausted");
        if(w.terrain_revision==UINT64_MAX) throw std::runtime_error("terrain revision exhausted");
        std::vector<ScTerrainShape> candidate(shapes.begin(),shapes.end());
        if(!w.physics) {
            w.physics.reset(new ScPhysics(w.entities.size()));
            b2World_SetPreSolveCallback(w.physics->world,contact_filter,nullptr);
        }
        auto previous=std::move(w.terrain_shapes);
        const auto revision=w.terrain_revision; const bool bounded=w.map.bounded;
        w.terrain_shapes=std::move(candidate); w.map.bounded=false; ++w.terrain_revision;
        try { terrain(&w,*w.physics); }
        catch(...) { w.terrain_shapes=std::move(previous); w.map.bounded=bounded; w.terrain_revision=revision; throw; }
        if(blocked!=w.map.navigation_blocked) { w.map.navigation_blocked=blocked; ++w.map.navigation_revision; }
        // Terrain publication cannot alter entity slots or identity records.
        if(auto result=sc_spawn_many(w,entering,parents);!result) throw std::runtime_error(result.error());
        return {};
    } catch(const std::exception& error) { return std::unexpected(std::string(error.what())); }
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
namespace {
bool coordinate(float x) { return std::isfinite(x)&&std::fabs(x)<=1e6f; }
b2QueryFilter query_filter() { return {UINT32_MAX,UINT32_MAX}; }
b2ShapeProxy query_proxy(const ScQueryShape& shape) {
    if(shape.count<1||shape.count>8||!std::isfinite(shape.radius)||shape.radius<0||shape.radius>4096||
       (shape.count<3&&shape.radius==0)) throw std::runtime_error("invalid query shape count or radius");
    b2Vec2 points[8];
    for(int i=0;i<shape.count;++i) {
        float x=shape.points[2*i],y=shape.points[2*i+1];
        if(!coordinate(x)||!coordinate(y)) throw std::runtime_error("query coordinates outside -1e6..1e6");
        points[i]={x/scale,y/scale};
    }
    if(shape.count==2&&points[0].x==points[1].x&&points[0].y==points[1].y)
        throw std::runtime_error("query capsule requires distinct endpoints");
    if(shape.count>=3&&!sc_physics_polygon_valid(shape.points.data(),shape.count))
        throw std::runtime_error("query polygon requires 3..8 cyclic convex vertices");
    return b2MakeProxy(points,shape.count,shape.radius/scale);
}
bool overlap_result(b2ShapeId shape,void* context) {
    auto& p=*static_cast<ScPhysics*>(context);
    auto* tag=static_cast<Tag*>(b2Shape_GetUserData(shape));
    if(!tag) return true;
    auto slot=tag->id?sc_entity_slot(tag->id):p.bodies.size();
    if(!p.query_seen[slot]) { p.query_seen[slot]=1; p.query_ids[p.query_count++]=tag->id; }
    return true;
}
float cast_result(b2ShapeId shape,b2Vec2 point,b2Vec2 normal,float fraction,void* context) {
    auto& best=*static_cast<ScRay*>(context);
    auto* tag=static_cast<Tag*>(b2Shape_GetUserData(shape));
    if(!tag) return 1;
    ScRay hit{tag->id,point.x*scale,point.y*scale,normal.x,normal.y,fraction,true};
    if(!best.hit||std::tie(hit.fraction,hit.id,hit.x,hit.y,hit.nx,hit.ny)<
                  std::tie(best.fraction,best.id,best.x,best.y,best.nx,best.ny)) best=hit;
    // Clip beyond the best hit, retaining exact ties even with exclusive cast limits.
    return std::nextafter(best.fraction,1.0f);
}
float ray_result(b2ShapeId shape,b2Vec2 point,b2Vec2 normal,float fraction,void* context) {
    return fraction==0?-1:cast_result(shape,point,normal,fraction,context);
}
}
std::span<const ScEntityId> sc_physics_overlap(ScWorld* w,const ScQueryShape& shape) {
    auto proxy=query_proxy(shape);
    sc_physics_sync(w); auto& p=*w->physics;
    std::fill(p.query_seen.begin(),p.query_seen.end(),0); p.query_count=0;
    b2World_OverlapShape(p.world,&proxy,query_filter(),overlap_result,&p);
    std::sort(p.query_ids.begin(),p.query_ids.begin()+static_cast<std::ptrdiff_t>(p.query_count));
    return {p.query_ids.data(),p.query_count};
}
ScRay sc_physics_sweep(ScWorld* w,const ScQueryShape& shape,float dx,float dy) {
    auto proxy=query_proxy(shape);
    if(!coordinate(dx)||!coordinate(dy)) throw std::runtime_error("query translation outside -1e6..1e6");
    sc_physics_sync(w); ScRay hit;
    b2World_CastShape(w->physics->world,&proxy,{dx/scale,dy/scale},query_filter(),cast_result,&hit);
    return hit;
}
ScRay sc_physics_ray(ScWorld* w,float x,float y,float dx,float dy) {
    if(!coordinate(x)||!coordinate(y)||!coordinate(dx)||!coordinate(dy)) throw std::runtime_error("ray coordinates outside -1e6..1e6");
    sc_physics_sync(w); ScRay hit;
    b2World_CastRay(w->physics->world,{x/scale,y/scale},{dx/scale,dy/scale},query_filter(),ray_result,&hit);
    return hit;
}
ScJointId sc_physics_joint(ScWorld* w,int kind,ScEntityId a,ScEntityId b,float x,float y,float length,
                             std::optional<std::array<float,2>> anchor_b) {
    if(w->epoch==0||w->epoch>0xfffff||kind<0||kind>2||!coordinate(x)||!coordinate(y)||!std::isfinite(length)||length<.01f||length>4096) return 0;
    if(anchor_b&&(kind!=0||!coordinate((*anchor_b)[0])||!coordinate((*anchor_b)[1]))) return 0;
    sc_physics_sync(w); auto& p=*w->physics;
    if(!sc_entity(w,a)||!sc_entity(w,b)||a==b) return 0;
    auto first=p.bodies[sc_entity_slot(a)],second=p.bodies[sc_entity_slot(b)];
    if(!b2Body_IsValid(first)||!b2Body_IsValid(second)) return 0;
    for(size_t i=0;i<p.joints.size();++i) if(!b2Joint_IsValid(p.joints[i].native)&&p.joints[i].generation<=0xffffff) {
        auto& slot=p.joints[i]; b2Vec2 point{x/scale,y/scale};
        if(kind==0) {
            auto def=b2DefaultDistanceJointDef(); def.bodyIdA=first; def.bodyIdB=second;
            auto second_point=anchor_b?b2Vec2{(*anchor_b)[0]/scale,(*anchor_b)[1]/scale}:point;
            def.localAnchorA=b2Body_GetLocalPoint(first,point); def.localAnchorB=b2Body_GetLocalPoint(second,second_point);
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
        slot.a=a; slot.b=b; slot.kind=kind; slot.length=std::max(length,.16f);
        slot.control={kind==2,false,kind==0?.16f:kind==1?-3.14159265f:0,kind==0?4096:kind==1?3.14159265f:length,0,0};
        return (ScJointId{w->epoch}<<32)|(ScJointId{slot.generation}<<8)|i;
    }
    return 0;
}
bool sc_physics_joint_destroy(ScWorld* w,ScJointId id) {
    if(!w->physics||id>SC_ID_MAX||(id>>32)!=w->epoch) return false;
    sc_physics_sync(w);
    auto& slot=w->physics->joints[id&255];
    if(((id>>8)&0xffffff)!=slot.generation||!b2Joint_IsValid(slot.native)) return false;
    b2DestroyJoint(slot.native); slot.native={}; ++slot.generation; return true;
}
namespace {
Joint* joint_slot(ScWorld* w,ScJointId id) {
    if(id>SC_ID_MAX||(id>>32)!=w->epoch) return nullptr;
    sc_physics_sync(w);
    auto& slot=w->physics->joints[id&255];
    return ((id>>8)&0xffffff)==slot.generation&&b2Joint_IsValid(slot.native)?&slot:nullptr;
}
}
std::expected<ScJointControl,const char*> sc_physics_joint_control(ScWorld* w,ScJointId id) {
    auto* slot=joint_slot(w,id);
    if(!slot) return std::unexpected("stale joint ID");
    return slot->control;
}
std::expected<void,const char*> sc_physics_joint_control(ScWorld* w,ScJointId id,const ScJointControl& c) {
    auto* slot=joint_slot(w,id);
    if(!slot) return std::unexpected("stale joint ID");
    const float bound=slot->kind==1?3.14159265f:4096;
    if(!std::isfinite(c.lower)||!std::isfinite(c.upper)||c.lower>c.upper||c.lower< -bound||c.upper>bound||
       (slot->kind==0&&c.lower<.16f)||!std::isfinite(c.speed)||std::fabs(c.speed)>1e6f||
       !std::isfinite(c.max_effort)||c.max_effort<0||c.max_effort>1e9f)
        return std::unexpected("invalid joint control range");
    if(c==slot->control) return {};
    auto native=slot->native;
    if(slot->kind==0) {
        b2DistanceJoint_SetLength(native,(c.limit&&c.lower==c.upper?c.lower:slot->length)/scale);
        b2DistanceJoint_EnableSpring(native,c.limit||c.motor);
        b2DistanceJoint_SetSpringHertz(native,0);
        b2DistanceJoint_SetLengthRange(native,c.lower/scale,c.upper/scale);
        b2DistanceJoint_EnableLimit(native,c.limit);
        b2DistanceJoint_SetMotorSpeed(native,c.speed/scale);
        b2DistanceJoint_SetMaxMotorForce(native,c.max_effort/scale);
        b2DistanceJoint_EnableMotor(native,c.motor);
    } else if(slot->kind==1) {
        b2RevoluteJoint_SetLimits(native,c.lower,c.upper);
        b2RevoluteJoint_EnableLimit(native,c.limit);
        b2RevoluteJoint_SetMotorSpeed(native,c.speed);
        b2RevoluteJoint_SetMaxMotorTorque(native,c.max_effort/(scale*scale));
        b2RevoluteJoint_EnableMotor(native,c.motor);
    } else {
        b2PrismaticJoint_SetLimits(native,c.lower/scale,c.upper/scale);
        b2PrismaticJoint_EnableLimit(native,c.limit);
        b2PrismaticJoint_SetMotorSpeed(native,c.speed/scale);
        b2PrismaticJoint_SetMaxMotorForce(native,c.max_effort/scale);
        b2PrismaticJoint_EnableMotor(native,c.motor);
    }
    slot->control=c; b2Joint_WakeBodies(native);
    return {};
}
