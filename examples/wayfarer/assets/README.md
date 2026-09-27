# Asset provenance

forest.json and tiles.png are original ShinyCore map/tile assets. keeper.png and
chime.wav are unchanged original Workshop assets; wisp.png is from Lantern.
These assets use the repository's MIT license.

theme.ogg is an original 20-second stereo forest melody authored for Wayfarer.
Rebuild it with `python tools/build_sample_music.py wayfarer`;
the offline generator uses Python's standard library and ffmpeg's Vorbis encoder.
The shipped Ogg file needs neither tool at runtime and uses the repository MIT license.

Source Han Sans SC has its separate SIL OFL license; see FONT.md and OFL.txt.
All runtime resources are bundled locally.

landmarks-v1.png is a new transparent prop atlas generated for this project with
the built-in imagegen tool on 2026-09-27, distributed with the repository MIT
license. It contains an herbalist shelter, trail sign, moon stone and fallen log;
scenery.lua records their inspected source rectangles. The generated 1254x1254 RGBA
file is copied unchanged, with no runtime generation/download requirement. It adds
about 1.21 MiB. Original tiles and sprites remain unchanged.
