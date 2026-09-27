Read docs/api.lua. title.lua selects a complete save or an explicitly reset journey;
main.lua owns the streamed room, quest.lua its rules, and patrol.lua the courier.
Keep gameplay in Lua and preserve original artwork.
Keep package.json roots current; all stream chunks are included through their index.

Run --check-all and the replay relevant to the change, with an isolated --save-dir.
walkthrough.jsonl completes the actual quest in 3244 frames. For visual changes use
--capture-hidden --capture FILE.png --frames N --mute, then inspect the native PNG.
Do not equate headless checks with visual or device acceptance.
