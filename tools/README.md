# Developer CLIs

Run scripts from the repository root with Python. `new_game.py` scaffolds projects; `assets.py`, `aseprite.py`, `tiled_xml.py` and `nav_bake.py` build content; `package.py` assembles releases. `scenario.py`, `capture_samples.py`, trace comparison and profiling scripts support verification. The remaining `build_*` scripts reproduce sample-owned assets.

These scripts are development-time tools; packaged games do not require Python. Keep their command paths stable for the documented workflows.

After changing documentation paths, run `python tools/check_docs.py` to validate local Markdown links.
