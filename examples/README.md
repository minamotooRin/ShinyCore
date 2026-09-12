# Examples

## Lantern / The Quiet Below

[`lantern`](lantern/) is a complete original exploration game: pixel sprites,
fixed-step platform movement, one-way platforms, collectible lights, procedural
audio, camera follow, particles, a HUD, and a transition between two rooms.

```sh
./build/shiny examples/lantern
./build/shiny --check examples/lantern
./build/shiny --headless examples/lantern --frames 430 \
  --replay examples/lantern/replays/tour.txt --snapshot /tmp/lantern.json
```

Start a new game by copying this directory and replacing its scene data and
rules. The runtime consumes the `.lua` files and `.png` assets directly; the
included Python sprite generator is optional development tooling.
