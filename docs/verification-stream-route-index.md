# Saved streamed-route index verification

Focused Windows headless checks used `build/full/shiny.exe`:

- `tests/nav_route.py` exercised route invalidation on a three-block graph. An
  unknown middle block prevents an apparently valid coarse route and returns its
  block coordinates; native-mask confirmation restores routing. Duplicate input
  leaves the graph unchanged.
- `Streaming.test_stream_world_map_edits` saved a real map patch, unloaded the
  chunk, restored it, and ran a second process against the same disk slot. The
  second process began two blocks away: its fresh graph reported the edited
  destination as `unverified`, then confirmed the saved tile state after loading
  that chunk. The test also checked a graph left stale across an edit and a
  repeated edit of the same block before unload, plus rejection of a populated
  legacy slot without the route index.
- `tests/test_sdk.py` checked the revised project-local Lua module version and
  manifest behavior.

These checks passed. The index is opt-in and records only edited block names;
unknown routes still require prefetch and local `World.path` checks. Existing
save slots without an index are explicitly rejected when `route_index=true`.
No visual behavior or physical device path changed in this work.
