#include "shiny/audio.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(value) do { if(!(value)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#value); std::exit(1); } } while(false)

int main() {
    ScAudioState app;
    app.audio_generations[32]=7; app.audio_clock=9;
    auto& music=app.audio[32]; music.alive=music.music=music.persistent=music.loop=true;
    music.id=(7u<<6)|32; music.duration=10; music.position=2;
    app.audio[0].alive=true; app.audio[0].id=64;
    app.audio_buses[1].volume=.6f; app.audio_gains[1]=.7f;
    {
        auto failed=app.room_candidate();
        CHECK(failed.audio[32].id==music.id && !failed.audio[0].alive);
        CHECK(failed.audio_clock==9 && failed.audio_generations[32]==7);
        failed.audio[32].alive=false; failed.audio_buses[1].volume=0;
    }
    CHECK(app.audio[32].alive && app.audio[0].alive && app.audio_buses[1].volume==.6f);
    auto next=app.room_candidate(); next.audio[32].paused=true; app=next;
    CHECK(!app.audio[0].alive && app.audio_gains[1]==.7f);
    app.step(.25f); CHECK(app.audio[32].position==2);
    app.audio[32].paused=false; app.audio[32].pitch=2; app.step(.25f);
    CHECK(app.audio[32].position==2.5f);
    app.audio_buses[0].paused=true; app.step(.25f); CHECK(app.audio[32].position==2.5f);
    app.audio_buses[0].paused=false; app.audio[32].fade=.25f;
    app.audio[32].target_volume=0; app.audio[32].stopping=true; app.step(.25f);
    CHECK(!app.audio[32].alive && app.audio[32].volume==0);
    std::puts("Application audio draft isolation, commit, pause, pitch and fade passed");
}
