# Static streamed-route verification

On Windows, `tests/nav_route.py build/full/shiny.exe` first bakes the 16-block
Wayfarer map through the headless native engine, compiles two identical graph
modules and compares their bytes. The graph contains 16 connected components and
24 cross-block portals. The actual Lua VM routes from the first to the far corner,
checks each paired portal is one cell apart, and reports blocked, unloaded and
budget-exhausted requests explicitly.

A second graph has a negative chunk coordinate and two disconnected components
inside one chunk. The reachable crossing chooses the expected boundary cells;
the other component cannot reach the goal. The project scaffold's focused test
passes with the new standard Lua module and SDK version. No rendering changed.

This validates static coarse connectivity and the Lua query path. It does not
validate stale graphs after saved map edits, live streaming prefetch policy, or
physical movement across each portal. Those remain implementation and acceptance
work; local `World.path` and loading boundaries remain authoritative.
