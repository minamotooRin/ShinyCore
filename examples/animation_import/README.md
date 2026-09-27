# Imported animation

Run `shiny examples/animation_import`. Three actors share one generated atlas and
play idle, walk and one finite ping-pong clip using ordinary shiny.animation data.
No Python or editor is needed at runtime. The original Workshop Keeper image remains
unchanged (repository MIT license); keeper.json is a hand-authored Aseprite-format
export fixture. The actual Aseprite editor was not used to create that fixture.

From the engine checkout, rebuild the offline source pair:

```powershell
$result = python tools/assets.py examples/animation_import/assets.build.json build/animation-import-content | ConvertFrom-Json
Copy-Item (Join-Path $result.directory 'animation-hero.png') examples/animation_import/content/
Copy-Item (Join-Path $result.directory 'animations.lua') examples/animation_import/content/
build/lightweight/shiny.exe examples/animation_import --check-all
build/lightweight/shiny.exe examples/animation_import --headless --frames 60 --replay examples/animation_import/smoke.jsonl
```

project.lua declares the image resource; content/animations.lua is a generated plain
Lua catalog consumed by main.lua. Source PNG/JSON and the cache are not required by
the running game and are omitted from its runtime package. See docs/aseprite.md in
the engine checkout for trimmed exports, tag directions, bounds and diagnostics.
