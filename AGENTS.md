# Working on ShinyCore

Read `README.md`, `docs/architecture.md`, and `docs/api.lua` before changing contracts. The user-facing goal is a small, clear native 2D engine that LLMs can program and verify through text.

- Engine core: `src/core/core.cpp` and `src/physics/physics.cpp`, public contract `include/shiny/core.h`; one owned Box2D world, no graphics or Lua dependencies.
- Script boundary: `src/script/script.cpp`, contract `include/shiny/script.h`; validate all authored input and synchronize Lua annotations plus API metadata.
- Optional network: `src/net/net.cpp` / `include/shiny/net.h` own ENet transport; `src/script/net_lua.cpp` owns Lua sessions. Default OFF; no socket dependencies in core. Keep game protocols in `examples/duet` or the owning game. Validate both ON and OFF builds, actual two-process traffic, and native resource cleanup. Never let Lua longjmp skip a C++ destructor, or let a native exception unwind through Lua.
- Host and backend: `src/runtime/main.cpp`, `src/render/render.cpp`. Platform effects belong here; simulation behavior must work headlessly.
- Authored games: `examples/workshop/` demonstrates 0.2; `examples/lantern/` retains the original tour. Keep controllers and animation in Lua, preserve original assets.
- Tests use explicit checks that run under `NDEBUG`. Do not replace them with disabled Release assertions.
- Do not edit fetched dependencies in `build*/_deps`; versions and checksums live in CMake.
- No hidden setup, generated editor scenes, credentials, network calls, or model-provider integration are required to author a game.

Engine code uses C++23; third-party dependencies retain their C builds. Use a
compiler and standard library that implement `std::expected`. Preserve explicit
ownership: fixed simulation storage is allocated once from project capacities, runtime ownership is
exclusive, native resources release through RAII, and expected failures return
data for CLI diagnostics. Do not copy or move a runtime or script whose address
is borrowed by Lua. Give every new abstraction a concrete owner and purpose.

Lua's C error path uses `longjmp`. No C++ object with a nontrivial destructor may
remain live in a callback frame across a potentially raising Lua call. Keep
owning standard containers and strings outside that boundary, and never allow a
C++ exception to cross a C callback. Resource destructors must not throw. Preserve
stable entity iteration, input replay ordering, and logical-field state hashing.

The C11 source baseline is tagged `v0.1.0-c11`; `release/c11` adds documentation
corrections only. C++23 lives on `main`, with `feature/cpp23` and `v0.1.0-cpp23`
preserving the migration delivery. Inspect `git worktree list` before adding a comparison
checkout, and never overwrite an existing checkout or reuse its build directory
for a different implementation. Those tags preserve 0.1; current development is
the complete-edition development branch with Box2D and explicit checkpoint state. Do not reuse 0.1 size or behavior
claims as 0.2 evidence; physics trajectories may change.

Normal verification:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/shiny --headless examples/lantern --frames 430 --replay examples/lantern/replays/tour.txt
```

Use `-DSHINY_GRAPHICS=OFF` for a machine without graphics development dependencies. For changes affecting visuals, additionally capture and inspect actual native output with `--frames N --capture /absolute/path.png`; a successful headless test does not validate pixels. For changes to native ownership, pointer lifetimes, or the C/Lua boundary, run a headless sanitizer build using `-DSHINY_SANITIZERS=ON`.

On Windows, use LLVM-MinGW for ASan/UBSan in a separate build directory; keep its
`bin` on PATH for the sanitizer DLL and symbolizer. Set `ASAN_OPTIONS=halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. See
`docs/sanitizer-verification.md` for the verified 22.1.8 workflow. Missing GCC
sanitizer libraries do not imply that Windows needs WSL for these checks.

Do not claim platform compatibility, saved-state restoration, Lua-local state hashing, or arbitrary graphics feature coverage beyond what was validated. With --debug-keys, F5 resets the current room but inherits explicit sc.state. Checkpoints restore authored data and rebuild the room, not VM/solver state. Keep docs synchronized and record material limits honestly. Missing local sanitizer runtimes must be reported; maintain the Linux sanitizer CI job.
