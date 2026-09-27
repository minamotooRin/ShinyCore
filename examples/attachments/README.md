# Persistent attachments

Run `shiny examples/attachments`. Space starts/stops a physics-driven rotating root;
its lamp and nested marker follow through native attachments. D detaches the lamp
without moving it, A attaches it again, S saves, L reloads, R resets the room.
The example starts paused and requires no optional modules or external assets.

`rig.lua` is the explicit game save schema: format 1, persistent object IDs, parent
persistent IDs, local poses for attached objects and world poses for roots. Shapes,
colors and bodies stay in authored definitions. No runtime handles are persisted.
Restore maps names to batch indices, then creates the whole graph atomically;
records deliberately place children before parents. Invalid names, cycles, poses
or capacities cannot publish a partial hierarchy. Room replacement retains the
old room if candidate initialization fails. This is a small sample schema, not
arbitrary VM or physics state serialization.

From the engine root:

```powershell
build/full/shiny.exe examples/attachments --headless --frames 40 --replay examples/attachments/smoke.jsonl --save-dir build/attachment-example-saves
```

The replay rotates, pauses, saves, detaches, resets and reloads the hierarchy.
Its final watch can be compared to the saved `state.rig`; reconstructed handles
are different after the room change. Real-time interpolation requires running
without `--frames`; bounded/headless runs show the latest fixed state.

Focused native/headless save checks and headless ASan/UBSan pass. The hidden
40-frame restored image was reviewed in `build/attachment-save-reviewed/restored.png`;
fixed fields agree between native and headless. This does not claim real-time
interpolation or physical input-device acceptance.
