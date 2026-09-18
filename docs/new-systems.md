# Complete-edition development interfaces

These interfaces are implemented in the current development tree. This document
does not certify the complete specification. See `implementation-status.md` for
remaining work and actual verification. `shiny --api` reports this executable's
capabilities; `project.modules` rejects unknown or unavailable requirements.

## Project and ownership

```lua
return {
    id="my.game", data_version=1, rooms={"main.lua","rooms/end.lua"},
    modules={"physics","navigation"},
    limits={entities=4096,particles=32768,draws=4096,contacts=16384,sound_voices=32},
}
```

Entities use integer generation/room handles below 2^52. They remain exact in Lua
integers and JSON numbers. `sc.get_many(ids)` copies a batch; `sc.set_many({
{id=id,patch={x=42}}, ...})` validates the entire batch before committing.
`sc.find_all(tag)` returns stable pool order. Handles are not save references.

`sc.app.pause(true)` stops physics, bodyless movement, projectiles and particles;
Lua UI, input, audio and network polling remain available. `sc.app.quit()` requests
host shutdown. One candidate room can load alongside the active room. Candidate
shared data is copied; candidate save and network writes are prohibited.

## Input and UI

`sc.input` exposes keyboard, mouse, four gamepad snapshots, text, composition and
recorded clipboard paste data. Gamepad APIs take an optional stable slot 1..4;
`gamepad_axis(name,deadzone,slot)` defaults to deadzone 0.2. Omitting a slot uses
the selected controller. Quick taps retain both edges even when not held at the
tick boundary. Disconnect clears held state and generates releases.

`--record file.jsonl` writes version-3 fixed-tick snapshots. `--replay` isolates
device input, including clipboard paste; the two options cannot be combined.
Mouse positions are logical viewport coordinates and carry `inside` for black bars.
`sc.input.boundaries(text)` returns grapheme byte boundaries, including the end.

`require("shiny.ui")` supplies retained author-ID trees, rows/columns/grids,
focus, clipping, modal input boundaries, scrolling, virtual lists and text fields.
Call `UI.layout` before the first draw, `UI.update` during fixed updates and
`UI.draw` during drawing. `UI.set` marks layout dirty. Text layout is cached and
long strings split into bounded native commands instead of disappearing.
Text fields support grapheme editing, selection, undo/redo, multiline input and
clipboard shortcuts. `examples/text_input` is a native rendering fixture.

Fonts load metrics lazily (8192 cached glyphs per resource); the native renderer
loads 64-codepoint atlas pages as needed (128 pages per resource). A font must
contain the desired characters. The fixture's original Workshop subset font is
small and is not a general CJK font. Complex shaping, bidi and color Emoji are
unsupported. Windows IME message integration exists; actual device/IME testing
is still required. Linux/macOS composition is not implemented.

## Batch simulation

Configure bullets once in initialization with `sc.projectiles.configure(32768)`.
`spawn(specs)` validates a batch of x/y, vx/vy, ax/ay, radius, life, mask, numeric
RGBA color, terrain and piercing fields. `hits()` returns the previous completed
step's hits ordered by projectile, distance fraction and target. `count()` and
`clear()` inspect/reset the batch. Targets use authored AABBs; current terrain
blocking uses ASCII cells. Atlas projectile rendering and general collision
geometry remain outstanding.

`sc.navigation.path(sx,sy,gx,gy,budget)` returns status, visited count and points.
`flow(gx,gy,budget,slot)` builds one shared field in slot 1..16 and returns handle,
status, visited. `direction(flow,x,y)` samples it in world coordinates.
`steer(flow,ids,speed)` validates all IDs before assigning batch velocities.
Rebuild a field after terrain changes. Incremental dirty-region maintenance is
not yet implemented. Lua retains decision-making and behavior state machines.

## Content building and streaming

```sh
python tools/assets.py game/assets.build.json game/build-content
```

The JSON manifest has `atlases`, `maps`, and `animations` dictionaries. Atlas
entries use `{ "width": 2048, "images": { "hero": "assets/hero.png" } }`;
map/animation entries map a stable name to a Tiled/Aseprite JSON path. Atlas
building requires Pillow on the development machine, never in the shipped game.
Build outputs are immutable content-addressed directories with readable indexes.
All file dependencies, parameters and tool version participate in the key.

The importer converts finite/infinite orthogonal maps, negative chunks, integer
or base64 raw/zlib/gzip data, groups, image/object layers, external tilesets and
object templates into 32x32 blocks. It preserves layer metadata and decomposes
concave object polygons. It is not yet wired into the full map drawing, collision
and persistent-object lifecycle; the direct runtime Tiled reader still has its
older restricted format contract.

Build with `SHINY_STREAMING=ON` to enable `sc.stream.open(index_path)` in init,
`request(x,y)`, `get(x,y)`, `release(x,y)` and `stats()`. One background thread
reads and decodes CPU data. `get` waits without advancing simulation; missing
sparse chunks are empty. The charged cache budget is 128 MiB; pinned exhaustion
is an error. Disk waits currently block the host and do **not** meet the requested
33.3 ms streaming-frame guarantee. GPU/physics commit, loading barriers, state
export/reload and delayed/out-of-order fault acceptance remain unfinished.

## Saves, audio and networking

`project.display` supplies defaults for windowed width/height, `mode` (windowed or
borderless), `scale` (integer or smooth), VSync and four user volume gains. Read
copies with `sc.settings.get()` and apply patches during update with
`sc.settings.apply(patch, persist)`; persistence defaults true. Settings use
`<save-root>/<project-id>/config/settings.json`, separate from checkpoint slots.
The service validates the entire patch first and restores the previous window
configuration if applying or saving fails. Corrupt files retain project defaults
and report a recoverable `sc.settings.error()`. The file format is exactly 1.

`shiny.settings` is a reusable menu with stable control IDs; all three sample
shells include it. Changes remain local until Apply. `shiny.input.new(bindings,
profile)` loads a saved binding profile, and `shiny.input.save(input)` persists
rebinding. Preference gains multiply game audio buses without altering their
fades. Headless runs use the same validation and persistence. Integer scaling
uses centered black bars; smaller-than-logical windows use fractional fit.
Native `--capture` exports the actual window framebuffer, including scaling and
black bars, rather than resizing the internal render target.

Save format 2 uses exact project data versions. `sc.save.write(slot)` creates an
atomic record and preserves a previous valid `.bak`; damaged bytes never replace
a good backup. `load` reconstructs the room; `read` returns data, `list` returns
sorted slot metadata and `delete` removes main and backup. Writes/deletes require
update and are disabled during checks. Large chunk-state stores are not yet built.

`sc.audio.bus(name,options)` reads/updates master/music/sfx/ui volume, fade and
pause. Voice options include pan -1..1 and integer priority -128..127. At capacity,
a new voice replaces the oldest voice at the lowest eligible priority; higher
priority voices are protected. Bus state and persistent music survive room swaps.
The backend shares decoded sounds through aliases with a 64-entry/64-MiB charged
cache. Its hardware playback and latency still require acceptance.

With `SHINY_NETWORK=ON`, `session:persist(name)` transfers socket ownership to the
application. A new room calls `sc.net.bind(name)`; collecting bindings does not
close the socket. Explicit close invalidates all old bindings, even if the name
is later reused. Candidate initialization can bind/read but cannot poll, flush,
send, disconnect, close or create sockets. The four-player protocol, reconnect
tokens and weak-network acceptance are not yet implemented.

## Agent tooling

`--trace` writes explicit snapshots only when requested. `sc.debug.watch` exposes
up to 64 bounded named values. `tools/compare_traces.py` reports the first differing
frame/path with a float tolerance; it skips only top-level diagnostic hashes and
engine version. `tools/scenario.py` checks authored snapshot assertions.
`--profile` currently reports wall time including pacing; it is **not** the final
CPU/GPU acceptance profiler. Interactive debugging is not implemented.

`tools/new_game.py` copies `lib/shiny` and local LuaLS docs into each project.
`tools/api_docs.py` generates supplemental declarations from native metadata;
rich table annotations still contain hand-maintained definitions checked by
contract tests. The complete field/default/phase metadata schema remains work.

## Build and package inspection

`CMakePresets.json` provides Ninja Release configure/build/test presets named
`lightweight`, `full`, and `headless`. The full preset requests all modules;
advanced rendering and interactive debugging are still unavailable and produce
a configure warning. Always inspect `--api.modules` for actual capabilities.

`tools/package.py` reads the executable's real API contract before copying and
checks custom games using `--check-all`, including their required modules. Its
`package-report.json` records engine version, capabilities, separate engine,
resource, Lua standard-library, game-code and supplied debug-symbol byte totals,
plus relative paths and SHA-256 hashes. `--symbols FILE` adds separate symbols;
embedded symbols remain part of executable size. The report and ZIP overhead
are excluded from the listed totals. Projects are copied conservatively; pruning
unused resources/modules and a complete runtime dependency audit remain open.
