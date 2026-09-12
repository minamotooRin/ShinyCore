# Programming ShinyCore with an LLM

Give the model the public API, a working scene, and a command that checks its
changes. Keep one behavior change small enough to verify with a deterministic
input recording.

ShinyCore's native implementation uses C++23; game rules use Lua scene tables
and `sc.*` functions. Most game tasks only need Lua and assets. A native engine
change requires a compiler and standard library with `std::expected`.

## A short working loop

From the repository root:

```sh
./build/shiny --api
./build/shiny --check examples/lantern
./build/shiny --headless examples/lantern --frames 430 \
  --replay examples/lantern/replays/tour.txt --snapshot /tmp/lantern.json
./build/shiny examples/lantern
```

The API command describes the Lua interface in JSON. The check command validates
the entry scene, runs initialization and an initial draw, and checks referenced
sprite paths; it does not prove that every
gameplay branch or every destination scene works. The headless run exercises
the same fixed-step update and collision code as the windowed game. Inspect
the JSON snapshot and process exit status, then inspect the rendered result
for visual changes. A successful headless run alone does not verify layout,
artwork, sound, or whether the game feels good to play.

An input recording contains `frame mask` pairs. Frames start at zero and remain
global across room transitions. Each mask stays held until the next entry:
left `1`, right `2`, up `4`, down `8`, jump `16`, action `32`. Add bits to hold
multiple actions. For example, `120 18` holds right and jump from frame 120;
`145 2` releases jump at frame 145 while continuing right.

## Keep game rules in Lua

A project has `main.lua`, optional additional scene files, and project-relative
assets. Each scene returns one table with optional `init()`, `update(dt)`, and
`draw(alpha)` callbacks. Use the Lantern scene as an executable schema example.
`#` map cells are solid, `=` cells are one-way platforms, and `.` cells are air.

- Put configuration and authored world data in the returned scene table.
- Create and locate entities in `init`; retain their IDs in local variables.
- Change velocity, inspect overlap, handle input, and request transitions in
  `update`. Its `dt` is always 1/60 second. Physics advances after the callback.
- Put only rendering commands and reads in `draw`. Do not modify game state or
  draw random numbers there. Rendering may run more or less often than updates.
- `sc.get(id)` returns a copy. Change an entity with `sc.set(id, {vx = 90})`;
  editing the returned table does not change the world.
- `grounded` describes the preceding physics step. Keep jump buffering and
  coyote time in the controller, as the sample does.
- `gravity = 1` on an entity uses scene gravity; `gravity = 0` disables it.
- IDs belong to a scene. After destroying an entity, stop using its ID.
  `sc.scene("rooms/archive.lua")` defers a complete scene replacement.
- Use `sc.random()` and an explicit CLI seed for reproducibility. Lua file I/O,
  native modules, `require`, and `dofile` are unavailable. Scenes are self-contained.
- Keep asset paths relative to the project root, including in nested scene files.

The core is intentionally small: axis-aligned bodies, a tile grid, sprite frame
rectangles, particles, generated tones, and a queued immediate drawing API.
Build gameplay abstractions when the game needs them; first look for an existing
primitive instead of adding engine systems for a single mechanic.

## A useful task prompt

> Work in `examples/lantern/main.lua`. Read `./build/shiny --api` and the current
> scene before changing code. Add a pressure plate that opens a nearby gate
> while the keeper overlaps it. Keep the existing movement controller and
> assets. Use `sc.get`, `sc.set`, and `sc.overlap`; do not add native APIs. Add an input
> recording that demonstrates the plate opening and closing. Run the scene
> check and a headless replay; inspect the snapshot. Report the changed files,
> observed behavior, and the checks you actually ran.

For a rendering change, add a windowed screenshot to that request. For a new
room, explicitly exercise the transition into and out of it. For randomness,
run the same recording and seed twice and compare results before claiming
determinism. Cross-platform bit-identical floating point is not a promise.

## When a native change is needed

This section requires a source checkout. A runnable package includes the Lua
examples and API guide, but does not include native sources or repository tools.

Read `AGENTS.md` and `docs/architecture.md`, then locate the owning `.cpp` module.
Keep fixed-step simulation in `src/core.cpp`, Lua validation in `src/script.cpp`,
and platform resources in `src/main.cpp` or `src/render.cpp`. Preserve bounded
world storage, stable iteration, and explicit error results. Use RAII for native
resources and exclusive ownership for a world plus its VM; their addresses must
remain stable while Lua borrows them.

The Lua C API is a special boundary: an error can `longjmp` past callback-local
destructors. Do not introduce an owning C++ object that stays alive across a
potentially raising Lua call. Put such work outside the protected callback or
finish its scope before reporting the Lua error. Update the Lua annotations and
API metadata when a public game contract changes. An ownership refactor should
preserve existing projects and replay behavior.

The C11 baseline remains at `v0.1.0-c11`; the README includes separate-worktree
commands and links to `tools/compare_engines.py`. Compare the same project,
seed, and recording before claiming equivalent behavior. Use matched build
options and actual measurements for performance or binary-size comparisons.
