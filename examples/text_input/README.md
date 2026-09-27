# Text input fixture

Run `shiny examples/text_input`. Tests lazy glyph loading with no predeclared characters,
grapheme-safe selection/deletion, multiline editing, clipboard shortcuts and undo.
Composition previews replace the selection visually, wrap/scroll with the text and
use an accent underline; commit changes the value once, while cancellation preserves it.
Candidate navigation does not also edit the field. The preview caret is at the end;
internal IME cursor/clause attributes are not yet exposed.
The outer panel scrolls with the wheel or thumb. Tab/gamepad focus reveals controls;
the bottom button returns focus to the name field. This also demonstrates nested
text scrolling inside a scrolling panel.
Hover or focus a control for its delayed tooltip; Escape dismisses the current hint.
`Inspect note` writes the current text to the explicit `inspected_note` debug watch.
The text-size slider changes the notes font: Left/Right steps, PageUp/PageDown makes
larger changes, Home/End reaches the limits. Buttons accept Enter, Space or gamepad South.
The included original Workshop font is a deliberately small subset; use a full licensed
CJK font in a game accepting arbitrary Han characters. IME composition requires Windows.
Actual IME hardware validation remains outstanding. This is not a complete sample game.

`shiny examples/text_input --headless --frames 4 --replay examples/text_input/smoke.jsonl`
checks a composition/commit/undo sequence without opening a window. This verifies Lua
behavior and draw commands, not native font pixels or the OS candidate window.
