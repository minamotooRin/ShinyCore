# Architecture and contracts

ShinyCore separates simulation, scripting, presentation, and an optional network transport under one host. There is no required editor or generated scene state.

```text
game/main.lua, rooms/*.lua, PNG assets
             │ 22 explicit sc.* functions
             ▼
shiny_script (Lua 5.4) ───────► shiny_core (C11, no heap or platform calls)
             ▲                        │ world + presentation commands
             │                        ▼
          host CLI ─────────────► native renderer (raylib)
             │
             └── headless replay, JSON snapshot, diagnostics
```

## Simulation ownership

`src/core.c` owns fixed-capacity arrays and advances only when `sc_step()` is called. Lua owns authored game rules. The renderer reads world state, owns GPU/audio resources, and never advances simulation or consumes simulation randomness. The host owns scene loading and the event loop.

Each fixed tick runs:

1. Clear the previous tick's audio commands and apply one input snapshot.
2. Call `update(1/60)`. `grounded` is the preceding tick's physics result.
3. Integrate dynamic entities, sweep X then Y against tiles, update particles and camera, increment tick.
4. Submit audio in a graphical run. Apply any deferred scene request by constructing a fresh candidate world and VM.
5. Call `draw(alpha)` once per displayed frame; headless execution calls it once per tick with alpha zero.

The live loop caps catch-up at eight steps and discards excess whole steps after a stall. Short input edges are retained until a fixed update consumes them. Bounded graphical runs (`--frames`) deliberately advance exactly one simulation tick per displayed frame, making automated graphical replays comparable with headless output. The renderer pixel-snaps the newest state; it does not interpolate sprite positions. `alpha` is available for authored visual effects and always lies in `[0,1]`.

The host also calls `draw(0)` during initial validation. Render callbacks must keep Lua state unchanged. Engine mutation is rejected in `draw`; Lua-local mutation cannot be generally prevented. Randomness for simulation comes from `sc.random`, never wall-clock time or rendering.

## Entity and collision contracts

Entity IDs contain an 8-bit slot and a 24-bit generation. Destroying a slot invalidates outstanding handles; exhausted capacity fails explicitly. Generation rollover after 16,777,215 reuses of a single slot is a theoretical aliasing limit. IDs are scoped to one scene; do not retain them across scene transitions.

Dynamic entities integrate velocity and `world.gravity * entity.gravity`. With `solid=true`, they collide against the map. Static entities are artwork or overlap targets; they do not become solid obstacles for other entities. `sc.overlap` is an AABB query and does not automatically resolve collisions.

Tiles are `.` (empty), `#` (solid), `=` (one-way, downward crossings only). Virtual map boundaries are solid. Sweeps inspect crossed grid cells rather than taking one discrete move, so fast bodies cannot skip a thin wall. There is no general depenetration: initial spawns and teleports should be in free space. One-way platforms do not implement a drop-through action. Broad-phase entity physics, slope contacts and moving-platform attachment are separate future work.

## Script boundary

Scenes return plain data tables with optional callbacks. Unknown configuration keys error rather than silently falling back. Config and entity patches may not have metatables. `sc.get` returns a snapshot; `sc.set` validates a patch before replacing the entity. Public Lua calls reject invalid types, non-finite numbers, stale IDs and out-of-range values.

Only selected base, table, string, math and utf8 functionality is available. File/process/module loading and `math.random` are absent. `pcall`/`xpcall` cannot suppress the callback budget; `__gc` finalizers are disabled because Lua does not run instruction hooks inside them. VM allocation is limited to 16 MiB. C-library operations and image decoding are not a hard wall-time sandbox. Paths are checked lexically; symlinks are not isolated. Run projects you trust.

## Rendering and resources

The backend uses three native-resolution render targets: scene color, light field and final composite. Point lights build 128-ray triangle fans with tile-grid DDA occlusion. Ambient plus additive illumination is multiplied into scene color; particles, HUD and debug overlays follow. The output scales by an integer with nearest-neighbor sampling and letterboxing. Light occlusion uses solid tiles only, not sprites or one-way platforms. This is a compact approximate lighting model, without normal maps, bloom, fluid simulation or indirect lighting.

All entities draw after the map, in stable ascending `layer` order. Custom world-space draw commands follow entities and receive lighting; screen commands follow the light composite. `sc.message` displays a persistent upper-left HUD message. The default bitmap font is designed for ASCII at 10 pixels or integer multiples; rendering smaller sizes loses glyph detail. Supply ASCII UI in this version.

Asset paths resolve against the project root, independent of the shell working directory when an absolute project path is supplied. GPU textures cache by relative path and unload on scene reload/close. Frame selection is zero-based, row-major; atlas dimensions must be divisible by frame dimensions. Graphical scene preflight decodes initial assets without a GPU before swapping the world. Newly introduced runtime assets are checked when rendered; their failures stop with a diagnostic.

F5 reload constructs and validates a candidate. Failure preserves the old VM/world/cache. Success resets scene state and its assets. A deferred scene-transition failure exits nonzero after preserving the old world in memory until cleanup. No incremental Lua state migration is promised.

## Replay and state evidence

Replay frames are global to the process; scene ticks reset on scene entry. A held action remains held across a transition without emitting a new press. Each scene restarts from the requested host seed. Same binary, platform, seed, project and input produce reproducible simulation; cross-architecture bitwise floating-point equivalence is not promised.

The hash serializes explicit logical C fields rather than struct padding or addresses. It excludes transient draw and tone queues; it includes entity generations, map edits, active particles, input, RNG, camera and authored presentation attributes. Arbitrary Lua locals are absent. Snapshots are evidence and test output, not a complete checkpoint format or a save/load contract.

## Extending the engine

`shiny_net` owns native UDP endpoints through pinned ENet, independent of the core, Lua, and graphics. It is absent unless `SHINY_NETWORK=ON`. `src/net_lua.c` exposes this transport as VM-owned session userdata; it shares the script mutation guard and closes sockets on collection or VM teardown. Connection IDs are endpoint-local and distinct from entity IDs. Game protocols, authority, input validation, and snapshot smoothing belong in game scripts; `examples/duet` demonstrates these choices. See [networking.md](networking.md).

The headless host's `--realtime` option uses a monotonic deadline to pace fixed ticks at 60 Hz. Ordinary headless execution remains unpaced. Network packet arrival is external input and is not captured by the input replay or state hash; reproducibility claims apply to simulations without live network input.

Keep game-specific rules in the game directory. Add a core primitive only for behavior that should be shared across projects. A new public function requires validation in `src/script.c`, the same function's entry in its registration metadata, Lua annotations in `docs/api.lua`, and a behavior-focused test. Preserve the headless build and provide explicit diagnostics for unsupported behavior. Avoid introducing graphics headers into `shiny/core.h` or `shiny/script.h`.
