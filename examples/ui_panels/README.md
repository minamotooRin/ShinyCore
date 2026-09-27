# UI panels

Run `shiny examples/ui_panels`. Nine anchored buttons share one 16×16 original
PNG and a four-pixel nine-slice frame. The center stretches; colored corners keep
their size. Tab, arrows or a controller move focus; confirm or click selects a
button. Watch `selected` and `focus` for Agent checks.
Run `python tools/scenario.py build/full/shiny.exe examples/ui_panels/scenario.json`
from the engine root to verify right-arrow focus and Enter selection at exact frames.

`skin` on a node overrides `theme.skins[kind]`; false selects the ordinary color
background. `anchor` aligns within the parent's allocated rectangle; overlays
provide their whole padded inner rectangle. See `docs/ui-lifecycle.md` and
`docs/image-regions.md` in the engine repository.

Hidden visual check: `shiny examples/ui_panels --frames 1 --replay
examples/ui_panels/idle.jsonl --mute --capture-hidden --capture OUTPUT.png
--save-dir TEMP_SAVES` (join onto one command line).

The PNG was authored as a deterministic pixel frame for this project. No external
font, audio, network service or additional engine module is required.
