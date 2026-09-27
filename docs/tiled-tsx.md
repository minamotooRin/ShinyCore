# External Tiled TSX tilesets

The offline resource builder accepts a JSON orthogonal map whose `tilesets` entry
uses `source` to name a project-local `.tsx` file. It normalizes the TSX into the
same readable format-3 chunk index used by external JSON tilesets. No XML parser
is linked into the game binary. See the [runnable example](../examples/tsx_import/README.md).

Supported fields are atlas and image-collection PNG paths, grid dimensions,
margin/spacing, tile offset, sparse tile IDs, tile animation frames with millisecond
durations, simple typed properties and rectangle, ellipse or polygon tile collision
objects. File properties become project-relative paths and dependencies. Images,
TSX files and effective file properties all enter the content hash; bad references,
duplicate tile/property IDs, malformed XML, dimension mismatches and invalid
collision geometry fail before publication with source context.
The package dependency closure also retains effective tileset, tile and layer
`file` properties; it rejects missing or outside-project targets.

This importer reads **JSON maps plus external TSX tilesets**. It does not parse TMX
maps. Both JSON and XML `.tx` object templates may refer to a TSX tileset;
see [object templates](tiled-templates.md). Embedded images, color-key transparency, nonorthogonal tile grids,
custom tile image subrectangles, grid-size tile rendering and aspect-fit are
explicitly rejected. DTD/entity declarations are rejected, and a TSX is limited
to 4 MiB. A game's runtime needs only the built index/chunks and referenced PNGs.
The builder cache key is now version 16; the runtime chunk index remains format 3.
Tile objects keep an authored draw index, layer draw order and validated tileset
alignment. `shiny.stream_tiles` draws their atlas or collection image automatically,
including rotation and GID flips. The game still owns non-GID objects and their
behavior; a tile image alone does not create a physics body.

This normalization follows Tiled's [TMX/TSX reference](https://doc.mapeditor.org/en/stable/reference/tmx-map-format/)
and [JSON map reference](https://doc.mapeditor.org/en/stable/reference/json-map-format/).
It does not claim the example source was exported from the Tiled editor.

Focused verification on 2026-09-27: atlas and sparse collection TSX inputs,
concave collision decomposition, animation, JSON template GID remapping, file
dependencies, deterministic rebuild, cache invalidation and malformed input passed
four targeted asset tests. The self-contained example passed `--check-all` and a
30-frame headless run with a published chunk and advancing animation. Its hidden,
muted native capture at `build/tsx-import-reviewed/tsx-scene.png` was viewed:
both tilesets, the animated atlas tile, transparency, labels and layout rendered.
The runtime-only package closure was copied to another path without source TSX or
map JSON; it loaded the same chunk headlessly. Two focused packaging checks cover
file properties and broken references.
The capture does not prove all Tiled XML variations or full game acceptance.

Object-layer follow-up: the rebuilt TSX example now contains the templated flower
and a second rotated tile object, both drawn by `shiny.stream_tiles`. Targeted
import and runtime tests cover alignment metadata, draw order, rotation and a
deleted object's visibility. A hidden, muted native capture was inspected at
`build/tsx-tileobjects-reviewed/tsx-scene.png`; graphical and headless 30-frame
replays agreed on the explicit gameplay fields. Full Tiled-editor export and
platform coverage remain unverified.
