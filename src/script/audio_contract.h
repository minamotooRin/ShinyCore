#pragma once
#include "shiny/audio.h"
#include "shiny/state.h"

inline constexpr double SC_AUDIO_FADE_MAX=60;
struct ScAudioNumberField {
    const char* name;
    float ScAudioVoice::*member;
    double minimum,maximum;
    const char* description;
};
inline constexpr ScAudioNumberField SC_AUDIO_NUMBERS[]={
    {"volume",&ScAudioVoice::target_volume,0,1,"Target gain; a fade interpolates the current gain."},
    {"pitch",&ScAudioVoice::pitch,.25,4,"Playback speed multiplier; also advances the logical clock."},
    {"pan",&ScAudioVoice::pan,-1,1,"Stereo position: -1 left, 0 center, 1 right."},
    {"fade",&ScAudioVoice::fade,0,SC_AUDIO_FADE_MAX,"Seconds remaining to reach target volume; play starts a nonzero fade at silence."},
};
struct ScAudioBoolField { const char* name; bool ScAudioVoice::*member; const char* description; };
inline constexpr ScAudioBoolField SC_AUDIO_BOOLEANS[]={
    {"loop",&ScAudioVoice::loop,"Repeat when duration is reached."},
    {"paused",&ScAudioVoice::paused,"Pause playback, logical position and voice fade."},
    {"persistent",&ScAudioVoice::persistent,"Only music is retained across room changes; sound voices always expire."},
};
inline constexpr int SC_AUDIO_PRIORITY_MIN=-128,SC_AUDIO_PRIORITY_MAX=127;
inline constexpr const char* SC_AUDIO_BUSES[]={"master","music","sfx","ui"};
ScValue sc_audio_contracts();
