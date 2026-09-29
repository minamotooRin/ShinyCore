# Audio ownership

The host owns one `ScAudioState` for the application. It contains the bounded
voice slots, handle generations, buses, gains and logical clock. The simulation
world no longer owns or advances audio. `src/audio/audio.cpp` advances the mixer
at the fixed update boundary, including when gameplay simulation is paused.
Profiling attributes this work to audio rather than physics/simulation.

Each candidate room gets an isolated draft with persistent music, generations,
buses and preferences copied from the application. Temporary voices are omitted.
Its Lua init/draw, physics and native asset preparation must succeed before the
host copies the draft into the application and binds the new script to it.
An unsuccessful draft cannot stop active music or change active buses. Ordinary
room sounds disappear only on successful commit; persistent music keeps its ID,
position and fade state. Scripts used without the host own a local draft.

Lua exposes `sc.audio.play/music/set/stop/bus`. Music has two
slots, sound voices are configured through `project.limits.sound_voices` (up to 32),
and handles remain generation checked. Voice pause freezes its clock and fade;
master/category pause also stops affected voices. Bus fades advance independently.
Application volume preferences multiply authored bus and voice volumes.

## Acquiring persistent music

```lua
-- Call from each room's init; no handle belongs in sc.state or a save slot.
local music = assert(sc.audio.music('theme', {loop=true, volume=.08, fade=.4}))
```

`music(resource, options?)` requires a declared music resource. It reuses the first
live persistent, non-stopping music voice whose stored resource path matches,
including aliases with the same path. Reuse preserves its handle and playback
position and atomically patches supplied options. Omitted options retain their
current values; passing a fade sets the remaining fade without starting at silence.
The path must remain the same across rooms. Multiple explicitly played persistent
voices of that path are not merged; acquisition returns the first matching slot.

If none matches, a new persistent voice uses ordinary playback defaults and the
same two-stream priority/age capacity rules. A fading-out voice is not reused.
`ScMusicOptions` contains playback fields except `persistent`, which is implicit
and rejected if supplied. The operation is allowed during load/init/update;
candidate-room changes remain draft-only until successful commit. `set` and `stop`
can then control the returned handle in the current VM. Regular `play` remains the
explicit voice-allocation operation. Both entry points require one or two arguments.

Crossing uses this entry point and retains the same music voice across all three
rooms and normal checkpoint loads. A fresh application starts a fresh track;
save records contain campaign progress, not native voice handles. Logical/native
ownership evidence and its audible limits are in
[persistent music verification](../verification/systems/persistent-music.md).
Wayfarer's title and forest now acquire its theme the same way. Its integration
trace checks the voice ID and clock through ending, title and a new journey;
the original 20-second loop also advances across its end in a headless run.
Hidden native title/world captures were inspected. This verifies logical
continuity, not audible device behavior or final music quality.

Crossing and Barrage now use distinct, reproducible 16- and 15-second original
themes in place of their 0.3-second placeholder loops. All three sample themes
rebuild byte-for-byte in the current offline encoder environment and cross their
loop boundaries with one voice in the real headless host. Crossing/Barrage
packages include the new tracks; hidden native scene captures were inspected.
See [sample music verification](../verification/games/sample-music.md). Real speaker
playback and subjective mixing remain unaccepted.

The host backend owns `ScAudioDevice` in `src/audio/device.cpp`. It owns the device,
decoded sound cache, playback aliases, music streams and transient synthesized tones.
Candidate voices are prepared without playing; all GPU and audio preparations must
succeed before either cache is committed. Failed or discarded drafts release their
aliases before their decoded samples and leave active voices untouched. Matching
persistent IDs reuse the existing music stream. First playback applies volume,
pan and pitch first; initially paused voices do not briefly play before pausing.

Decoded sounds are shared by playback aliases within one application. The cache
counts active and staged decoded resources against 64 entries / 64 MiB (stereo
float estimate); eviction only removes entries with no active or staged aliases.
A failed draft may evict unused cache entries, but cannot remove active resources.
Drafts borrow the owning device's cache and must be committed/discarded before the
next prepare/update/close. The host keeps this lifetime local to room preparation.
Aliases and streams are destroyed before the device closes.

CPU-only declared-audio validation still decodes files at each room preflight;
avoiding that repeated validation and real device playback acceptance remain
outstanding. Logical position is diagnostic simulation time, not a guarantee
that the hardware sample clock is identical.

`--api` includes `ScAudioOptions`, `ScMusicOptions`, `ScAudioBusOptions`, `ScAudioBusState`
and structured contracts for all five audio functions. Numeric ranges and voice
fields share descriptors with validation; initial defaults come from the logical
voice. Bus routing defaults depend on the resource type. `bus(name)` reads during
all ordinary callbacks; passing non-nil options limits it to load/init/update.
`stop` exposes its zero-second default and 0..60 range. Voice patches retain
omitted values rather than reapplying play defaults.

Near-integer priorities are checked before float conversion; numeric-string handles
and NUL-suffixed resource/bus names are rejected. Failed patches remain atomic.
The Lua annotations and reference are generated from these contracts. Lua SDK
`1.0.0-dev.3` includes the updated local annotations; examples with annotations are
repinned, while the unchanged minimal snapshot SDK retains its prior version.

Focused verification (2026-09-26): the mixer test covers draft discard/commit,
pause, pitch and fade. Two existing host tests cover stale handles, capacity/priority,
bus state and room persistence. `tests/audio_transaction.py` runs eight hidden,
muted native frames: a candidate stops music and alters a bus, then fails; the old
room proves both remain valid, and a successful second room proves temporary handles
are stale while the music ID and all eight ticks of playback position are retained.
It does not enable or validate an audio device.
The rebuilt full-feature headless LLVM-MinGW ASan/UBSan configuration also passed
the mixer test and both host audio tests. This covers logical state and script
lifetimes, not audible continuity.

The extracted device adapter also passes `test_audio_device` in Release and a
standalone LLVM-MinGW ASan/UBSan build. This links the real pinned raylib declarations
to a silent fake device, injecting alias/music creation failures and checking draft
discard, cache reuse/eviction, pause/resume, pre-play gain, persistent-stream reuse
and alias-before-sample-before-device destruction. No real driver or speaker is
opened; these checks do not establish audible continuity or raylib device behavior.
The hidden/muted eight-frame host transaction check passes with the rebuilt adapter.

```powershell
.\build\full\test_audio.exe
.\build\full\test_audio_device.exe
python tests/audio_transaction.py build/full/shiny.exe
```

Focused contract follow-up: `tests/audio_contracts.py` passes on Windows Release
and the rebuilt full-feature headless ASan/UBSan executable. It checks metadata,
range boundaries, wrong scalar types, finite/integer requirements, NUL names,
metatable rejection, atomic failure and load/init/update/draw/ui_update rules.
The two existing audio behavior tests also pass with sanitizers. Generator checks
confirm current annotations/reference and preservation of adjacent namespaces,
functions and aliases when regenerating a type block. No device was opened.
