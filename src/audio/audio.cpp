#include "shiny/audio.h"
#include <algorithm>
#include <cmath>

ScAudioState ScAudioState::room_candidate() const {
    auto draft=*this;
    for(auto& voice:draft.audio) if(!voice.music||!voice.persistent) voice={};
    return draft;
}
void ScAudioState::step(float dt) {
    for(auto& bus:audio_buses) if(bus.fade>0) {
        bus.volume+=(bus.target-bus.volume)*std::min(1.0f,dt/bus.fade);
        bus.fade=std::max(0.0f,bus.fade-dt);
    }
    for(auto& voice:audio) if(voice.alive&&!voice.paused&&!audio_buses[0].paused&&!audio_buses[static_cast<size_t>(voice.bus)].paused) {
        if(voice.fade>0) {
            float part=std::min(1.0f,dt/voice.fade);
            voice.volume+=(voice.target_volume-voice.volume)*part;
            voice.fade=std::max(0.0f,voice.fade-dt);
            if(voice.stopping&&voice.fade==0) voice.alive=false;
        }
        voice.position+=dt*voice.pitch;
        if(voice.duration>0&&voice.position>=voice.duration) {
            if(voice.loop) voice.position=std::fmod(voice.position,voice.duration); else voice.alive=false;
        }
    }
}
