# C11 reference verification

This is the preserved C11 implementation, rebuilt from this task's pre-migration source and patch history. It is compiled as C11, with C++ migration kept on a separate branch.

Verified on macOS arm64, AppleClang 21, Release configuration, 2026-09-12:

- CMake native build with optional networking enabled: all 8 CTest groups passed.
- Core: 40,696 explicit checks, including high-speed collision and generation invalidation.
- Lua: schema, stale handles, allocation limits, instruction budget, draw guards.
- Networking: native loopback, Lua lifetime, two-process authored example.
- CLI: Lantern collection/room tour, deterministic seed replay, diagnostics.
- Tools: new-project workflow and relocated package launch; real engine tests ran without skips.
- Native Lantern rendered and reviewed in the original build; packaged C11 app remains preserved outside Git.

Build artifacts and generated output are excluded from Git. Branch `release/c11` and annotated tag `v0.1.0-c11` identify this reference. Use a separate worktree and a separate build directory when comparing with C++23.

The branch includes a documentation-only correction after the original tag: two
comments accidentally described the later C++23 ownership model. The C11 sources,
tests, and build settings are unchanged; `v0.1.0-c11` remains at the original commit.
