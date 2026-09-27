#include "device.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

bool ScAudioDevice::open() {
    close();
    InitAudioDevice(); ready_=IsAudioDeviceReady();
    return ready_;
}
void ScAudioDevice::reset_voices() noexcept {
    for(auto& voice:voices_) voice=Voice{};
    for(auto& tone:tones_) tone.reset();
    tone_slot_=0;
}
void ScAudioDevice::close() noexcept {
    reset_voices(); sounds_.clear();
    if(ready_) CloseAudioDevice();
    ready_=false;
}
Sound ScAudioDevice::sound(const std::string& path,Prepared& draft) {
    if(auto found=sounds_.find(path);found!=sounds_.end()) return found->second.get();
    if(auto found=draft.sounds.find(path);found!=draft.sounds.end()) return found->second.get();
    SoundOwner decoded{LoadSound(path.c_str())};
    if(!decoded) throw std::runtime_error("cannot decode sound: "+path);
    constexpr std::uint64_t budget=64u*1024u*1024u;
    auto charge=[](Sound value) { return static_cast<std::uint64_t>(value.frameCount)*8; };
    auto resident=charge(decoded.get());
    for(const auto& [name,value]:sounds_) { (void)name; resident+=charge(value.get()); }
    for(const auto& [name,value]:draft.sounds) { (void)name; resident+=charge(value.get()); }
    while(sounds_.size()+draft.sounds.size()>=64 || resident>budget) {
        auto victim=sounds_.end();
        for(auto entry=sounds_.begin();entry!=sounds_.end();++entry) {
            auto refers=[&](const Voice& voice) { return voice.sound && voice.resource==entry->first; };
            bool referenced=false;
            for(const auto& voice:voices_) referenced=referenced||refers(voice);
            for(const auto& voice:draft.voices) referenced=referenced||refers(voice);
            if(!referenced) { victim=entry; break; }
        }
        if(victim==sounds_.end()) throw std::runtime_error("decoded sound cache budget exhausted (64 entries / 64 MiB)");
        resident-=charge(victim->second.get()); sounds_.erase(victim);
    }
    return draft.sounds.emplace(path,std::move(decoded)).first->second.get();
}
std::expected<ScAudioDevice::Prepared,std::string> ScAudioDevice::prepare(const ScAudioState* mixer,const char* root) {
    try {
        Prepared draft;
        if(!ready_ || !mixer) return draft;
        for(std::size_t i=0;i<mixer->audio.size();++i) {
            const auto& voice=mixer->audio[i];
            if(!voice.alive) continue;
            if(voice.id==voices_[i].id) { draft.retain[i]=true; continue; }
            auto& native=draft.voices[i];
            native.id=voice.id; native.resource=std::string(root)+"/"+voice.path;
            if(voice.music) native.music.reset(LoadMusicStream(native.resource.c_str()));
            else native.sound.reset(LoadSoundAlias(sound(native.resource,draft)));
            if(!native.music && !native.sound) throw std::runtime_error("cannot prepare audio voice: "+native.resource);
        }
        return draft;
    } catch(const std::exception& error) { return std::unexpected(error.what()); }
}
void ScAudioDevice::commit(Prepared&& draft) noexcept {
    // Node transfer preserves sample addresses; std::less performs no allocation.
    sounds_.merge(draft.sounds);
    for(std::size_t i=0;i<voices_.size();++i)
        if(!draft.retain[i]) voices_[i]=std::move(draft.voices[i]);
}
std::expected<void,std::string> ScAudioDevice::update(const ScAudioState& mixer,std::span<const ScTone> tones,const char* root) {
    if(!ready_) return {};
    auto prepared=prepare(&mixer,root);
    if(!prepared) return std::unexpected(prepared.error());
    commit(std::move(*prepared));
    for(std::size_t i=0;i<mixer.audio.size();++i) {
        const auto& voice=mixer.audio[i]; auto& native=voices_[i];
        if(!voice.alive) continue;
        const auto& bus=mixer.audio_buses[static_cast<std::size_t>(voice.bus)];
        const auto& master=mixer.audio_buses[0];
        const float volume=voice.volume*master.volume*mixer.audio_gains[0]*
            (voice.bus==0?1:bus.volume*mixer.audio_gains[static_cast<std::size_t>(voice.bus)]);
        const bool paused=voice.paused||master.paused||bus.paused;
        if(native.music) {
            auto& music=*native.music.ptr(); music.looping=voice.loop;
            SetMusicVolume(music,volume); SetMusicPan(music,(voice.pan+1)*.5f); SetMusicPitch(music,voice.pitch);
            if(!paused && !native.started) {
                PlayMusicStream(music);
                if(voice.position>0) SeekMusicStream(music,voice.position);
            } else if(native.started && paused!=native.paused) {
                if(paused) PauseMusicStream(music); else ResumeMusicStream(music);
            }
            if(!paused) UpdateMusicStream(music);
        }
        if(native.sound) {
            auto value=native.sound.get();
            SetSoundVolume(value,volume); SetSoundPan(value,(voice.pan+1)*.5f); SetSoundPitch(value,voice.pitch);
            if(!paused && !native.started) PlaySound(value);
            else if(native.started && paused!=native.paused) {
                if(paused) PauseSound(value); else ResumeSound(value);
            }
            if(voice.loop && !paused && !IsSoundPlaying(value)) PlaySound(value);
        }
        native.started=native.started||!paused; native.paused=paused;
    }
    try {
        for(const auto& tone:tones) {
            auto count=static_cast<unsigned int>(tone.duration*44100);
            if(!count) continue;
            std::vector<float> samples(count);
            for(unsigned int j=0;j<count;++j) {
                float t=static_cast<float>(j)/44100,progress=static_cast<float>(j)/static_cast<float>(count);
                float attack=std::min(1.0f,t/.008f),envelope=(1-progress)*(1-progress)*attack;
                samples[j]=std::sin(2*std::numbers::pi_v<float>*tone.frequency*t)*envelope*tone.volume;
            }
            Wave wave{.frameCount=count,.sampleRate=44100,.sampleSize=32,.channels=1,.data=samples.data()};
            auto& slot=tones_[tone_slot_]; tone_slot_=(tone_slot_+1)%tones_.size();
            slot.reset(LoadSoundFromWave(wave));
            if(slot) PlaySound(slot.get());
        }
        return {};
    } catch(const std::exception& error) { return std::unexpected(error.what()); }
}
std::expected<void,std::string> sc_audio_validate(std::span<const ScResource> resources,const char* root) {
    try {
        for(const auto& resource:resources) if(resource.type=="sound"||resource.type=="music") {
            Owned<Wave,IsWaveValid,UnloadWave> wave{LoadWave((std::string(root)+"/"+resource.path).c_str())};
            if(!wave) return std::unexpected("cannot decode audio: "+resource.path);
        }
        return {};
    } catch(const std::exception& error) { return std::unexpected(error.what()); }
}
