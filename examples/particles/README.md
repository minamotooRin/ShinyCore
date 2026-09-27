# Particle columns

From the repository, run:

```powershell
.\build\full\shiny.exe examples/particles --frames 90 --replay examples/particles/smoke.txt --capture particles.png
```

This fixture runs three continuous emitters with speed/lifetime ranges and three-key
size/RGBA curves, testing translucent overlap, expiration and capacity reuse.
`--headless` accepts the same replay; `--profile particles.jsonl` records active count
and capacity. It is not the combined 20,000-particle performance acceptance scene.
