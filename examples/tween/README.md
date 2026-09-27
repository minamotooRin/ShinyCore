# Tween timelines

Two numeric targets move in parallel at different durations, wait together, then
return. Advance only in fixed update; draw reads the current values. Space pauses,
C cancels without snapping, and R creates a fresh timeline.

```powershell
.\build\full\shiny.exe examples/tween
.\build\full\shiny.exe examples/tween --headless --frames 160 --replay examples/tween/smoke.jsonl
```

The project includes a pinned local tween module and native API annotations. No
external assets or runtime Python are needed. The `timeline` watch exposes lane
positions, stage, completion, cancellation and pause state. At 160 frames both
positions are 40 and the timeline is complete. See ../../docs/tween.md for the
composition, cancellation, ownership and surplus-time contracts.
