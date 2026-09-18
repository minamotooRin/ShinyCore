# Keyboard and single-player gamepad input

The existing `sc.down/pressed/released` six-action API remains available. Direct
device queries let a game define any controls using ordinary Lua tables:

```lua
local controls = {attack="j", dodge="left_shift"}
-- Inside update:
if sc.key_pressed(controls.attack) or sc.gamepad_pressed("west") then
    -- Attack once.
end
local movement = sc.gamepad_axis("left_x")
```

All names are case-sensitive. The complete authoritative lists are the `keys`,
`gamepad_buttons`, and `gamepad_axes` arrays from `shiny --api` and the aliases in
`docs/api.lua`. Invalid names/types raise Lua errors, including when disconnected.
This is gameplay key input using the pinned desktop backend's US-layout key
codes; it is not Unicode text input, IME composition, or a physical scancode API.
Desktop keys include A–Z (`a`–`z`), digits (`0`–`9`), punctuation names such as
`apostrophe` and `left_bracket`, navigation, F1–F12, keypad `kp_*`, left/right
modifiers, locks, `print_screen`, `pause`, and `kb_menu`. OS-reserved combinations
and controls not delivered by the device/backend cannot be guaranteed.

## Lua queries

| API | Result |
| --- | --- |
| `sc.key_down(name)` | Key held during this fixed tick |
| `sc.key_pressed(name)` | Key pressed during this fixed tick |
| `sc.key_released(name)` | Key released during this fixed tick |
| `sc.gamepad_connected()` | Selected gamepad connection state |
| `sc.gamepad_down(button)` | Button held during this fixed tick |
| `sc.gamepad_pressed(button)` | Button press during this fixed tick |
| `sc.gamepad_released(button)` | Button release, including disconnection |
| `sc.gamepad_axis(axis, deadzone?)` | Standardized, deadzone-adjusted axis value |

Buttons use positional names: `south/east/west/north`,
`dpad_up/dpad_right/dpad_down/dpad_left`, `left_shoulder/right_shoulder`,
`left_trigger/right_trigger`, `left_thumb/right_thumb`, `back/start/guide`.
Button availability, especially `guide`, depends on the backend and operating
system. Trigger buttons follow raylib's native digital threshold (GLFW: native
axis greater than 0.1, equivalent to standardized trigger greater than 0.55).
Read trigger axes if the game needs a different threshold.

Axes are `left_x/left_y/right_x/right_y/left_trigger/right_trigger`. Sticks use
`[-1,1]`, positive right/down; triggers use `[0,1]`, zero at rest. The default
deadzone is **0.2 per axis**, not radial. For magnitude `m > d`, output magnitude
is `(m-d)/(1-d)`; otherwise zero. Pass `0` for standardized values without a
deadzone. The argument must be finite, in `[0,1)`, and representable below 1 as a
32-bit float. Missing axes/devices return zero; missing buttons return false.

## Tick, device, and host behavior

- Input is committed before `update` and remains unchanged during `draw`.
  Press/release edges survive displayed frames without an update, then clear on
  the next fixed tick. A short tap can report both edges with `down == false`.
  Keyboard repeat does not produce new presses. Multiple events are coalesced
  into booleans; this API does not preserve the count/order of repeated taps
  inside one simulation tick. Only backend-reported events can be retained.
- One active gamepad is selected from raylib's four desktop slots, starting with
  the lowest available slot and keeping it until it disconnects. Disconnection
  clears buttons/axes and releases held buttons. At least one disconnected
  simulation tick is submitted before a replacement can be selected.
- Window focus loss submits neutral controls and releases held keys/buttons.
  Sampling resumes on focus return. Connection queries are also neutral while
  unfocused. There is no input query to other applications.
- Scene switches and successful hot reloads inherit held inputs before `init`,
  clearing already consumed edges. Buffered, unconsumed live events survive.
- Legacy actions OR keyboard and gamepad sources. Left-stick X/Y crosses
  `-0.25/+0.25` to provide directions; this threshold is independent of the
  configurable direct-axis deadzone. Handing an action between sources in one
  sample does not create an extra action edge; an observed release/repress of
  the same held control can retrigger it. Existing arrow/WASD, Space/Z, E/X, D-pad,
  south/east bindings remain intact.
- By default, all keys belong to the game and the close button exits the host.
  Explicit `--debug-keys` restores F1 stats, F2 bounds, F3 lighting, F5 reload,
  P pause, O single step, and Escape exit. Debug shortcuts remain visible to
  device queries when a simulation tick consumes them; Escape can exit first.
  raylib's implicit F12/Ctrl-F12 capture shortcuts are disabled; `--capture`
  continues to export the requested native image.

## Replay formats

Legacy `frame mask` files remain unchanged: masks are 0–63, frames strictly
increase, and comments start with `#`. They drive only the six actions; direct
device queries stay neutral. The selected-controller format uses `{"version":2}`:

```jsonl
{"version":2}
{"frame":0,"keys":[],"gamepad":{"connected":false}}
{"frame":10,"keys":["j"],"gamepad":{"connected":true,"buttons":["south"],"axes":{"left_x":0.6,"right_trigger":1}}}
{"frame":11,"keys":[],"gamepad":{"connected":true},"key_pressed":["escape"],"key_released":["escape"]}
{"frame":30,"keys":[],"gamepad":{"connected":false}}
```

Every record requires `frame`, `keys`, and `gamepad.connected`. Frame numbers
are global zero-based integers in `0..1000000000`, strictly increasing across
scene switches. Each record replaces the **entire** held device state; omitted
buttons/axes are neutral. State holds until the next record. Initial state is
neutral. `buttons` is a name array and `axes` is an object of standardized values
before applying a deadzone. Axes must be finite and within the ranges above.

Optional top-level `key_pressed`, `key_released`, `button_pressed`, and
`button_released` arrays supplement edges inferred from held-state changes.
They apply only at the record's frame. For a completed tap, provide both edges
and leave that control out of held state; a press on a non-held control also
implies release. A previously held control may explicitly release and repress
within the same tick. A press on an already-held control without release is
treated as repeat and suppressed.

Unknown fields/names, duplicate JSON members or array names, wrong types,
non-increasing frames, and non-neutral held/buttons/axes on a disconnected pad
are errors with file/line diagnostics. Explicit button releases on a disconnect
frame are allowed. JSON lines are limited to 64 KiB and files to one million
records. Blank lines are accepted; JSON records do not accept inline comments.
Legacy and version 2 records cannot be mixed.

All formats isolate gameplay from live device state. Explicit host
debug shortcuts can still control the host. The existing JSON snapshot gains
`input`, containing the named held controls, per-tick edges, connection state,
and all six standardized axes. The world hash includes these logical fields;
hash strings are not compatible with older binaries. Same binary/platform,
project, seed and replay produce the same results. There is no arbitrary VM
checkpoint restoration or cross-platform float equivalence claim.

Current `--record FILE` output begins with `{"version":3}` and records one
normalized snapshot per simulation tick. `--replay FILE` isolates live input.
Version 3 keeps `frame`, `keys`, and `gamepad`, and adds these optional fields:

- `pads`: exactly four objects containing `connected`, `buttons`, `axes`,
  `pressed`, and `released`; their positions correspond to Lua slots 1..4.
- `mouse`: logical viewport `x`, `y`, movement `dx`, `dy`, `wheel_x`, `wheel_y`,
  `inside`, and `buttons`/`pressed`/`released` name arrays. Black bars are outside.
- `text`, `composition`, `clipboard`: UTF-8 strings of at most 4095 bytes.
  Text and paste are consumed once; composition persists until replaced.

The `sc.input` namespace exposes these snapshots and individual controls.
Unslotted gamepad queries retain the selected-controller view; an optional slot
argument addresses one of the four pads directly. See [new-systems.md](new-systems.md)
for text editing, action profiles and settings.

## Example and tests

Run `shiny examples/input` for the live Input Lab, or
`shiny examples/input --replay examples/input/demo.jsonl --frames 125` for its
replay. Add `--headless` for automated verification. `tests/test_input.cpp` covers
buffering and device selection; `tests/input_integration.py` runs the Lua/replay
contracts through the host. On a Windows desktop, explicitly run
`python tests/native_input_windows.py build-input/shiny.exe` for keyboard,
focus, and host-shortcut checks targeting only that child process's window.
Physical gamepad validation is separate from simulated input tests.
