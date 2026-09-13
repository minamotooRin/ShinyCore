# Input Lab

Run `shiny examples/input` to inspect held keys, press/release edges, gamepad
connection/buttons and all six deadzone-adjusted axes. Bindings are ordinary Lua
data. The display abbreviates long lists of simultaneously held controls.

```sh
shiny examples/input --replay examples/input/demo.jsonl --frames 125
shiny --headless examples/input --replay examples/input/demo.jsonl --frames 125
```

The replay exercises keyboard keys, all six axes, buttons, disconnection,
reconnection and an Escape tap. No physical controller is required to replay it.
Default Escape/P/O/F keys belong to the game; close the window to exit. Use
`--debug-keys` explicitly to enable host debugging shortcuts. Full semantics and
format: [input documentation](../../docs/input.md).
