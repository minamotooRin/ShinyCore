# Tiled map and group properties

The offline JSON map builder retains the map's simple `properties` and each group
layer's `properties` in the readable format-3 index. `sc.stream.metadata()` returns
copies of `properties` and `groups` alongside flattened `layers`. A group's
`parent_group` and a leaf layer's `group` are one-based indices into `groups`;
the fields are absent for root items. Group entries retain their source name/path,
optional Tiled `id`, and their own properties. These properties are author data:
the engine does not implicitly apply them to child layers or run scripts from them.

`file` property values become project-relative paths and enter the content hash.
Packaging includes effective map, group, layer, tileset, tile and object file
references. Missing files or paths outside the project fail before publication.
The runtime needs only the built index/chunks and referenced files, never the
source Tiled JSON. The builder cache key was version 13 at this checkpoint; the
current cache is version 15 for streamed object collision. Chunk format stays 3.

The [TSX/TX example](../../examples/tsx_import/README.md) reads map and group file
properties through `sc.stream.metadata()`. Focused builder and package tests,
30 headless frames, and a viewed hidden native capture on 2026-09-27 verify this
path. The same 30 frames also passed in the full-feature headless ASan/UBSan build;
an 18-file dependency-closure copy completed the same headless replay. The capture
is `build/map-properties-reviewed/map-groups.png`. The example fixtures
were authored directly; no Tiled editor export or
full-platform acceptance is claimed.
