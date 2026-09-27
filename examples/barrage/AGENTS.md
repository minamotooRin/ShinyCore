Read docs/api.lua. Keep gameplay in main.lua and rules/seed in challenge.lua;
use the local lib/shiny modules and preserve original artwork.

Check content with `shiny . --check-all`. The short smoke.jsonl uses 180 frames;
the actual six-wave challenge.jsonl uses 18138 frames. Select checks relevant to
the change; do not shorten challenge waves to obtain a successful replay.
Use --capture-hidden --capture FILE.png --frames N --mute with an isolated save
directory; inspect changed states rather than replaying every wave. Keep
package.json roots current; generated screenshots require actual inspection.
