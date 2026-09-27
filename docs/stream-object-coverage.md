# Streamed object coverage

The offline Tiled JSON builder keeps each object in exactly one anchor chunk. Its
format-3 index now adds sparse `object_coverage` entries for chunks intersected by
an object's authored shape outside that anchor. Each entry contains `x`, `y` and
an `anchors` array of chunk coordinates. Rectangles, tile objects, rotated shapes,
polygons and polylines use a conservative axis-aligned footprint. The builder
rejects non-finite dimensions/points and footprints spanning over 1024 chunks;
the index retains its 16 MiB bound. Builder cache version 14 changes no chunk
payload format.
`sc.stream.metadata()` exposes the bounded sparse table without copying the chunk
directory or object payloads; generated LuaLS contracts name `ScStreamMetadata`,
`ScObjectCoverage` and `ScChunkCoordinate`.

`shiny.stream_world` passes the index to `shiny.stream_regions`. A requested player
or camera area pins its intersecting object anchors before publication. The
returned `plan.enter` includes those extra chunks, so object creation, explicit
saved-state restore and later unload still use the ordinary chunk transaction.
`World.contains` and physical loading walls use only the requested area; an anchor
pin does not silently open terrain outside that area. Region capacity includes
both area chunks and required anchors, and failure leaves the prior pins intact.

This covers the imported static footprint. A game that moves an object beyond its
authored footprint must keep its anchor loaded or migrate ownership explicitly;
the index cannot predict arbitrary movement from Lua. Object factories must also
keep their collision geometry within the authored footprint when relying on this
automatic coverage.

Focused asset, region-boundary and world-publication tests pass in Release, with
the region and world cases also passing under headless ASan/UBSan. Wayfarer's
generated map has one cross-chunk herb at x=510; its 550-frame travel replay and an
inspected hidden native capture are in `build/object-coverage-reviewed/`.
