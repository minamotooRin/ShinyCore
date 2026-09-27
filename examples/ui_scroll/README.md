# Horizontal UI scrolling

Run `shiny examples/ui_scroll`. The card strip uses `kind="scroll"` with
`axis="horizontal"`; ordinary scroll containers remain vertical. Move focus with
Tab or a controller shoulder button, use the mouse wheel inside the strip, or drag
its bottom scrollbar. Enter/click selects a room. `ui_scroll` watch reports the
selected card, offset and maximum for replay checks.

From the engine root, run `python tools/capture_samples.py build/full/shiny.exe
--output build/ui-scroll-capture --case ui-scroll` for a hidden native screenshot.
The project needs no assets, audio or network connection.
