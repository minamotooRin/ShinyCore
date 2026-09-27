# Crossing

Run `shiny examples/crossing`. Enter starts; A/D moves, Space jumps, E uses a switch,
R returns to camp, F6 saves and F9 loads. Escape opens the pause/settings menu.
Arrow keys also move. With a controller, D-pad or the left stick moves, south jumps/confirms, west
uses a switch, north returns to camp and Start opens/resumes the pause menu.
Button names describe their physical position, independent of printed A/B/X/Y symbols.
Use D-pad or shoulder buttons to navigate menus; east returns from settings.
SAVE/LOAD are available in the pause menu. Buttons also accept keyboard and mouse.

`controls.lua` defines named actions, using the optional `settings.bindings.crossing`
profile. `Input.bind` and `Input.save` can persist a customized profile independently
of campaign saves. SETTINGS → CONTROLS also edits these bindings with draft/apply,
cancel, defaults and conflict checks; see ../../docs/rebinding.md. Prompts use the bound
keys/buttons and show controller bindings while the selected controller is connected.
Left-stick displacement scales movement speed after the native deadzone; D-pad remains
full speed. The walk animation follows movement speed. Rebinding can replace either
source in the named action profile.
Menu transitions consume gameplay input, including held actions until their release.

The HUD shows the room, five collected-light markers and mechanism power. The bottom
objective updates with progress; blue markers point toward the next light, ferry,
mechanism or exit, including targets outside the camera. Nearby switches have amber
interaction prompts. Mechanism/save/rescue messages expire after four active seconds.
Signal progress survives loading and the next target remains visible beside a nearby
switch prompt. `guide.lua` reads campaign data without changing puzzle or save rules.

Three distinct rooms share `game.lua` and plain authored data in `levels.lua`:

- **Aqueduct:** collect two lights, use the lever, cross the gap on the moving ferry.
- **Old Mill:** push the crate onto the amber plate and climb the physical ramp.
- **Signal Tower:** operate WEST, EAST, CENTER in that order; a wrong switch resets
  the sequence (WEST starts a fresh attempt).

Each room uses its own row of the bundled dusk panorama atlas: aqueduct, forest
watermill and signal-tower ruins. Backgrounds use one image command and an explicit
background layer; all playable geometry stays visually distinct in the foreground.

Each exit requires five lights and a powered gate. Jump for lights above ledges.
Falling into the water returns to camp while keeping collected lights and powered gates. Finishing
the third room saves the ending; NEW JOURNEY clears campaign progress and returns
to the title. Escape cannot bypass the title or ending.

The checkpoint reconstructs the current room from explicit campaign data: collected
object persistent IDs, gates/switch sequence, player/crate positions, motion phase and
death counts. Native handles are never persisted. Save data version is 2.
The original Workshop keeper, chime and music are bundled locally; see assets/README.md.
The theme is acquired with `sc.audio.music` and keeps one application-owned voice
through all rooms and in-process loads. No voice handle is stored in campaign saves.
Fresh launches start playback anew; audible device continuity still needs acceptance.
Five original short WAV cues now mark jump, landing, switch, failed interaction and
rescue. The first room contact is silent; landing plays only after actual airtime.
These cues are cosmetic and do not advance campaign randomness. Regenerate them with
`python tools/build_crossing_audio.py examples/crossing/assets`.

```powershell
.\build\full\shiny.exe examples/crossing --check-all
.\build\full\shiny.exe examples/crossing --headless --frames 162 --replay examples/crossing/checkpoint.jsonl --save-dir build/crossing-saves
.\build\full\shiny.exe examples/crossing --headless --frames 2876 --replay examples/crossing/walkthrough.jsonl --save-dir build/crossing-walkthrough-saves
.\build\full\shiny.exe examples/crossing --headless --frames 2876 --replay examples/crossing/gamepad.jsonl
python tests/crossing_analog.py build/full/shiny.exe
python tests/crossing_integration.py build/full/shiny.exe
```

The checkpoint replay walks to the first light and saves. The walkthrough uses only
keyboard input to collect all 15 lights, ride the ferry, push the plate crate, climb
the ramp and finish the switch sequence across all three rooms. It automatically
saves the ending. From the title choose LOAD, then NEW JOURNEY to restart.
`gamepad.jsonl` traverses the same route entirely with controller button snapshots;
the focused test compares its final campaign state with the keyboard result and checks
controller rescue after a disconnect/reconnect. This does not replace physical device
or hot-plug acceptance. The controller HUD and normal controller LOAD flow were inspected
in a short hidden native run; see ../../docs/verification-crossing-controls.md.
`analog.jsonl` demonstrates partial left-stick travel, reversal and disconnect.

The focused test checks actual support contacts on the ferry/ramp, plate occupancy,
all-room completion without deaths, fresh-process checkpoint/ending recovery and
restart. It also checks falling into water retains collected lights. No gameplay
state or entity positions are injected into the walkthrough.

All declared rooms passed content checks. The optimized replay takes 2,876 fixed
frames (about 48 simulated seconds); this does not establish the target 5–10 minute
human playtime. The native ending has been inspected with totals for collected
lights, crossings, elapsed time and rescues; the new-journey button is focused.
Full art/audio acceptance and final packages remain outstanding. Headless checks
above do not open a window; hidden native ending capture is available through
`tools/capture_samples.py --case crossing-ending` with the engine/output arguments.
The added cue integration, unchanged walkthrough state and an inspected native
aqueduct capture are recorded in ../../docs/verification-crossing-audio.md.

For the new guidance views, use `--case crossing-mill` and `--case crossing-signal`.
These cases reach the room headlessly using the walkthrough and F6, then capture ten
native frames after the normal LOAD action. They show restored gameplay, not an exact
continuation of transient physics or notification state. Reviewed evidence is recorded
in ../../docs/verification-crossing-guide.md.
`--case crossing-aqueduct` also uses the short checkpoint-restore capture workflow.
The three backgrounds have been reviewed in ../../docs/verification-crossing-scenery.md.

角色表现由 `presentation.lua` 与本地 `shiny.animation` 推进：待机、行走、上升、
下落使用独立时钟，停步保留朝向，暂停冻结，回营重置。复用 keeper 原创帧，
尚未制作专门的腾空姿态。机关的空心/实心菱形和勾号分别表示未就绪、待操作、
已完成；信号灯顺序与 campaign 的同一份规则数据对应。表现不改变碰撞与存档规则。
渡台、固定踏板、箱子、开关和门现在由 `presentation.lua` 绘制少量像素细节；
地面前缘和渡槽水纹使用固定关卡坐标与模拟时间，不调用玩法随机数。
三关隐藏原生画面已目视检查，记录见 ../../docs/verification-crossing-presentation.md。
