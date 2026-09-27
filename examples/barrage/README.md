# Barrage

Defend the beacon through six 50-second waves, then defeat the remaining enemies.
The sixth wave includes a guardian. A successful challenge takes at least five
minutes of active simulation; pause and upgrade menus stop gameplay.

- Enter starts. WASD moves; diagonal movement is normalized.
- Firing is automatic. Arrow keys override the nearest-target aim.
- Left Shift dashes while moving, with a two-second cooldown and brief immunity.
- Escape opens pause/settings. Menus support keyboard, mouse and gamepad.
- Between waves, choose one of three upgrades with Tab/Enter or the mouse.
  Choices improve damage, firing rate, spread, movement or health. Every twelfth
  defeat repairs one heart. Enemy shots are pink; keeper shots are amber.
- Ranged enemies show four converging pink markers during their final half-second
  before firing; the amber dot points toward their current aim. Damaged enemies
  show remaining health. Amber numbers show hit damage, pink `-1` and a brief
  border indicate player damage, and green `+1` indicates an actual repair.
- The HUD shows individual health segments (pink at two or fewer), guardian health
  and a dash recharge bar. These indicators freeze with gameplay when paused.
- NEW CHALLENGE returns to a fresh title screen after victory or defeat.

Controllers use the left stick or D-pad to move, right stick to override auto aim,
south to dash and Start to pause. Partial stick travel gives partial movement speed;
diagonals are capped at normal speed. Use D-pad/shoulders and south in menus, east to
leave settings. After a menu consumes a held stick/button, release it before gameplay
can receive it. The same rule prevents upgrade confirmation from triggering a dash.
`controls.lua` declares named actions; `Input.bind`/`Input.save` can customize the
independent `barrage` settings profile. SETTINGS → CONTROLS edits keys/mouse, pad
buttons or directional axes as a draft; APPLY saves, BACK discards, DEFAULTS restores
the authored defaults. Conflicts are reported and menu bindings cannot be cleared.
Release controls before capture; Escape or pad Back cancels capture. See
../../docs/rebinding.md. The dash HUD shows the current device's actual binding.

`main.lua` owns the scene, enemy behaviors, native projectile batches, particle
emitters and UI. `presentation.lua` owns bounded transient feedback and read-only
combat indicators; it consumes no random numbers and keeps no saved state.
`sound.lua` provides bounded per-cue cooldowns and stereo position, using twelve
sound voices. Shooting/hits have lower priority than injury and outcome cues;
voice rejection is cosmetic. Wave/upgrade/outcome cues use the UI bus, combat uses
SFX, and its original 15-second theme uses persistent music through result loads/restarts.
Nine original short WAVs are rebuildable with `tools/build_barrage_audio.py`; they
add about 142 KiB. Rebuild the theme with `python tools/build_sample_music.py barrage`.
No audio state is saved or fed into challenge randomness.
`challenge.lua` holds plain progression data and the explicit
challenge seed (260926). Restarting reproduces its spawn/upgrade random sequence;
input and upgrade choices determine the result. Particle randomness is separate.
The engine's `--seed` does not override this sample's explicit challenge seed.

Challenge results are saved to `last_result` (data version 3). LAST RESULT on the
title restores the victory/defeat score screen, including time and challenge seed.
NEW CHALLENGE always resets gameplay. This is a result record, not a mid-challenge
resume; no enemy, projectile or runtime handle is persisted. Settings are separate.
Sprite/audio assets are bundled locally with provenance in assets/README.md.
Five enemy kinds now have distinct four-frame pixel sprites at their native sizes;
the guardian's broad crown and core stand apart from the small fliers. The original
wisp atlas remains in use for projectiles. `tools/build_barrage_sprites.py` rebuilds
the enemy images offline; gameplay rules and the fixed challenge seed do not depend
on sprite animation.
The focused native review and its limits are recorded in
../../docs/verification-barrage-sprites.md.
The arena uses a dark stone-court background with a recessed beacon seal; its
low-contrast floor stays below characters, projectiles and attack warnings.
The seal is passable decoration, not a separate damageable objective.

```powershell
.\build\full\shiny.exe examples/barrage --check-all
.\build\full\shiny.exe examples/barrage --headless --frames 18138 --replay examples/barrage/challenge.jsonl
.\build\full\shiny.exe examples/barrage --headless --frames 18138 --replay examples/barrage/gamepad.jsonl
python tests/barrage_integration.py build/full/shiny.exe
python tools/scenario.py build/full/shiny.exe examples/barrage/walkthrough.scenario.json
python tools/capture_samples.py build/full/shiny.exe --output build/barrage-endings --case barrage-ending --case barrage-defeat
```

The keyboard-only replay completes all six waves in about 302 active seconds,
with five upgrades, 317 defeated enemies and the guardian. The focused headless
check compares pause/no-pause runs with the same neutral resume frame,
starts a fresh challenge and exercises real defeat. It does not inject game state,
reduce wave durations or replace native projectile hits.
`gamepad.jsonl` completes the same challenge entirely with controller snapshots,
including quick-tap menu confirmations released before the next dash. Its complete
result matches the original keyboard route. Targeted checks also verify partial
stick movement and resume isolation. See ../../docs/verification-analog-actions.md;
physical controllers and hot-plug behavior remain unverified by this replay.

Hidden native victory and defeat screens have been inspected: titles distinguish
the outcome, statistics and buttons are readable, and no text overlaps. The result
restore/restart checks pass. Full art/audio acceptance, hands-on difficulty/usability
and portable final packages remain outstanding. This gameplay is separate from the
20,000-projectile performance workload in benchmarks/barrage.

Combat feedback was checked against the unchanged six-wave result and in hidden
native gameplay/preview captures; see ../../docs/verification-barrage-feedback.md.
The preview arranges representative visual states, so it is not evidence of gameplay
completion. The gameplay comparison uses the ordinary unmodified challenge replay.
The new arena was checked with the same native battle/feedback states and matching
gameplay fields; see ../../docs/verification-barrage-arena.md.

Combat audio integration preserves the complete six-wave gameplay result. Cue
cooldowns, native voice priority/capacity rejection, PCM validation and restart
music continuity were checked headlessly. Real-device playback and subjective
mix quality remain unaccepted; see ../../docs/verification-barrage-audio.md.

Sprite timelines now use the local shiny.animation module: idle/walk and a .2s
dash sequence for the keeper, independent clocks and kind-specific rates for enemies.
Original atlas art is retained; frames 0..3 are right-facing and flip_x controls
direction. A 600-frame challenge comparison preserves gameplay fields, and native
dash/pose captures have been reviewed. See ../../docs/animation.md; this is not a
new full six-wave or final animation-quality acceptance run.
