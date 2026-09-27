#include "shiny/attachment.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>

#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"attachment:%d: %s\n",__LINE__,#c); std::exit(1); } } while(false)
bool near(float a,float b) { return std::abs(a-b)<.002f; }
int main() {
    auto owner=std::make_unique<ScWorld>(); auto& w=*owner;
    w.map.bounded=false; w.gravity=0; w.particles.configure(0);
    ScEntity spec; spec.w=spec.h=10; spec.x=100; spec.y=100;
    const auto root=sc_spawn(&w,&spec),child=sc_spawn(&w,&spec),tip=sc_spawn(&w,&spec);
    CHECK(sc_attach(w,child,root,{20,0,.25f}));
    CHECK(sc_attach(w,tip,child,{10,0,0}));
    CHECK(near(sc_entity(&w,child)->x,120));
    const auto hash=sc_state_hash(&w);
    CHECK(!sc_attach(w,root,tip,{})); CHECK(sc_state_hash(&w)==hash);
    CHECK(!sc_attach(w,child,root,{NAN,0,0})); CHECK(sc_state_hash(&w)==hash);
    auto copy=*sc_entity(&w,child); copy.vx=1;
    CHECK(!sc_attachment_patch_valid(*sc_entity(&w,child),copy));
    copy=*sc_entity(&w,child); copy.color=0; CHECK(sc_attachment_patch_valid(*sc_entity(&w,child),copy));

    CHECK(sc_attach(w,child,root,{20,0,0})); CHECK(sc_detach(w,tip));
    sc_presentation_configure(w,true); sc_presentation_capture(w);
    sc_entity(&w,root)->angle=std::numbers::pi_v<float>/2; sc_step(&w);
    const auto& e=*sc_entity(&w,child); CHECK(near(e.x,100)&&near(e.y,120));
    const auto half=sc_display_pose(w,e,.5f);
    CHECK(near(half.x,100+std::sqrt(200.f))&&near(half.y,100+std::sqrt(200.f)));
    CHECK(near(half.angle,std::numbers::pi_v<float>/4));
    // Local animation interpolates in parent space, and display reads are pure.
    sc_presentation_capture(w); CHECK(sc_attach(w,child,root,{40,0,0})); sc_step(&w);
    const auto fixed_hash=sc_state_hash(&w); const auto local_half=sc_display_pose(w,e,.5f);
    CHECK(near(local_half.y,130)&&sc_state_hash(&w)==fixed_hash);
    sc_presentation_capture(w); sc_entity(&w,root)->x=200;
    sc_presentation_snap(w,root); sc_step(&w);
    CHECK(near(sc_display_pose(w,e,0).x,200));
    CHECK(sc_detach(w,child)); CHECK(!e.parent&&near(e.x,200)&&near(e.y,140));
    CHECK(sc_detach(w,child));

    // Real solver movement/rotation must complete before attachments and queries.
    auto& body=*sc_entity(&w,root); body.body_type=3; body.dynamic=true;
    body.vx=120; body.angular_velocity=1; body.fixed_rotation=false;
    CHECK(sc_attach(w,child,root,{20,0,.25f}));
    CHECK(!sc_attach(w,root,tip,{}));
    const auto before=body.x; sc_step(&w); CHECK(body.x>before);
    const auto expected=sc_attachment_transform({body.x,body.y,body.angle},body.w,body.h,e,e.local_pose);
    CHECK(near(e.x,expected.x)&&near(e.y,expected.y)&&near(e.angle,expected.angle));
    CHECK(sc_attach(w,tip,child,{10,0,0}));
    const auto child_pose=ScPose{e.x,e.y,e.angle};
    CHECK(sc_destroy(&w,root)); CHECK(!e.parent&&near(e.x,child_pose.x)&&near(e.y,child_pose.y));
    CHECK(sc_entity(&w,tip)->parent==child);
    const auto replacement=sc_spawn(&w,&spec); CHECK(replacement!=root);
    CHECK(!sc_attach(w,child,root,{})); CHECK(!sc_detach(w,root));

    // A deep subtree cannot be reparented under another node past the depth cap.
    sc_world_init(&w,1); w.map.bounded=false; w.particles.configure(0);
    ScEntityId chain[34]{};
    for(auto& id:chain) id=sc_spawn(&w,&spec);
    for(std::size_t i=1;i<=32;++i) CHECK(sc_attach(w,chain[i],chain[i-1],{}));
    const auto deep_hash=sc_state_hash(&w);
    CHECK(!sc_attach(w,chain[0],chain[33],{})); CHECK(sc_state_hash(&w)==deep_hash);
    CHECK(!sc_attach(w,chain[33],chain[32],{}));
    CHECK(sc_destroy(&w,chain[0])); CHECK(!sc_entity(&w,chain[1])->parent);
    CHECK(sc_attach(w,chain[1],chain[33],{}));
    // Batch forward references resolve before publication; failure leaves drafts,
    // generations and persistent ID records untouched.
    sc_world_init(&w,1); w.particles.configure(0);
    std::array<ScEntity,3> drafts{spec,spec,spec};
    drafts[0].x=5; drafts[0].y=0; drafts[1].x=100; drafts[1].y=100; drafts[2].x=20; drafts[2].y=0;
    std::snprintf(drafts[1].persistent_id,sizeof drafts[1].persistent_id,"batch/root");
    const std::array<std::size_t,3> parents{3,0,2},cycle{3,1,2},outside{4,0,2};
    const auto empty_hash=sc_state_hash(&w);
    CHECK(!sc_spawn_many(w,drafts,cycle)); CHECK(!sc_spawn_many(w,drafts,outside));
    CHECK(sc_state_hash(&w)==empty_hash&&drafts[0].id==0&&drafts[1].id==0);
    CHECK(!w.identities->find("batch/root"));
    drafts[0].vx=1; CHECK(!sc_spawn_many(w,drafts,parents)); drafts[0].vx=0;
    drafts[1].x=999999; CHECK(!sc_spawn_many(w,drafts,parents)); drafts[1].x=100;
    CHECK(sc_state_hash(&w)==empty_hash&&drafts[0].x==5);
    CHECK(sc_spawn_many(w,drafts,parents));
    CHECK(near(sc_entity(&w,drafts[0].id)->x,125)&&near(sc_entity(&w,drafts[2].id)->x,120));
    CHECK(sc_entity(&w,drafts[0].id)->parent==drafts[2].id&&sc_entity(&w,drafts[2].id)->parent==drafts[1].id);
    CHECK(drafts[0].x==5); // Only IDs are written to caller drafts.
    std::puts("attachments: physics order, nested transforms, curved interpolation, cuts, lifetime and depth passed");
}
