# Wayfarer

Explore a streamed forest, collect 24 moon herbs, clear the eastern road and return
medicine to the village healer. Requires an engine built with streaming.

Enter starts a new game if no checkpoint exists, or restores the complete existing
checkpoint. The title owns no streamed world. It probes the checkpoint on the
application IO worker; menu focus and Quit remain responsive, while selections wait
for the result.
NEW JOURNEY asks before clearing the
checkpoint, collected-object markers and map edits; cancelling preserves them.
Completing the quest saves the ending. Its RETURN TO TITLE button cannot resume a
completed quest as unfinished gameplay.
The title and world acquire one application-owned theme voice. Returning to the
title or beginning a new journey keeps its playback position; sound effects remain
room-local. The theme is an original 20-second loop built offline from
`tools/build_sample_music.py`; running or packaging the game does not need ffmpeg.

WASD moves, E talks/gathers or clears the stone at (328,128), I opens the journal,
F6 saves, and Escape opens pause/settings. A controller uses the left stick or D-pad
to move, West to interact, North for the journal, Back to save and Start for the
menu. SETTINGS / CONTROLS edits the independent `wayfarer` binding profile; prompts
follow its current bindings. Speak to the healer near (208,100) before
collecting the medicine. Trail boots move faster; field boots gather from farther
away. The journal starts focus at the name field; Tab visits items, equipment and
CLOSE. I is ordinary text while the name field is focused. North still closes the
journal; Escape (outside composition) or East closes the top dialogue/journal.
Consumed held actions stay blocked until release or stick neutral, including the
closing frame. Dialogue and the journal
pause simulation. The village courier uses budgeted native flow-field navigation
and waits outside loaded coverage or while menus are open. Traveler and courier use
independent idle/walk clocks and retain horizontal facing when stopping or moving
vertically. Gold corner marks show herbs inside the equipped boots' gathering reach;
collection shows a short count notice and up to eight floating pickup marks. These
visuals freeze during menus/loading and reset when the room is rebuilt.

The herbalist shelter, trail signs, moon stones and fallen logs are passable
landmarks drawn behind actors; authored terrain still defines collisions. A small
north-up locator shows the road, landmarks, player, healer and only currently
loaded herbs. Its four district names help orient exploration. The amber prompt
points to the healer before the quest, the nearest loaded herb while gathering,
the roadblock once 24 herbs are ready, then the healer for delivery. When no loaded
herb remains, explore another district; the guide does not reveal unloaded objects.
Prompts follow the configured interaction binding and hide behind menus/dialogue.
`scenery.lua` owns atlas placements; `guide.lua` reads quest and visible-world data.

`title.lua` owns save selection, `main.lua` the room/UI, `quest.lua` the rules and
`patrol.lua` the courier, `presentation.lua` the ephemeral feedback and
`controls.lua` the named action defaults. `shiny.stream_world` loads terrain, navigation and persistent
objects from 16 authored chunk files. Unprepared edges have loading barriers.
The offline index pins an object's anchor chunk when its authored geometry extends
into a neighboring interest chunk; the barrier still follows the player/camera
area. See ../../docs/stream-object-coverage.md.
Collected herbs use object persistent IDs and deletion markers; road edits and
active objects are saved with explicit player, name, equipment and quest state.
The road objective changes only after its streamed tile patch is published; a failed
or cancelled image preparation leaves the road uncleared and available to retry.
Save data version is 2; VM state and native handles are not persisted.

Entering chunk states and outgoing snapshots use the application IO worker. The
current map stays active until state reads, world/image preparation, saving and publication succeed;
gameplay pauses during this transaction, including initial loading. Failures show
stage-specific retry and return-to-menu controls, including image decode/upload errors. Returning keeps the current scene
and does not undo a disk commit. Room ui_update services recovery while fixed updates
wait. The room index is declared in `project.stream_indexes` and parsed on the content
worker before `init`; its metadata is ready when `shiny.stream_world` opens it.
Ordinary menus wait for world preparation; normal loading preserves held
movement for resumption, while the failure panel consumes gameplay actions. Forest tiles and herb sprites use stream=true; the world collects their image dependencies
and commits prepared textures with terrain/objects. Initial player/healer/courier sprites
remain eager resources. The stream watch includes pinned image count. The selected
checkpoint index stays pinned across title-to-world loading. Starting a new journey
still deletes the slot synchronously. For a bounded visual fault check, run
`python tests/native_wayfarer_save.py build/full/shiny.exe --output build/wayfarer-save-check`.

In the source checkout, map content is assets/forest.json, assets/tiles.png and assets.build.json.
Rebuild with `python tools/assets.py examples/wayfarer/assets.build.json build/wayfarer-assets`,
then copy the reported map-world output to maps/world. Runtime uses only local
files. package.json selects runtime scripts/resources and maps/world/index.json;
the packager includes every indexed chunk and omits the offline map source and unused
Lua modules. Original sprites/music/audio and the Source Han Sans SC font are bundled; see
assets/README.md, FONT.md and OFL.txt. The font is 15.68 MiB and supports on-demand
name glyphs; it is not linked into the engine.

```powershell
.\build\full\shiny.exe examples/wayfarer
.\build\full\shiny.exe examples/wayfarer --check-all
.\build\full\shiny.exe examples/wayfarer --headless --frames 3244 --replay examples/wayfarer/walkthrough.jsonl --save-dir build/wayfarer-walkthrough
.\build\full\shiny.exe examples/wayfarer --headless --frames 3244 --replay examples/wayfarer/gamepad.jsonl --save-dir build/wayfarer-gamepad
python tests/wayfarer_integration.py build/full/shiny.exe
python tests/wayfarer_route_commit.py build/full/shiny.exe
python tools/scenario.py build/full/shiny.exe examples/wayfarer/walkthrough.scenario.json
python tools/capture_samples.py build/full/shiny.exe --output build/wayfarer-gather-captures --case wayfarer-gather-ready --case wayfarer-gathered
```

Use a fresh save directory for the full walkthrough. It uses only real keyboard
input, gathers all 24 herbs, clears the road, revisits chunks and delivers the quest.
The focused check verifies all 16 authored chunk records, deletion/road state,
fresh-process ending recovery, cancelled/new journeys, Chinese names and equipment.
`journal.jsonl` is the shorter name/equipment checkpoint replay (46 frames), and
`travel.jsonl` checks crossing chunks and road edits (551 frames). `gamepad.jsonl`
completes the same 3244-frame mission with controller input; `controller-journal.jsonl`
checks journal toggling, name typing and held-stick isolation in 23 frames. The
integration test compares keyboard/controller quest and courier state, and checks
the same music voice and continuous loop clock across room changes. See
../../docs/verification-wayfarer-controls.md for the new controller visual check.

Actual hidden native captures have been inspected for title, dialogue, journal,
forest and the restored quest ending. The ending displays herb/road progress and
equipment; RETURN TO TITLE is focused. `capture_samples.py --case wayfarer-ending`
plays the quest headlessly, then captures its normal native checkpoint restore. Chinese UI is rasterized at its actual window pixel size, using
16 logical pixels for body text; it is no longer enlarged from the low-resolution
scene. The journal has an opaque panel. The optimized full route takes about 54 simulated
seconds; normal 5–10 minute exploration has not been established by a human session.
Audio-device playback, real IME input, final art, ending screenshots and portable
packages remain unaccepted. See ../../docs/verification-sample-visuals.md.
