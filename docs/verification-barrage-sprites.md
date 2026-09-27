# Barrage enemy sprite review — 2026-09-27

Five enemy kinds now draw from distinct four-frame local pixel sprites. Native
dimensions match their existing 7, 8, 10, 15 and 24 unit bodies. The former
`wisp.png` remains in use for player projectile regions. The changed Lua fields
select an image and frame grid; health, speed, collisions, spawn order, challenge
seed and enemy animation rates are unchanged.

`tools/build_barrage_sprites.py` is the reproducible Pillow source for the five
small PNGs. Byte-for-byte rebuild, all 20 nonempty frame cells and expected grid
dimensions were checked. `examples/barrage --check-all`, a 180-frame headless
smoke replay, package dependency closure and the existing project SDK audit pass.
The headless run reports one active runner, two kills and four hits at frame 180.

Two captures came from the actual native renderer, using hidden, muted, bounded
runs and isolated save directories. Both were viewed:

- `build/barrage-sprites-reviewed/five-kinds.png`: all five kinds at native size
  and at twice their gameplay size; silhouettes, transparent backgrounds and
  labels are clear.
- `build/barrage-sprites-reviewed/battle.png`: the 180-frame sample run loads
  the new runner sprite in the arena without clipping or UI overlap.

This is focused visual and content validation. It does not establish real-device
feel, every combat state, five-minute play quality, hardware performance or the
final release package. The established full six-wave replay was not rerun for a
cosmetic resource change.
