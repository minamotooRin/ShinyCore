# Asset provenance

keeper.png, chime.wav and theme.ogg are unchanged copies of the original ShinyCore
Workshop assets in examples/workshop/assets. They are covered by the repository's
MIT license. Room geometry, palettes and level data are authored in levels.lua/game.lua.
There are no runtime downloads or external asset paths.

scenery-v1.png is new artwork generated for this project on 2026-09-27 using the
built-in image generation tool, without reference images. It preserves the existing
keeper and audio assets. Brief: one dusk pixel-art panorama atlas with three rows:
ruined blue aqueduct and river; sage/ochre forest watermill; purple signal-tower ruins.
No characters, UI, text or playable foreground geometry. Keep distant scenery muted
so the real platforms, player and mechanisms remain identifiable.

The delivered PNG is 1672 x 941, RGBA, 2,438,040 bytes. Runtime regions are x=0,
y=0/314/628, w=1672, h=313, excluding row-edge pixels. game.lua draws the current row
with a subdued tint at layer -100, inside the side walls and above the ground.
The PNG is shipped locally and listed in package.json; no image-generation service
or Python is needed to run the game. Native review: docs/verification-crossing-scenery.md
in the engine repository.
