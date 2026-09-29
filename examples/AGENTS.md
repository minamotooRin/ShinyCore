# Sample development

Keep game rules in ordinary Lua and preserve original assets. Use each project's
local API annotations and select only checks relevant to the change.

Visual changes require actual native screenshots and inspection. From the engine
root, select relevant cases with `tools/capture_samples.py`; for a new scene use
`shiny PROJECT --capture-hidden --capture FILE.png --frames N --mute --replay FILE
--save-dir ISOLATED_DIR`. Keep runs bounded, hidden and unfocused; do not launch
visible windows or play audio during routine development.

Inspect text readability, clipping/overlap, focus and modal visibility, missing
assets, camera framing and gameplay visibility as relevant. Capture representative
changed states, fix findings and inspect the affected state again. Do not rerun
all scenes when a focused check suffices.

Keep the PNG, replay/frame reference and concise finding. A generated screenshot
is pending review until actually viewed. Headless results do not prove rendering;
static screenshots do not prove animation, audio, device input or complete visual
acceptance. Record unverified coverage explicitly. Existing evidence and commands:
`../docs/verification/sample-visuals.md`.
