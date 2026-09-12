# Lantern / The Quiet Below

An original, compact exploration game built from two Lua scene files and two
tiny sprite atlases. Guide a hooded keeper through a dark cavern, collect three
wandering lights, and wake the archive door. The sample uses no borrowed game
artwork and needs no network service.

From the repository root, after building ShinyCore:

```sh
./build/shiny examples/lantern
```

- Move: **A / D** or **Left / Right**.
- Jump: **Space**. Hold for a higher jump; release for a shorter hop.
- Interact: **E**, near the door after collecting all three lights.
- In the archive, **E** near the left doorway begins a fresh run.

The raised lights need a jump. Both platforms can be jumped through from below.
The controller includes a 100 ms grace period after leaving a ledge and a
120 ms jump input buffer. Movement and collection run at a fixed 60 Hz.

## Explore the code

`main.lua` owns the cavern, controller, three lights, and the gate. Its returned
table contains the scene configuration plus `init`, `update`, and `draw`.
`rooms/archive.lua` is a second complete scene. Scene transitions replace the
world, so returning deliberately resets collected lights; there is no hidden
save state.

`assets/keeper.png` contains eight 12 × 18 frames. `assets/wisp.png` contains four
8 × 10 frames. Recreate both original, hand-authored pixel designs with:

```sh
python3 examples/lantern/assets/generate.py
```

## Reproduce a run

```sh
./build/shiny --check examples/lantern
./build/shiny --headless examples/lantern --frames 430 \
  --replay examples/lantern/replays/tour.txt --snapshot /tmp/lantern-archive.json
./build/shiny --headless examples/lantern --frames 480 \
  --replay examples/lantern/replays/tour.txt --snapshot /tmp/lantern-return.json
```

The recording uses global frame numbers, continuing across scene changes. The
430-frame endpoint is in the archive; the 480-frame endpoint returns to a fresh
cavern. The CLI integration test verifies both endpoints and the collection of
each light along the way. Run with the same seed when comparing snapshots.
