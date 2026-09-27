#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

struct ScAudioVoice {
    std::uint32_t id{};
    bool alive{},music{},loop{},paused{},persistent{},stopping{};
    float volume{1},target_volume{1},pitch{1},fade{},position{},duration{};
    float pan{};
    int bus{2},priority{};
    std::uint64_t age{};
    char path[128]{};
};
struct ScAudioBus { float volume{1},target{1},fade{}; bool paused{}; };

// Application-owned logical mixer; candidates use an isolated room draft.
struct ScAudioState {
    std::array<ScAudioVoice,34> audio{};
    std::array<std::uint32_t,34> audio_generations{};
    std::array<ScAudioBus,4> audio_buses{}; // master, music, sfx, ui
    std::array<float,4> audio_gains{1,1,1,1}; // Application preferences, independent of game bus fades.
    std::uint64_t audio_clock{};
    std::size_t sound_voice_limit{32};
    ScAudioState room_candidate() const;
    void step(float dt);
};
