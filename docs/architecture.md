# Architecture and contracts

ShinyCore owns one simulation world per room, one Lua VM per runtime, and one native backend per host. There is no ECS, plugin framework or generated editor scene database.

## Ownership and fixed update

`shiny_core` owns fixed entity/particle pools and one exclusively owned Box2D world. Map layers and native solver storage allocate within explicit limits. `shiny_script` owns validated project data, the Lua boundary and checkpoint operations. `shiny_text` supplies the same UTF-8 layout and font metrics to headless and graphical execution. The raylib backend owns textures, fonts, streams and sound voices.

Each tick applies input, invokes `update(1/60)`, synchronizes authored body changes, steps Box2D with four substeps, updates logical audio/particles/camera, and increments scene time. It then prepares any requested room and calls drawing. `grounded`, `support` and contacts describe the preceding completed step. Raycasts and joint creation synchronize pending bodies early without advancing time.

The real-time loop caps catch-up at eight steps. Bounded graphical runs advance one simulation step per displayed frame; headless uses the same updates and draws with alpha zero. The renderer pixel-snaps the latest state. Draw cannot mutate engine state or first-load modules; Lua-local side effects cannot be prevented generally.

## Bodies and handles

Pixels use +Y down, angles use radians, and Box2D uses 32 authored pixels per meter internally. Force and impulse inputs scale correspondingly; density is in solver mass per square meter. Bodies may be static, kinematic or dynamic, with boxes, circles, longer-axis capsules, convex polygons or 1..4 compound shapes. Polygons require 3..8 distinct convex points. Materials and filters are shared across compound shapes.

Entity positions locate the unrotated bounds' top-left; rotation is about the center. `sc.get` copies body data; `sc.set` validates before replacing the entity. Shape/material/filter changes recreate the body and destroy attached joints at the next sync. `body=false` removes it. Legacy `dynamic=true` creates a dynamic box with zero friction. Legacy static artwork remains bodyless. `solid=false` disables collision filtering.

ASCII solid runs are merged to avoid contact seams. Tiled collision geometry is compiled into the same solver, not a second collision engine. Map virtual boundaries are solid. One-way platforms use prior bounds and relative vertical velocity; use unrotated top-face geometry. `drop` temporarily disables their contacts. Lua implements coyote time, jump buffering and moving-platform velocity inheritance; Workshop supplies an editable controller.

Contacts are sorted by a/b/phase. Terrain has ID 0. Solid contacts carry normals from a to b, sensors emit begin/end edges with zero normals; destroyed shapes may not produce end events. Limits are 64 contacts per body and 1024 collected records per step, with explicit failures. `overlap` and `physics.query` use authored AABBs, while `physics.ray` uses actual solver shapes. Joints expose distance, revolute and local-vertical prismatic constraints, without motors in 0.2.

Entity/joint IDs use slot plus generation; audio IDs use six slot bits. IDs are room-local and must not be stored as persistent game references. Generations have finite rollover limits. Use stable authored names in saves and recreate references in init. The world hash contains explicit logical fields including compound geometry, not padding, addresses, solver caches, joints, resources or arbitrary Lua locals; audio is exposed separately in snapshots. It is diagnostic evidence, not a complete future-state identity.

## Lua and failure boundaries

A runtime and its script cannot move: Lua's allocator/extraspace borrow stable addresses. The host transfers a unique runtime pointer. Native owners are move-only and release resources before their devices. Expected loading failures use `std::expected`; outer exception guards report diagnostics.

Lua is built as C and errors use longjmp. No C++ owning local may survive a potentially raising Lua operation. Native data conversion uses raw non-allocating reads and bounded stack checks, then pushes from script-owned values. All registered engine callbacks have exception guards, while native network operations retain their explicit expected-result boundary. Destructors and C solver callbacks must not throw.

Modules resolve `game.controller` to project-local `game/controller.lua`, share the VM's allocation/instruction budget, cache their returned value and reject cycles. Native/process/file modules and arbitrary load APIs are absent. Paths are lexical project-relative checks; symlinks and hostile native resource parsers are not isolated.

Function tables drive both registration and `--api`; tests compare names and writable/read-only fields with `docs/api.lua`. Network functions use the same approach with their two captured upvalues. Unknown configuration fields and invalid authored types/ranges should fail explicitly.

## State and transactional rooms

`sc.state` is an ordered plain-data object with independent copies on read. Dense arrays, string-key objects, UTF-8 strings, finite numbers and booleans are supported; nil deletes a key. Empty Lua tables represent objects. Metatables, cycles, sparse/mixed arrays and JSON null are rejected. Serialized state is bounded to 256 KiB and depth 16.

A scene request inherits explicit state unless a replacement is supplied. The host constructs a new world and VM, runs load/init/initial draw, validates physics and declared assets, and prepares GPU resources before swapping ownership. Graphics failure retains the old runtime/cache and reports the diagnostic; headless failure exits nonzero. Viewport dimensions must match the live window. With `--debug-keys`, F5 follows the same process and retains explicit state. Module caches and Lua locals restart.

Only persistent music voices carry across rooms/F5; other voices expire. Candidate init may create new voices within the remaining capacity. The renderer retains matching persistent handles and replaces other resources after preparation succeeds.

## Checkpoints

`project.id` scopes saves. Records contain format=1, project, data_version, scene and state. `write` runs only in update, writes a sibling temporary file, flushes/closes it and atomically replaces the target. This protects the previous file from ordinary write/rename failures; POSIX power-loss durability and simultaneous writers to one slot are not promised.

`load` validates a complete candidate, rejects future versions and optionally calls the module named by project.migrate with `(oldVersion,newVersion,state)`. Engine mutation is disabled during migration module loading and execution. The returned state passes the same type/UTF-8/depth/size checks. Successful loading requests room reconstruction; no VM locals, entities, solver caches, audio position or animation time are restored automatically.

Graphical defaults: LOCALAPPDATA/ShinyCore on Windows, ~/Library/Application Support/ShinyCore on macOS, XDG_DATA_HOME/shinycore or ~/.local/share/shinycore on Linux. Headless defaults to up to 16 in-memory slots; `--save-dir` opts into disk. Check modes invoke no updates and disable save operations. `--check-all` validates the entry and each declared room with empty state.

## Resources and presentation

Tiled support is finite orthogonal, square equal-size tiles, right-down ordering, integer GID arrays, tile/object layers and one convex collision polygon per tile. Inline and external tilesets, spacing/margins, H/V/diagonal flips, visibility/opacity and tile-layer pixel offsets are supported. Unsupported compression/encoding, groups, images layers, parallax, tint, templates, rotated collision objects and hex rotation bits fail. Object records are copied to Lua for factories; they do not spawn native entities automatically. See Workshop for the editor workflow.

Resources declare image, WAV sound, Ogg Vorbis music, or standalone TTF/OTF font paths. Header structure/duration and font metrics validate without devices; graphical builds additionally decode images/audio and validate atlas rectangles. Text uses declared glyph repertoires plus ASCII, tries fallback fonts in name order and warns once per room when replacing a missing glyph with ?. Layout uses codepoint wrapping and optional alignment; no kerning, shaping, bidi or IME is provided.

Audio has a deterministic logical clock even when muted/headless, 32 SFX voices and two music streams, pause/loop/pitch/volume/fades, and generation-checked handles. Pause freezes clock and fades. The native backend decodes sounds and streams music; device playback timing is not an exact simulation clock.

Rendering uses scene/light/composite targets, integer scaling, nearest-neighbor textures, layers and particles. Tiled layers interleave with entity layers; custom world drawing follows entities and screen drawing follows lighting. Light occlusion remains an approximate ASCII-grid DDA; it does not reflect Tiled or dynamic-body geometry. Default sc.message/debug text uses the ASCII font; multilingual authored UI uses sc.text.

Optional ENet transport remains separate from the solver and game protocol; see networking.md. Platform acceptance and sanitizer availability are recorded in verification.md.

## Device input

The host owns `ScInputBuffer`; the world owns a fixed-capacity `ScDeviceInput` snapshot, with no raylib or Lua dependency. The native backend selects a single gamepad and samples named controls; Lua reads only the committed fixed tick. Buffered presses/releases survive render-only iterations and are consumed once, including taps with both edges in one tick. OS repeat is suppressed. Hardware events not reported by the backend cannot be recovered.

Loss of window focus submits neutral state and release edges. A disconnected pad cannot be replaced until at least one neutral simulation tick has been consumed. A scene transition or successful reload inherits held device/action state before init, with consumed edges cleared. The host buffer survives both. Failed reloads preserve the active runtime.

The six legacy actions derive from the same device state, retaining the 0.25 stick direction threshold and adding vertical left-stick motion. Logical actions OR all sources; releasing one source while another remains held does not release the action. `--debug-keys` explicitly enables host shortcuts; otherwise all keyboard controls, including Escape, belong to the game.

Both replay versions suppress live device input. Legacy masks do not fabricate device state. Version 2 contains normalized controls and derives legacy actions just like live sampling. State hashes include device held/edge bits, connection state and each axis as an explicit logical field; device metadata and native slot numbers are excluded. Hash values therefore change from older binaries. See [input.md](input.md) for names and format.
