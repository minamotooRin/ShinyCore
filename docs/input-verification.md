# Device input verification — 2026-09-13/14

Implementation lives in the isolated `feature/device-input` worktree. Its parent
`dd16684` is a snapshot of the concurrently developed, uncommitted 0.2 engine,
not an input-feature commit. Integrate only the subsequent input feature commit
onto the completed 0.2 work; do not replay that snapshot over the other task.
The original `ShinyCore` checkout and its build directories were not used for
this feature's builds/tests after isolation.

## Results

Windows x64, GCC/MinGW 16.1.0, Release, network OFF:

| Check | Result |
| --- | --- |
| Native graphics build (`build-input`) | Passed |
| Graphics OFF build (`build-input-headless`) | Passed |
| `input`, `core`, `script`, `input_integration`, `integration`, `tools`, `realtime` | All seven CTest targets passed in both builds |
| New input integration suite | Eight cases passed, including all 105 keys and 17 buttons, six axes, invalid arguments/replays, tap edges, replay determinism, and scene continuity |
| Input unit tests | Passed buffering, repeat suppression, overlapping device sources, disconnect release, slot selection/acknowledgment, normalization and deadzones |
| Existing 0.2 `features` suite | 20/21 cases passed; pre-existing documentation completeness failure described below |
| Lantern legacy replay, 430 ticks | Passed; reached `rooms/archive.lua` |
| Native versus headless Input Lab replay, 30 ticks | Complete JSON outputs identical; hash `e5b7e135fc23094c` |
| Native `--capture` after disabling raylib F12 shortcuts | Passed; inspected Input Lab image and readable keyboard/button/axis display |
| Explicit Windows native-message smoke test | Passed default key delivery, F12 without implicit screenshot, focus-loss release, debug reload/pause/single-step/exit, and replay isolation from native keyboard messages |

The native test sends messages only to the HWND owned by its child process. It
does not use global keyboard injection or move another application's focus.
It verifies the native callback/sampling/host path, not physical keyboard wiring.
The gamepad sampler did not report a connected device during these runs;
physical gamepad buttons, axes and unplug/replug remain **unverified**.

Full `ctest --output-on-failure` reports **7/8 targets passing**, not a clean full
suite. The remaining target is `features`, whose
`test_api_metadata_matches_annotations` fails because `sc.measure` is registered
but lacks a declaration in the captured baseline's `docs/api.lua`. This was
confirmed against `dd16684`'s source and docs, independently of the new input
API. The input suite separately checks that all new functions and control names
are present in metadata/annotations. Unrelated 0.2 documentation was left to the
concurrent engine task.

## Environment limits

- The sanitizer configure probe with `SHINY_SANITIZERS=ON` failed: this MinGW
  installation lacks ASan/UBSan runtimes. No WSL Linux environment was installed.
  Sanitizer execution remains outstanding; the existing Linux sanitizer CI
  includes the added CTest input targets when run.
- Dependency download initially failed certificate validation. Builds used
  independent local copies of the already fetched pinned dependency sources,
  configured with `FETCHCONTENT_SOURCE_DIR_*`. No fetched dependency was edited,
  and the concurrent task's source/build cache was not used as a build output.
- No claims of physical-controller compatibility, other-platform validation,
  automatic recording, or unchanged hashes across engine versions are made.

## Reproduce

```sh
cmake -S . -B build-input -DSHINY_GRAPHICS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-input --parallel 2
ctest --test-dir build-input --output-on-failure
python tests/native_input_windows.py build-input/shiny.exe
build-input/shiny.exe examples/input --mute --frames 30 --replay examples/input/demo.jsonl --capture input.png

cmake -S . -B build-input-headless -DSHINY_GRAPHICS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-input-headless --parallel 2
ctest --test-dir build-input-headless --output-on-failure
build-input-headless/shiny.exe --headless examples/lantern --frames 430 --replay examples/lantern/replays/tour.txt
```

Local evidence is under the ignored `artifacts/input/` directory:
`showcase.png`, `showcase-state.json`, `lantern-430.json`, and
`native-windows.json`. Build-specific CTest logs are under each build directory's
`Testing/Temporary/`.
