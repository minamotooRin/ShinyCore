# Camera

Run `shiny examples/camera`. WASD moves; Q/E rotates; Up/Down zooms; Space shakes.
The pink world cursor tracks the logical mouse position, including rotation and shake.
The HUD stays in screen space. No optional modules or external assets are required.
I toggles display interpolation; T teleports and cuts the camera without blending.
The Player label uses `sc.presentation.pose`, matching the native entity drawing.
Run without `--frames` to see interpolation; bounded/headless runs use the current fixed state.
Headless smoke: `shiny examples/camera --headless --frames 4 --replay examples/camera/smoke.jsonl`.
Native pixels have not been validated yet.
