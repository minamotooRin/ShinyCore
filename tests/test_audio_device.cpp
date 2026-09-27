#include "../src/audio/device.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(value) do { if(!(value)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#value); std::exit(1); } } while(false)

// Real raylib declarations, fake resources: no window, audio driver or speaker.
struct rAudioBuffer {
    rAudioBuffer* source{};
    int aliases{};
    bool playing{},configured{};
    float volume{},pan{},pitch{},position{};
};
namespace {
bool device{},fail_alias{};
int live{},sound_loads{},music_loads{},plays{},music_plays{},resumes{},updates{};
rAudioBuffer* last_music{};
rAudioBuffer* buffer() { CHECK(device); ++live; return new rAudioBuffer; }
void release(rAudioBuffer* value) { CHECK(device && value && value->aliases==0); --live; delete value; }
void play(rAudioBuffer* value) { CHECK(device && value && value->configured); value->playing=true; ++plays; }
ScAudioVoice voice(unsigned id,const char* path,bool music=false) {
    ScAudioVoice value; value.alive=true; value.id=id; value.music=music; value.persistent=music;
    std::snprintf(value.path,sizeof value.path,"%s",path); return value;
}
}
void InitAudioDevice() { CHECK(!device); device=true; }
bool IsAudioDeviceReady() { return device; }
void CloseAudioDevice() { CHECK(device && live==0); device=false; }
Sound LoadSound(const char* path) {
    ++sound_loads;
    if(std::strstr(path,"bad")) return {};
    Sound result{}; result.frameCount=128; result.stream.buffer=buffer(); return result;
}
bool IsSoundValid(Sound value) { return value.stream.buffer!=nullptr; }
void UnloadSound(Sound value) { CHECK(!value.stream.buffer->source); release(value.stream.buffer); }
Sound LoadSoundAlias(Sound value) {
    if(fail_alias) return {};
    CHECK(IsSoundValid(value));
    Sound alias=value; alias.stream.buffer=buffer(); alias.stream.buffer->source=value.stream.buffer;
    ++value.stream.buffer->aliases; return alias;
}
void UnloadSoundAlias(Sound value) {
    CHECK(value.stream.buffer->source && value.stream.buffer->source->aliases>0);
    --value.stream.buffer->source->aliases; release(value.stream.buffer);
}
void PlaySound(Sound value) { play(value.stream.buffer); }
bool IsSoundPlaying(Sound value) { return value.stream.buffer->playing; }
void PauseSound(Sound value) { value.stream.buffer->playing=false; }
void ResumeSound(Sound value) { value.stream.buffer->playing=true; ++resumes; }
void SetSoundVolume(Sound value,float volume) { value.stream.buffer->volume=volume; value.stream.buffer->configured=true; }
void SetSoundPan(Sound value,float pan) { value.stream.buffer->pan=pan; }
void SetSoundPitch(Sound value,float pitch) { value.stream.buffer->pitch=pitch; }
Sound LoadSoundFromWave(Wave) {
    Sound value{}; value.stream.buffer=buffer(); value.stream.buffer->configured=true; return value;
}
Music LoadMusicStream(const char* path) {
    ++music_loads;
    if(std::strstr(path,"bad")) return {};
    Music value{}; value.stream.buffer=buffer(); last_music=value.stream.buffer; return value;
}
bool IsMusicValid(Music value) { return value.stream.buffer!=nullptr; }
void UnloadMusicStream(Music value) { release(value.stream.buffer); }
void PlayMusicStream(Music value) { play(value.stream.buffer); ++music_plays; }
void SeekMusicStream(Music value,float position) { value.stream.buffer->position=position; }
void PauseMusicStream(Music value) { value.stream.buffer->playing=false; }
void ResumeMusicStream(Music value) { value.stream.buffer->playing=true; ++resumes; }
void UpdateMusicStream(Music) { ++updates; }
void SetMusicVolume(Music value,float volume) { value.stream.buffer->volume=volume; value.stream.buffer->configured=true; }
void SetMusicPan(Music value,float pan) { value.stream.buffer->pan=pan; }
void SetMusicPitch(Music value,float pitch) { value.stream.buffer->pitch=pitch; }
Wave LoadWave(const char* path) {
    if(std::strstr(path,"bad")) return {};
    Wave value{}; value.data=new float[1]; return value;
}
bool IsWaveValid(Wave value) { return value.data!=nullptr; }
void UnloadWave(Wave value) { delete[] static_cast<float*>(value.data); }

int main() {
    ScAudioDevice adapter;
    ScAudioState mixer;
    mixer.audio[0]=voice(64,"hit.wav");
    mixer.audio[1]=voice(65,"hit.wav");
    mixer.audio[32]=voice(96,"theme.ogg",true);
    mixer.audio[32].paused=true; mixer.audio[32].position=2;
    mixer.audio[32].bus=1; mixer.audio[32].volume=.5f;
    mixer.audio_buses[0].volume=.5f; mixer.audio_gains[1]=.5f;
    CHECK(adapter.update(mixer,{},"project")); CHECK(live==0 && plays==0);
    CHECK(adapter.open());
    {
        auto prepared=adapter.prepare(&mixer,"project"); CHECK(prepared);
        CHECK(plays==0 && sound_loads==1 && music_loads==1);
        adapter.commit(std::move(*prepared)); CHECK(plays==0);
    }
    CHECK(adapter.update(mixer,{},"project"));
    CHECK(plays==2 && music_plays==0 && !last_music->playing);
    CHECK(last_music->volume==.125f);
    mixer.audio[32].paused=false;
    CHECK(adapter.update(mixer,{},"project"));
    CHECK(music_plays==1 && last_music->position==2 && updates==1);
    auto* persistent=last_music;
    const auto baseline=live;
    // Failure after a successful alias allocation must free only the draft.
    auto candidate=mixer; candidate.audio[0]=voice(128,"new.wav");
    candidate.audio[33]=voice(97,"bad.ogg",true);
    CHECK(!adapter.prepare(&candidate,"project"));
    CHECK(live==baseline && persistent->playing && plays==3);
    candidate.audio[33]={}; fail_alias=true;
    CHECK(!adapter.prepare(&candidate,"project"));
    fail_alias=false; CHECK(live==baseline && persistent->playing);
    {
        auto discarded=adapter.prepare(&candidate,"project"); CHECK(discarded);
    }
    CHECK(live==baseline && persistent->playing && plays==3);
    candidate.audio[0]={}; candidate.audio[1]={};
    {
        auto prepared=adapter.prepare(&candidate,"project"); CHECK(prepared);
        adapter.commit(std::move(*prepared));
    }
    CHECK(adapter.update(candidate,{},"project"));
    CHECK(music_plays==1 && last_music==persistent && persistent->playing);
    candidate.audio_buses[0].paused=true;
    CHECK(adapter.update(candidate,{},"project")); CHECK(!persistent->playing);
    candidate.audio_buses[0].paused=false;
    CHECK(adapter.update(candidate,{},"project")); CHECK(persistent->playing && resumes==1);
    // Unused cached sounds may be evicted, but active and prepared aliases must survive.
    candidate.audio[0]=voice(192,"hit.wav");
    auto loads=sound_loads;
    CHECK(adapter.update(candidate,{},"project")); CHECK(sound_loads==loads);
    for(unsigned i=0;i<70;++i) {
        auto path="sound-"+std::to_string(i)+".wav";
        candidate.audio[1]=voice(257+i*64,path.c_str());
        CHECK(adapter.update(candidate,{},"project"));
    }
    CHECK(persistent->playing && music_plays==1);
    adapter.close(); CHECK(!device && live==0);
    std::array<ScResource,1> resources; resources[0].type="sound"; resources[0].path="ok.wav";
    CHECK(sc_audio_validate(resources,"project"));
    resources[0].path="bad.wav"; CHECK(!sc_audio_validate(resources,"project"));
    std::puts("Silent device checks passed: staged failure/discard, cache aliases, persistence, pause and release order");
}
