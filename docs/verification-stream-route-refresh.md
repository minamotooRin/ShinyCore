# Streamed-route refresh verification

On Windows, the focused headless native-host run used `build/full/shiny.exe`:

- `python tests/nav_route.py build/full/shiny.exe` baked Wayfarer's real map,
  compiled its static graph, and exercised dynamic portal/component changes with
  `sc.navigation.mask`. It checked negative chunk coordinates, duplicate and
  incomplete batch rejection without graph mutation, revision changes, return
  to the static baseline, and the clearance halo gate.
- `Streaming.test_stream_world_map_edits` in `tests/stream_integration.py` used
  `World.patch`, a disk-backed chunk save, unload and reload. The live graph
  reflected the blocked cell; a newly created graph recovered the saved edit
  after publication; clearing the tile restored the route.

Both checks passed. This is headless gameplay verification, not a visual or
hardware acceptance run. Unloaded chunks whose saved edits have never been
published still use the static bake. For a radius wider than half a navigation
cell, edge chunks wait for a loaded halo before their dynamic summary can be
refreshed. Callers must validate each active leg with `World.path` and retain
the stream loading boundary.
