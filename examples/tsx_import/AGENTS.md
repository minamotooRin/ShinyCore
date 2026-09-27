Use the project-local docs/api.lua and lib/shiny/stream_tiles.lua. Rebuild PNG/JSON
source data with make_source.py, then content with tools/assets.py from the engine
checkout. Runtime uses the checked-in content index and images; TSX is offline input.
For visual changes capture hidden/muted native frames into an isolated save directory
and inspect the PNG. Keep diagnostics and tests focused on changed behavior.
