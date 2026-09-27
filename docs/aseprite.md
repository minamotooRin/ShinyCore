# Aseprite animation imports

Export a PNG sheet and JSON metadata, then declare the JSON relative to the asset
manifest. Both files must remain inside the project:

```json
{"animations":{"hero":"assets/hero.json"}}
```

Run `python tools/assets.py PROJECT/assets.build.json build/content`. The reported
directory contains `animation-hero.png`, `animations.lua` and a readable `index.json`.
Copy the image and Lua catalog into the project, declare the image in `project.lua`,
and use `require("content.animations").hero.clips` with `shiny.animation`. The
[runnable example](../examples/animation_import/README.md) shows the complete path.
Python/Pillow are build-time dependencies only.

The importer accepts array or hash frames in export order. It restores trimmed
pixels at `spriteSourceSize` offsets onto a shared `sourceSize` canvas, preserving
alpha, then writes a regular native sprite grid. Frame durations convert from
integer milliseconds to seconds; frame indices are zero-based. Names are explicit
data, including when Lua quoting is required.

Tags support forward, reverse, pingpong and pingpong_reverse. Ping-pong cycles do
not duplicate endpoints. Missing/zero repeat loops; a positive repeat count expands
a finite clip. Without tags, `all` loops through every frame. Bounds are 65,536
source frames and expanded clip entries, 256 tags, 128 UTF-8 bytes per name,
1–65,535 ms per frame, and 8,192 pixels per input/output dimension. Invalid bounds,
duplicate names/JSON keys, inconsistent canvases or missing references fail before
cache publication, with source and field diagnostics.

The contract covers flattened sprite animation, not editor projects, layers,
slices, embedded user data or rotated frames from other packers. See the
[official export CLI](https://www.aseprite.org/docs/cli/) and
[exporter source](https://github.com/aseprite/aseprite/blob/main/src/app/doc_exporter.cpp).
The shared builder's cache format 12 includes JSON, PNG, manifest options and tool version in its key;
generated output can be rebuilt without the cache.

Verification on 2026-09-27: focused importer/atlas tests covered trimmed offsets,
alpha, tags, malformed input, dependencies and reproducible output. The sample
passed content validation, dependency closure and SDK dev.46 audit. Headless and
hidden/muted native 60-frame runs agreed on observed animation state; the native
`build/animation-import-reviewed/imported-clips.png` was inspected for sprite and
label placement. The sample metadata is a hand-authored format fixture over the
original Workshop art; an actual Aseprite editor export was not exercised.
