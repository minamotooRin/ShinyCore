# Architecture and contracts

This is the current development implementation, not a complete-edition acceptance report. Expanded importer output and streaming APIs are documented separately in [new systems](new-systems.md); direct runtime Tiled loading retains the subset described below.

ShinyCore owns one simulation world per room, one Lua VM per runtime, and one native backend per host. There is no ECS, plugin framework or generated editor scene database.

With streaming enabled, the application owns one lazy `ScContentLoader` worker in
`src/content/content_loader.cpp`; room-owned `ScStream` caches borrow it. Its three
concrete job types read map indexes, map chunks or decode PNG pixels, using owned inputs and
results without room/VM/world/GPU references. Room destruction cancels jobs without
joining; only application shutdown waits for an in-flight read. Chunk publication
remains ordered on the main thread. PNG jobs reserve decoded bytes and use a bounded
CPU parser in `src/content/png.cpp`, also available in headless builds. The native
`ScImageCache` retains CPU pixels by counted references and bounded LRU residency;
`src/render/image_cache.cpp` supplies its host-thread texture adapter with bounded
upload counts and retained active textures on failure. Room-owned `ScRoomImages`
stages explicit `sc.images` replacement sets, gates the next fixed boundary and
publishes only on commit. Images declared `stream=true` bypass eager decoding;
other images retain eager preparation. Stream-world optionally collects tile,
animation, image-layer and object dependencies, prepares them before outgoing saves,
and publishes them with terrain/objects. Wayfarer uses this path. Image-dependent
map edits expose the same prepare/publish transaction. Explicit image reload stages
private revisions beside active pixels; commit changes future cache lookup preference,
while other owners retain their old versions until release. See [image residency](image-residency.md).
Standalone native streams can own a private loader; see [lifetimes and limits](streaming.md).

`src/content/save_io.cpp` supplies a lazy, single-transaction native read/write/delete service
with owned checkpoint payloads, explicit result observation and retry. The host
owns it and gates the next fixed update for Lua async requests, while servicing UI,
devices and network. Unreleased requests exclude other save operations, room changes
and reloads; releasing invalidates the request, while a successful read pins its
selected snapshot. Shutdown drains writes and
reports failure. The stream-world module and Wayfarer stage outgoing snapshots,
save asynchronously, then publish the next world. They preserve the old world on
failure and expose retry/cancel through UI; cancellation never undoes disk commits.
Synchronous APIs remain explicit utilities. Batch async reads select or retain one
complete snapshot on the same worker; successful release pins its index for later
reads. Stream-world uses this API before preparing entering objects/terrain, then
saves outgoing data and publishes. Read/write exclusion prevents this application's
collection from racing readers. Wayfarer probes its title checkpoint on the worker;
after release, `sc.save.load` consumes the pinned index without rereading the slot.
Wayfarer uses async deletion for a confirmed new journey; synchronous deletion and
slot enumeration remain explicit utilities.
Rooms declaring `project.stream_indexes` stage index parsing on the content worker
between scene-table loading and `init`, then consume the parsed index in `sc.stream.open`.
Undeclared indexes still open synchronously.
See [save lifetimes](chunk-saves.md) and [world coordination](stream-world.md).

The pixel scene uses the authored render resolution. Unlayered screen/UI commands
compose afterward at native window resolution, with the same logical coordinates,
ordering and viewport clips. TTF/OTF glyph pages use the displayed pixel size while
layout retains shared headless metrics; raster variants are dropped when viewport
scale changes. Default bitmap text and world text retain pixel-scene styling.

## Ownership and fixed update

`shiny_core` owns fixed entity/particle pools and one exclusively owned Box2D world. Map layers and native solver storage allocate within explicit limits. `shiny_script` owns validated project data, the Lua boundary and checkpoint operations. `shiny_text` supplies the same UTF-8 layout and font metrics to headless and graphical execution. The raylib render backend owns textures and fonts; its application-lived audio adapter in `src/audio/` owns the device, streams, decoded sounds and playback aliases.

Particles use dedicated room-owned columns (`ScParticles`), with a dense active prefix
and stable compaction to preserve translucent order. Emit/step reuse allocated storage;
zero project capacity releases the columns. Burst exhaustion fails atomically and does
not advance visual RNG. See [particle contract](particles.md).

Each tick applies input, invokes `update(1/60)`, synchronizes authored body changes, steps Box2D with four substeps, synchronizes [visual attachments](attachments.md) after root movement and before projectiles/camera, updates particles/camera, and increments scene time. The host then advances application audio before preparing any requested room and calling drawing. `grounded`, `support` and contacts describe the preceding completed step. Raycasts and joint creation synchronize pending bodies early without advancing time.

The real-time loop caps catch-up at eight steps. Scheduled streaming gates run before input consumption and update; an unready due batch holds simulation time while native presentation and input sampling continue (see [streaming](streaming.md)). Bounded graphical runs advance at most one simulation step per displayed frame; loading/paused frames do not count toward --frames, and headless uses the same updates and draws with alpha one (the latest completed fixed state). Optional [display interpolation](presentation.md) blends room-owned prior poses without mutating simulation; bounded/paused/loading presentation uses current state. [Camera controls](camera.md) share the transform with draw-time coordinate conversion, culling and lighting. Gameplay coordinate conversion remains fixed-step. Draw cannot mutate engine state or first-load modules; Lua-local side effects cannot be prevented generally.

## Bodies and handles

The optional scene `ui_update(dt)` runs before gameplay, including loading
waits and debug pauses. Live devices update UI once per host frame; replays and
headless runs update it before each fixed snapshot is delivered to gameplay.
Its temporary device snapshot is restored before returning
to simulation; gameplay mutation remains forbidden. Text focus, clipboard, explicit
pause and exit are the limited host-facing exceptions. See [UI lifecycle](ui-lifecycle.md)
for replay behavior. The Lua action module bridges UI consumption to fixed updates,
including pending edges and held-action release, without mutating raw input.

Pixels use +Y down, angles use radians, and Box2D uses 32 authored pixels per meter internally. Force and impulse inputs scale correspondingly; density is in solver mass per square meter. Bodies may be static, kinematic or dynamic, with boxes, circles, longer-axis capsules, convex polygons or 1..4 compound shapes. Polygons require 3..8 distinct convex points. Materials and filters are shared across compound shapes.

Entity positions locate the unrotated bounds' top-left; rotation is about the center. `sc.get` copies body data; `sc.set` validates before replacing the entity. Shape/material/filter changes recreate the body and destroy attached joints at the next sync. `body=false` removes it. Legacy `dynamic=true` creates a dynamic box with zero friction. Legacy static artwork remains bodyless. `solid=false` disables collision filtering.

ASCII solid runs are merged to avoid contact seams. Tiled collision geometry is compiled into the same solver, not a second collision engine. Map virtual boundaries are solid. One-way platforms use prior bounds and relative vertical velocity; use unrotated top-face geometry. `drop` temporarily disables their contacts. Lua implements coyote time, jump buffering and moving-platform velocity inheritance; Workshop supplies an editable controller.

Contacts are sorted by a/b/phase. Terrain has ID 0. Solid contacts carry normals from a to b, sensors emit begin/end edges with zero normals; destroyed shapes may not produce end events. Limits are 64 contacts per body and 16384 collected records per step by default, with explicit failures. `sc.overlap` uses authored AABBs. `physics.query`, `physics.overlap`, `physics.sweep` and `physics.ray` use actual solver shapes; see [query contracts](physics-queries.md). Distance, revolute and local-vertical prismatic joints expose atomic limit/motor controls; see [joint contracts](joints.md).

Entity/joint IDs encode room epoch, generation and slot within 52 bits; audio IDs use six slot bits. Entity/joint IDs are room-local and must not be stored as persistent game references. Joint slots retire at generation exhaustion; room epochs never wrap. Use room paths plus `persistent_id` in saves and resolve rebuilt objects through `sc.identity` (see [object identity](identity.md)). The bounded room-owned index retains unloaded/deleted records until the room ends; games explicitly restore persistent object state in init. The world hash contains explicit logical fields including compound geometry, not padding, addresses, solver caches, joints, resources or arbitrary Lua locals; audio is exposed separately in snapshots. It is diagnostic evidence, not a complete future-state identity.

## Lua and failure boundaries

Profiling is opt-in: the simulation receives an optional timing accumulator;
host-owned JSON Lines output and OS memory sampling stay outside the core.
The raylib backend uses custom frame control to time submission, swap/pacing and
input polling separately. Its bounded GPU query ring never waits for a result
and is destroyed before the graphics context. See [profiling](profiling.md).

A runtime and its script cannot move: Lua's allocator/extraspace borrow stable addresses. The host transfers a unique runtime pointer. Native owners are move-only and release resources before their devices. Expected loading failures use `std::expected`; outer exception guards report diagnostics.

Lua is built as C and errors use longjmp. No C++ owning local may survive a potentially raising Lua operation. Native data conversion uses raw non-allocating reads and bounded stack checks, then pushes from script-owned values. All registered engine callbacks have exception guards, while native network operations retain their explicit expected-result boundary. Destructors and C solver callbacks must not throw.

Modules resolve `game.controller` to project-local `game/controller.lua`, share the VM's allocation/instruction budget, cache their returned value and reject cycles. Native/process/file modules and arbitrary load APIs are absent. Paths are lexical project-relative checks; symlinks and hostile native resource parsers are not isolated.

Function tables drive both registration and `--api`; tests compare names and writable/read-only fields with `docs/api.lua`. Network functions use the same approach with their two captured upvalues. Unknown configuration fields and invalid authored types/ranges should fail explicitly.

Entity scalar descriptors use typed member pointers for validation, initial values,
snapshots and field metadata. Coupled sprite/body rules remain explicit code with
contract tests; body, compound shape and project content fields have native structured metadata.
Structured entity function metadata generates annotations and
[reference tables](api-reference.md). Audio descriptors now similarly share voice
field ranges with validation; function contracts include optional parameter
defaults/ranges and a read/patch function's conditional mutation phases. Navigation
bindings live in `src/script/script_navigation.cpp`; their named multiple returns
generate distinct LuaLS return entries. The same region preparation routine is used
by explicit navigation changes and streamed terrain transactions. All registered functions now expose contracts; open data dictionaries and some
resource-specific constraints remain described rather than closed record schemas.
Core drawing, input convenience, map and utility descriptors are grouped in
`src/script/core_contract.cpp`. They record strict optional arguments, draw-only
phases and presence-triggered map mutation without another callback implementation.
See [core contracts and evidence](core-api.md).
Network callbacks retain their context/guard closures while using the common API
registration descriptors for JSON documentation. Event and budget snapshots have
structured field contracts. Conditional mutation distinguishes omitted arguments
from explicit nil for session state; existing non-nil patch contracts are unchanged.
Application/settings bindings live in `src/script/script_application.cpp`; they
borrow the existing settings service and keep scene pause/exit flags room-local.
State/module contracts remain with data conversion; bounded debug observations
validate serialized UTF-8 before commit. See [application contracts](application-state.md).
Projectile bindings similarly live in `src/script/script_projectiles.cpp`: scalar
field descriptors drive strict Lua validation and metadata, with native defaults.
Seven function contracts and spec/hit/stats records generate the Agent-facing SDK;
manual integer, boolean, phase and transaction rules have focused contract checks.
See [projectile contracts](projectiles.md).
Input bindings have their own `src/script/script_input.cpp`; all 18 functions and
nested snapshot records expose structured contracts. Validation keeps exact argument
counts, UTF-8 limits, optional controller slots and clipboard/focus mutation phases
consistent with generated annotations. Contract metadata supports exclusive numeric
upper bounds, used by the input deadzone; the JSON default null is rendered as Lua nil.
Save bindings and their seven function contracts live in `src/script/script_save.cpp`,
separate from plain data conversion and project loading. Slot and checkpoint records
have generated field contracts; listing merges primary and backup names before reading
each complete snapshot. Optional frame/time metadata is validated in the content layer.
Finite tile edits preflight terrain plus both the room and active custom navigation
grid before publishing any mask. Each grid advances its own revision only when its
obstacles change, preserving local flow handles and authored region cells.
Path queries and shared fields accept an explicit circular body radius. The core
derives conservative cell-center clearance from blocked cells and grid boundaries,
using fixed scratch storage; a field retains its radius and mask across bounded
refresh calls. This does not inspect entities or replace physical collision response.
Batch edits reuse room-owned scratch storage reserved after project limits load.
A fixed 8 KiB slot bitmap rejects duplicate IDs without quadratic scans. A failed
batch never commits; the next call resets the bitmap before validation.

## State and transactional rooms

`sc.state` is an ordered plain-data object with independent copies on read. Dense arrays, string-key objects, UTF-8 strings, finite numbers and booleans are supported; nil deletes a key. Empty Lua tables represent objects. Metatables, cycles, sparse/mixed arrays and JSON null are rejected. Serialized state is bounded to 256 KiB and depth 16.

A scene request inherits explicit state unless a replacement is supplied. The host constructs a new world and VM, runs load/init/initial draw, validates physics and declared assets, and prepares GPU resources and silent native audio voices before swapping ownership. Graphics failure retains the old runtime/cache and reports the diagnostic; headless failure exits nonzero. Viewport dimensions must match the live window. With `--debug-keys`, F5 follows the same process and retains explicit state. Module caches and Lua locals restart.

A rejected candidate does not undo the active room's completed tick: trace, debugger
step completion and frame limits still advance normally. A successful subsequent room
commit clears the previous loading diagnostic; the full error remains in stderr logs.

Only persistent music voices carry across rooms/F5; other voices expire. Candidate init may create new voices within the remaining capacity. The audio adapter retains matching persistent handles and replaces other voices after all GPU/audio preparation succeeds. Draft preparation cannot play or pause the active device voices. See [audio ownership](audio.md).

Named network sessions belong to the application. Host iterations service bounded
event rings even while simulation is paused or awaiting chunks; a fixed update
publishes at most 64 FIFO events and resets send budgets. Overflow closes only
the affected transport and retains an inspectable error. Candidate rooms may bind
but cannot consume events or mutate sessions. See [networking](networking.md).
The application monotonic network clock is sampled with each fixed update, remains
stable during the callback and survives room changes. It is outside gameplay hashes
and checkpoints; Lua protocol deadlines must not use resettable room time.
Named sessions also own explicit bounded plain protocol data. `session:state()`
copies it into Lua; validated replacement is atomic and forbidden in candidate
initialization/draw. It survives room teardown but is not part of `sc.state`, save
records or automatic traces. Closing the session releases it.

## Checkpoints

`project.id` scopes saves. Records contain format=3, project, data_version, scene, state and a chunk reference index. `write` runs only in update, writes a sibling temporary file, flushes/closes it and atomically replaces the target. This protects the previous file from ordinary write/rename failures; POSIX power-loss durability and simultaneous writers to one slot are not promised.

`load` validates the current format (3) and exact project data version, recovering from a previous valid `.bak` when the main file is damaged. Writes atomically preserve the previous valid record; malformed records never replace a good backup. Unsupported formats/data versions are rejected. Successful loading requests room reconstruction; VM locals and solver caches are not restored. `read`, `list` and `delete` expose records, sorted metadata and slot removal.

`write_chunks` commits explicit chunk changes with the same checkpoint. Immutable bounded records are written before the index; the current and previous valid indices retain their referenced files. Opening a snapshot validates every record one at a time; a damaged chunk falls back to the whole previous checkpoint. Per-chunk reads then use one cached index and never silently mix generations. Unreferenced engine records are collected after successful commits. Async reads/writes reuse this format and validation/commit path. See [chunk saves](chunk-saves.md) for limits.

Graphical defaults: LOCALAPPDATA/ShinyCore on Windows, ~/Library/Application Support/ShinyCore on macOS, XDG_DATA_HOME/shinycore or ~/.local/share/shinycore on Linux. Headless defaults to up to 16 in-memory slots; `--save-dir` opts into disk. Check modes invoke no updates and disable save operations. `--check-all` validates the entry and each declared room with empty state.

## Resources and presentation

Direct Tiled loading supports finite orthogonal, square equal-size tiles, right-down ordering, integer GID arrays, tile/object layers and one convex collision polygon per tile. Inline and external tilesets, spacing/margins, H/V/diagonal flips, visibility/opacity and tile/object-layer pixel offsets are supported. Compression/encoding, groups, image layers, parallax, tint, templates, rotated tile-local collision and hex rotation bits fail in this direct loader; expanded offline conversion is separate.

Object records are copied to Lua with layer offsets applied. Explicit string property `collision=solid` adds a rotated rectangle or 3..8-point convex polygon to room-owned static terrain; `one_way` requires an unrotated rectangle, and the default `empty` leaves creation to Lua. The same committed geometry feeds physics, navigation and projectile collision. Authored object shapes survive tile edits, share the terrain capacity and never create entity handles. Lua factories must not duplicate these bodies. See [navigation](navigation-refresh.md) for limits.

Resources declare image, WAV sound, Ogg Vorbis music, or standalone TTF/OTF font paths. Header structure/duration and font metrics validate without devices; graphical builds additionally decode images/audio and validate atlas rectangles. Text caches metrics and glyph pages on demand, tries fallback fonts in name order and warns once per room when replacing a missing glyph with ?. Layout uses codepoint wrapping and optional alignment; no complex shaping, bidi or color Emoji is provided. Windows IME composition and candidate positioning live in src/platform; physical IME acceptance remains pending.

The application owns the logical audio mixer, with 32 SFX voices, two music streams,
pause/loop/pitch/volume/fades and generation-checked handles. Candidate rooms receive
isolated drafts, committed only after successful preparation; persistent music keeps
its ID and position. The native backend decodes sounds and streams music; device
playback timing is not an exact simulation clock. See [audio ownership](audio.md).

Rendering uses scene/light/composite targets, integer scaling, nearest-neighbor textures, layers and particles. Tiled layers interleave with entity layers; custom world drawing follows entities and screen drawing follows lighting. The lightweight renderer uses ASCII-grid shadows; optional [advanced lighting](lighting.md) uses committed map and entity geometry with sampled soft shadows. Default sc.message/debug text uses the ASCII font; multilingual authored UI uses sc.text.

Optional ENet transport remains separate from the solver and game protocol; see networking.md. Current acceptance limits are recorded in the [closeout audit](acceptance-audit-20260928.md); sanitizer evidence is in [sanitizer verification](sanitizer-verification.md).

## Device input

The host owns `ScInputBuffer`; the world owns a fixed-capacity `ScDeviceInput` snapshot, with no raylib or Lua dependency. The native backend samples four stable gamepad slots plus one selected-controller view, keyboard, mouse and text; Lua reads only the committed fixed tick. Buffered presses/releases survive render-only iterations and are consumed once, including taps with both edges in one tick. OS repeat is suppressed. Hardware events not reported by the backend cannot be recovered.

Loss of window focus submits neutral state and release edges. A disconnected pad cannot be replaced until at least one neutral simulation tick has been consumed. A scene transition or successful reload inherits held device/action state before init, with consumed edges cleared. The host buffer survives both. Failed reloads preserve the active runtime.

The six legacy actions derive from the same device state, retaining the 0.25 stick direction threshold and adding vertical left-stick motion. Logical actions OR all sources; releasing one source while another remains held does not release the action. `--debug-keys` explicitly enables host shortcuts; otherwise all keyboard controls, including Escape, belong to the game.

Replays suppress live device input. Legacy masks do not fabricate device state. Version 2 contains normalized keyboard/selected-controller controls; version 3 adds four pads, mouse, committed text, composition and paste input. `--record` writes version 3 fixed-tick snapshots. State hashes include these explicit input fields, including stable pad slots. Hash values therefore change from older binaries. See [input.md](input.md) for names and format.

Optional Agent debugging lives in src/dev/debug_stdio.*; bounded paused-stack and
local-table reading lives in src/dev/lua_inspect.*. Redirected nonblocking
stdin reads live in src/platform/debug_input.*. The host owns the protocol session
and attaches it to the current room at fixed-update boundaries. Devtools OFF
excludes these sources; even an enabled build allocates no channel until
--debug-stdio is requested. Source stops wait inside the shared instruction/debug
hook, preserving the live stack and remaining quota. Only the host device/network
pump runs while stopped; it cannot reenter Lua. Raw stack inspection never evaluates
expressions or metamethods. Local paths compare existing raw keys without interning
Lua strings; a request has a shared scan budget and always restores stack height.
The borrowed VM pointer exists only during the stop;
VM teardown clears the hook observer before installing the finalizer quota.
Candidate loads attach the same observer before opening their VM. `--debug-load`
also creates the protocol before initial load and stops at the first Lua line;
without it the initial pause remains after initialization. Loading stops inspect
candidate drafts while the host pump renders the active room only. Debugger quit
unwinds loading without committing the candidate. See [protocol](debug-stdio.md).
The optional inspector owns a VM-local weak UI registry. Its field keys are pinned
at bootstrap; raw inspection neither allocates in Lua nor invokes metamethods while
C++ assembles results. UI objects can be collected normally and no borrowed Lua
object survives an inspection request. Stdio and the graphics-backend-owned panel
share entity/resource/UI/capacity readers. The panel is off until explicitly opened
with debug keys; builds without devtools omit its implementation and registry.

Optional materials live in room-owned CPU content (`ScMaterials`), outside the
simulation core and Lua VM. Draw packets contain checked material IDs only.
Image defaults and entity overrides also live in this pool; entity slots are
qualified by complete generation handles. Dead generations do not retain resources.
The backend borrows these bindings only during frame submission. Ordered quad
submission for tiles, projectiles and particles shares one allocation-free helper,
which restores shader/blend state and refreshes samplers after batch flushes.
The backend separately owns GL programs and validates compile/link/uniform types
before replacing them. Candidate room preparation holds a separate program set
until all resources succeed; runtime reload retains the last valid program on failure.
The host borrows its current script to render for one call, never transfers VM
ownership. Default builds omit material content/bindings/backend sources entirely.
See [material contracts and validation limits](materials.md).

The optional postprocess chain is room-owned data with at most four material IDs.
The backend owns one or two reusable RGBA8 color-only targets, with an explicit
byte budget checked before configuration commits. Candidate preparation owns a
separate target set until the room commits. Passes run after world composition
and before screen UI; clearing the chain releases its targets. See [postprocessing](postprocess.md).

Optional `shiny_lighting` builds CPU occluder outlines without GPU/Lua/solver objects.
The backend owns fixed polygon/angle/output buffers prepared with candidate rooms;
collection reads committed terrain and transformed body geometry, never steps physics.
Room-owned lighting settings and submission diagnostics cross the Lua boundary through
`sc.lighting`. Equal-weight source samples soften shadows without gameplay RNG.
Explicit point lights are bounded draw commands, cleared before each draw callback.
One CPU preflight merges entity glows and point commands, culls invisible lights and
checks total/shadow budgets identically in headless and graphical hosts. Shader and
shadow stages consume the resulting fixed frame data without creating entities.
See [geometry shadows and limits](lighting.md).

Visual occlusion overrides are room presentation records indexed by entity slot and
qualified by the complete generation handle. Storage is sized at project load;
updates choose physical defaults, bounds, stored shape or no shadow without touching
the world/solver. Reused slots ignore stale overrides, and room teardown releases them.

Normal-map bindings are initialization-only image-path pairs owned by the room.
When present, a backend-owned color target records transformed normals in the same
world draw order; light fans sample it for diffuse response while retaining their
existing occlusion and attenuation. Candidate programs/target commit with all other
resources. Shader compilation and color-target ownership are shared with materials
and postprocessing; no default shader silently substitutes for compilation failure.
Headless checks validate data and tangent transforms, not GPU output. See [normal maps](normal-maps.md).

## Candidate image preparation

`scene.preload_images` stages the room's initial streamed images after init, using
its own `ScRoomImages`. The host retains a unique candidate across presentation
frames, polls decode/upload once per frame, and holds fixed input/time. Only after
image commit, first draw and native resource preflight does it replace the active
runtime and audio draft. Failure destroys the candidate and preserves active
resources. Tick trace/debug completion waits for that decision, including at a
frame limit. Startup displays a native loading notice without another Lua VM or
physics world. Eager resources, Lua and ordinary project/map parsing still run synchronously;
declared stream indexes are the exception and finish before `init`;
see [image residency](image-residency.md) for the exact boundary.

The Windows executable owns a platform manifest declaring UTF-8 process paths
(Windows 10 1903+), long-path awareness and asInvoker execution. RC dependencies
track manifest changes; GNU/MinGW excludes only its default manifest resource to
avoid conflicting language-tagged manifests. No path/locale policy enters the core.
See [portable-package evidence](verification/portable-packages.md).
