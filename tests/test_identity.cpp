#include "shiny/identity.h"
#include "shiny/core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(value) do { if(!(value)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#value); std::exit(1); } } while(false)

int main() {
    for(const char* name:{"map:forest/chest.1","npc_2","Door-1"}) CHECK(sc_identity_name_valid(name));
    for(const char* name:{"","/a","a/","a//b","a/../b",".","a b","a\\b"}) CHECK(!sc_identity_name_valid(name));
    ScIdentities index(2);
    auto* storage=index.records().data();
    CHECK(index.declare("a")); CHECK(index.declare("a")); CHECK(index.size()==1);
    CHECK(index.bind("a",42)); CHECK(!index.bind("a",43));
    index.release("a",43,true); CHECK(index.find("a")->entity==42);
    index.release("a",42,false); CHECK(index.find("a")->status==ScIdentityStatus::unloaded);
    CHECK(index.erase("b")); CHECK(index.find("b")->status==ScIdentityStatus::deleted);
    CHECK(!index.declare("c")); CHECK(index.find("c")==nullptr);
    CHECK(index.bind("b",44)); CHECK(!index.erase("b"));
    CHECK(index.records().data()==storage && index.size()==2);
    ScIdentities disabled(0); CHECK(!disabled.declare("a")); CHECK(!disabled.find("a"));

    ScWorld world; sc_world_init(&world,42);
    world.entities.resize(1); world.generations.resize(1);
    world.identities.reset(new ScIdentities(2));
    ScEntity entity{}; entity.w=entity.h=8;
    std::snprintf(entity.persistent_id,sizeof entity.persistent_id,"gate");
    const char* error=nullptr;
    auto first=sc_spawn(&world,&entity,&error); CHECK(first && !error);
    CHECK(world.identities->find("gate")->entity==first);
    CHECK(!sc_spawn(&world,&entity,&error)); CHECK(error);
    std::snprintf(entity.persistent_id,sizeof entity.persistent_id,"capacity-failure");
    CHECK(!sc_spawn(&world,&entity,&error)); CHECK(!world.identities->find("capacity-failure"));
    CHECK(sc_destroy(&world,first,false)); CHECK(!sc_entity(&world,first));
    CHECK(world.identities->find("gate")->status==ScIdentityStatus::unloaded);
    std::snprintf(entity.persistent_id,sizeof entity.persistent_id,"gate");
    auto second=sc_spawn(&world,&entity); CHECK(second && second!=first);
    CHECK(sc_destroy(&world,second)); CHECK(world.identities->find("gate")->status==ScIdentityStatus::deleted);
    auto deleted_hash=sc_state_hash(&world);
    CHECK(world.identities->declare("later")); CHECK(sc_state_hash(&world)!=deleted_hash);
    CHECK(!sc_destroy(&world,first)); CHECK(world.identities->find("gate")->status==ScIdentityStatus::deleted);
    world.entities.resize(2); world.generations.resize(2,1);
    CHECK(sc_spawn(&world,&entity));
    CHECK(!sc_spawn(&world,&entity,&error)); CHECK(std::strstr(error,"duplicate"));
    CHECK(!world.entities[1].alive);
    std::puts("identities: fixed storage, lifecycle, capacity and stale handles passed");
}
