# External TSX import

Run `shiny examples/tsx_import`. This small project shows a JSON map using two
external TSX tilesets: an animated atlas and a sparse image collection. A tile
object also inherits from the external XML `plaque.tx` template. Map and group
`file` properties remain readable through `sc.stream.metadata()`. The files
are local. Runtime uses the built chunk index, PNGs and two note files retained
by the package dependency closure.

Rebuild from the engine checkout:

```powershell
python examples/tsx_import/make_source.py
$result = python tools/assets.py examples/tsx_import/assets.build.json build/tsx-import-content | ConvertFrom-Json
Copy-Item (Join-Path $result.directory 'map-court') examples/tsx_import/content/ -Recurse
build/full/shiny.exe examples/tsx_import --check-all
build/full/shiny.exe examples/tsx_import --headless --frames 30 --replay examples/tsx_import/smoke.jsonl
```

`make_source.py` creates the original pixel art and JSON map deterministically;
`assets/ground.tsx`, `assets/flower.tsx` and `assets/plaque.tx` are hand-authored Tiled-format inputs.
No Tiled editor export is claimed. See [the importer contract](../../docs/tiled-tsx.md).
