#pragma once
#include "shiny/audio.h"
#include "shiny/core.h"
#include "../platform/raylib_owner.h"
#include "raylib.h"
#include <expected>
#include <map>
#include <span>
#include <string>

// Host-owned raylib adapter. Prepared resources never play before commit/update.
class ScAudioDevice final {
    using SoundOwner=Owned<Sound,IsSoundValid,UnloadSound>;
    using AliasOwner=Owned<Sound,IsSoundValid,UnloadSoundAlias>;
    using MusicOwner=Owned<Music,IsMusicValid,UnloadMusicStream>;
    using Cache=std::map<std::string,SoundOwner,std::less<>>;
    struct Voice {
        std::uint32_t id{};
        AliasOwner sound;
        MusicOwner music;
        bool started{},paused{};
        std::string resource;
    };
public:
    struct Prepared {
        Prepared()=default;
        Prepared(Prepared&&)=default;
        Prepared& operator=(Prepared&&)=delete;
        // Reverse destruction order: aliases must die before decoded samples.
        Cache sounds;
        std::array<Voice,34> voices;
        std::array<bool,34> retain{};
    };
    ScAudioDevice()=default;
    ScAudioDevice(const ScAudioDevice&)=delete;
    ScAudioDevice& operator=(const ScAudioDevice&)=delete;
    ~ScAudioDevice() { close(); }
    bool open();
    void close() noexcept;
    void reset_voices() noexcept;
    std::expected<Prepared,std::string> prepare(const ScAudioState*,const char* root);
    // The prepared draft borrows this device's cache; commit/discard before another update.
    void commit(Prepared&&) noexcept;
    std::expected<void,std::string> update(const ScAudioState&,std::span<const ScTone>,const char* root);
private:
    Sound sound(const std::string&,Prepared&);
    bool ready_{};
    Cache sounds_;
    std::array<Voice,34> voices_;
    std::array<SoundOwner,16> tones_;
    std::size_t tone_slot_{};
};

// CPU-only validation also used by check/headless runs of graphical builds.
std::expected<void,std::string> sc_audio_validate(std::span<const ScResource>,const char* root);
