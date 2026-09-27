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

`require("shiny.ui")` supplies retained stable-ID trees, rows/columns/grids,
focus, clipping, modal input boundaries, scrolling, virtual lists and text fields.
Call `UI.layout` before the first draw, `UI.update` during fixed updates and
`UI.draw` during drawing. `UI.set` marks layout dirty. Text layout is cached and
long strings split into bounded native commands instead of disappearing.
List rows can carry unique string IDs; replacing items through UI.set preserves the
selected ID across filtering/sorting, or clears it if removed. Inspection exposes
selected_item_id. See [list contracts](ui-lifecycle.md) for validation and events.
Text fields support grapheme editing, selection, undo/redo, multiline input and
clipboard shortcuts. `examples/text_input` is a native rendering fixture.

Fonts load metrics lazily (8192 cached glyphs per resource); the native renderer
loads 64-codepoint atlas pages as needed (128 pages per resource). A font must
contain the desired characters. The fixture's original Workshop subset font is
small and is not a general CJK font. Complex shaping, bidi and color Emoji are
unsupported. Windows IME message integration exists; actual device/IME testing
is still required. Linux/macOS composition is not implemented.

`shiny.shell` supplies optional title/pause/settings UI. `Shell.finish(shell,
message, {title=..., color=..., details=...})` pauses gameplay, focuses `play` and
lays out the outcome screen during init/update. `details` is a short two-line
summary; supplying it hides the checkpoint row to keep the panel within 384×216.
Set the `play` text/callback before finishing. Omit the third argument for the
simple completion screen. This helper neither saves nor restarts the game; those
rules belong to the sample. It is included in Lua SDK `1.0.0-dev.2`.

## Batch simulation

Particles use [dedicated columns and stable compaction](particles.md), batch native
drawing and atomic capacity failure. Bounded immutable emitter templates provide speed,
lifetime and angle ranges plus size/RGBA curves; `shiny.particles` schedules fixed-update
emission. Templates also support declared PNG regions, alpha and additive blending;
contiguous batches preserve creation order across textures and blend modes.

Configure bullets once in initialization with `sc.projectiles.configure()`.
`project.limits.projectiles` defaults to 32768 (range 0..65536); zero disables the
system. Configuration allocates the pool lazily and may request a smaller capacity,
but cannot exceed the project budget. `stats()` returns limit, allocated capacity,
used/available slots and sprite count. Exhaustion reports usage and requested count;
the failed batch changes neither live bullets nor the next ID.
Batch arguments use plain dense Lua arrays, including an empty array. Field values are
strictly typed and metatables are rejected. Configuration also reserves native staging;
spawn does not build a general state/JSON tree and is bounded by available pool slots,
not the save-state byte limit. Lua result allocation finishes before the native commit,
so an allocation failure cannot leave a partially spawned batch.
`spawn(specs)` validates a batch of x/y, vx/vy, ax/ay, radius, life, mask, numeric
RGBA color, terrain and piercing fields. `hits()` returns the previous completed
step's hits ordered by projectile, distance fraction and target. `count()` and
`clear()` inspect/reset the batch. Targets use authored AABBs. Terrain blocking
includes ASCII cells and committed Tiled tile rectangles/convex polygons with
circle sweeps, flips and offsets. A revision-cached grid avoids per-bullet full-map
scans. One-way rectangles use one-pixel thickness and block bullets from either side.
Atlas projectile rendering supports registered PNG regions and stable transparent order
([contract](projectile-atlas.md)). Exact target body shapes and streamed terrain remain outstanding.

`sc.navigation.path(sx,sy,gx,gy,budget)` returns a table with status, visited count
and points in zero-based grid cells. Blocked endpoints return `unreachable`.
`flow(gx,gy,budget,slot)` builds one shared field in slot 1..16 and returns handle,
status, visited. `direction(flow,x,y)` returns dx, dy and field status in world coordinates.
Tile passability edits invalidate shared fields; `refresh(flow,budget)` resumes bounded
BFS work. Stale/incomplete fields return zero directions. See [refresh contract](navigation-refresh.md).
`steer(flow,ids,speed)` validates a plain dense batch of distinct live IDs before
assigning velocities. Slot replacement produces a new generation-checked handle;
refresh preserves it. Region/room changes invalidate existing handles.
Rebuild a field after terrain changes. Incremental dirty-region maintenance is
not yet implemented. Lua retains decision-making and behavior state machines.
Finite Tiled tile collision rectangles and convex polygons now contribute a separate
conservative navigation mask, including flips and pixel offsets. Geometry edits publish
the mask atomically; unchanged passability retains valid shared fields. Body-radius
clearance, finite object-layer collision navigation and local dirty-region updates
remain outstanding. Streamed terrain can publish a local navigation region in the
same transaction; unloaded cells are blocked by the streaming modules.

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
object templates into 32x32 blocks. It preserves map/group/layer metadata and decomposes
concave object polygons. `shiny.stream_world` coordinates prepared drawing,
terrain, navigation and persistent objects from these blocks; the direct runtime
Tiled reader still has its older restricted format contract. Map and group
properties are described in [the metadata contract](tiled-map-properties.md).

Build with `SHINY_STREAMING=ON` to enable `sc.stream.open(index_path)` in init.
`request(x,y,commit_frame)` prefetches for a planned simulation tick;
`get(x,y)` returns committed data or nil without waiting. The host suspends
simulation at an unready boundary while continuing native presentation/input.
`release` balances pins and `stats` reports deterministic reservation/publication
counters. Index format 3 includes exact chunk byte counts and stores objects in their owning chunks. See
[streaming](streaming.md) for ordering, cancellation, limits and remaining
GPU/physics/object integration. This is not the 33.3 ms map-load acceptance.

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

`sc.save.write_chunks(slot, changes)` adds atomic per-chunk explicit state;
`read_chunk(slot, key)` reads a bounded record from the selected checkpoint.
See [chunk saves](chunk-saves.md) for snapshot recovery, capacities and the
current synchronous IO limitation.

Save format 3 uses exact project data versions. `sc.save.write(slot)` creates an
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
send, disconnect, close or create sockets. The host services named sessions even
while simulation waits; fixed updates expose at most 64 of 256 buffered events.
`session:stats()` reports queue and send budgets; overflow terminates the session
with an explicit error. See [networking](networking.md). `sc.net.time()` samples
application monotonic time at fixed updates; `shiny.rejoin` supplies bounded
30-second player reservations and token validation across rooms. See
[rejoining](rejoining.md). `sc.net.token()` issues 128 OS-random bits independently
of gameplay RNG, bounded to 64 attempts per fixed update. The complete four-player
protocol and weak-network reconnect acceptance remain unfinished.
Named session `state()` stores explicit bounded protocol data separately from
gameplay checkpoints; replacement is atomic and candidate initialization is read-only.

`shiny.snapshot` provides bounded position snapshot interpolation with uint32
sequence handling, explicit membership changes and no extrapolation. Its offline
visual example is `examples/snapshot`; see [snapshot contract](snapshots.md).
Server clock estimation and the four-player game protocol remain game-level work.

## Agent tooling

`--trace` writes explicit snapshots only when requested. `sc.debug.watch` exposes
up to 64 bounded named values. `tools/compare_traces.py` reports the first differing
frame/path with a float tolerance; it skips only top-level diagnostic hashes and
engine version. `tools/scenario.py` runs a replay in an isolated save directory and
checks final snapshots by default, an exact one-based `frame`, or `any_frame` within
an inclusive `start`/`end` window. Paths such as `watches.quest.stage`,
`entities.0.x`, and `watches.focus` address gameplay, position, event, or UI values
explicitly exposed in snapshots. Assertions accept `equals`, `near` with absolute
`tolerance`, `min`, `max`, `contains`, and `length`; failures name the frame and path.
For example, `{"path":"watches.focus","frame":3,"equals":"right"}`.
See the three game `scenario.json` files and `examples/ui_panels/scenario.json`.
`--profile` separates CPU phases from presentation/pacing, records asynchronous
GPU samples, pool occupancy and process resident memory. `tools/profile_report.py`
checks measured intervals, percentiles and explicit limits; see [profiling](profiling.md)
for the format and remaining measurement gaps. Optional `--debug-stdio` supports
frame/source stepping, breakpoints, stack/local-table pagination, watches and UI
inspection without evaluating Lua expressions. See [debug protocol](debug-stdio.md)
for limits, `--debug-load` startup stops and remaining native-panel acceptance gaps.

`tools/new_game.py` copies `lib/shiny` and local LuaLS docs into each project;
the starter uses `sc.input`, version-3 JSONL replay and `--check-all` commands
with no source-checkout path in project documentation.
`--api.contract_version=1` includes native entity patch/read/batch-record field
contracts and structured parameters, returns and phases for nine entity functions.
Typed member tables drive scalar validation, defaults and reads. `tools/api_docs.py`
generates those LuaLS definitions and [reference tables](api-reference.md); use a
network/streaming-enabled binary and `--reference docs/api-reference.md --check`
to detect stale documentation. Tests also check the four project-local SDK copies.
Other rich types, optional/default arguments, structured error codes and the
remaining function contracts still need conversion; absent/null contracts are not
claims of complete coverage.

## Build and package inspection

`CMakePresets.json` provides Ninja Release configure/build/test presets named
`lightweight`, `full`, and `headless`. The full preset requests all modules;
materials, normal maps, geometry shadows, postprocessing and interactive debugging
are available when enabled. Complete advanced-render acceptance remains pending.
Always inspect `--api.modules` and debugger ready capabilities for actual support.

`tools/package.py` reads the executable's real API contract before copying and
checks custom games using `--check-all`, including their required modules. Its
`package-report.json` records engine version, capabilities, separate engine,
resource, Lua standard-library, game-code and supplied debug-symbol byte totals,
plus relative paths and SHA-256 hashes. `--symbols FILE` adds separate symbols;
embedded symbols remain part of executable size. The report and ZIP overhead
are excluded from the listed totals. Projects are copied conservatively; pruning
unused resources/modules and a complete runtime dependency audit remain open.
