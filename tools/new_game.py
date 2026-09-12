#!/usr/bin/env python3
"""Create a small ShinyCore Lua project without overwriting existing work."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]

SCENE = '''-- Start with one room and one controller. The public API is in .luarc.json.
local player

local function make_map()
    local rows = {}
    for y = 0, 26 do
        local cells = {}
        for x = 0, 47 do
            local tile = (x == 0 or x == 47 or y >= 24) and "#" or "."
            if (y == 19 and x >= 14 and x <= 21)
                or (y == 15 and x >= 28 and x <= 35) then tile = "=" end
            cells[#cells + 1] = tile
        end
        rows[#rows + 1] = table.concat(cells)
    end
    return rows
end

return {
    title = "My ShinyCore Game",
    width = 384, height = 216, gravity = 600, ambient = 0.5,
    map = {
        tile_size = 8, rows = make_map(),
        color = "#19363D", accent = "#54837C", background = "#0B1924",
    },
    entities = {
        {tag = "player", x = 32, y = 178, w = 8, h = 14,
         dynamic = true, solid = true, gravity = 1,
         color = "#F9D98B", glow = 64},
    },
    init = function()
        player = sc.find("player")
        sc.camera(player)
    end,
    update = function(dt)
        local p = sc.get(player)
        local axis = (sc.down("right") and 1 or 0) - (sc.down("left") and 1 or 0)
        local vy = p.vy
        if sc.pressed("jump") and p.grounded then vy = -240 end
        sc.set(player, {vx = axis * 96, vy = vy})
    end,
    draw = function(alpha)
        -- Drawing reads state and queues visuals; it never advances the game.
        sc.rect(10, 10, 230, 36, "#08131DDD", true)
        sc.text("YOUR SMALL WORLD", 17, 15, 10, "#F9D98B", true)
        sc.text("A/D MOVE  SPACE JUMP", 17, 31, 10, "#99B9B0", true)
    end,
}
'''

REPLAY = '''# Fixed-tick held input. Right, jump, stop, then move left.
0 2
30 18
31 2
90 0
120 1
150 0
'''


def command(arguments: list[str]) -> str:
    return subprocess.list2cmdline(arguments) if os.name == "nt" else shlex.join(arguments)


def create_project(destination: Path) -> list[str]:
    api = ROOT / "docs" / "api.lua"
    guide = ROOT / "docs" / "llm-guide.md"
    if not api.is_file() or not guide.is_file():
        raise OSError("engine API documentation is missing; run this tool from a complete ShinyCore checkout")
    if os.path.lexists(destination):
        raise FileExistsError(f"destination already exists; refusing to overwrite: {destination}")
    binary = ROOT / "build" / ("shiny.exe" if os.name == "nt" else "shiny")
    release_binary = ROOT / "build" / "Release" / "shiny.exe"
    if os.name == "nt" and release_binary.is_file():
        binary = release_binary
    checks = [
        command([str(binary), "--check", "."]),
        command([str(binary), "--headless", ".", "--frames", "180", "--replay", "smoke.replay"]),
        command([str(binary), "."]),
    ]
    agents = f'''# Working on this ShinyCore game

Read the authoritative Lua API at `{api}` and the workflow at `{guide}`.
The `.luarc.json` file links that same API for Lua Language Server completion.

- Keep game rules in Lua and assets in this project. Each scene returns a table.
- Use only documented `sc` APIs. Unknown configuration fields are errors.
- `sc.get()` returns a copy; apply changes with `sc.set(id, patch)`.
- Update simulation state only in `init()` or `update(dt)`. `dt` is always 1/60.
- Keep `draw(alpha)` free of side effects, including changes to Lua locals.
- Use `sc.random()` for seeded randomness. `require` and file/process I/O are unavailable.
- Spawn in empty space. `#` tiles are solid and `=` tiles are one-way platforms.
- Preserve artwork and unrelated changes. Add a replay when changing game behavior.
- Run these checks from this project's directory, then visually inspect rendering changes:

```sh
{checks[0]}
{checks[1]}
```

The engine binary is built separately in its checkout. If the engine is moved,
update its absolute paths here and in `.luarc.json`.
On Windows, Visual Studio builds normally place the executable in
`build/Release/shiny.exe` after `cmake --build build --config Release`; adjust
the commands above if that configuration was built after project creation.
'''
    readme = f'''# My ShinyCore Game

An editable room, a small player, and two one-way platforms. Move with A/D or
the arrow keys, and jump with Space or Z. Edit `main.lua` to build your game.
There are no external assets or Lua module dependencies.

From this directory, after building ShinyCore:

```sh
{checks[0]}
{checks[1]}
{checks[2]}
```

`smoke.replay` moves right, jumps, stops, and moves left over 180 fixed ticks.
Headless mode prints a JSON snapshot and exits nonzero for script errors.
The API reference is `{api}`; see `AGENTS.md` for the LLM workflow.
On Windows with Visual Studio, build with `cmake --build build --config Release`
in the engine checkout. The resulting executable is normally
`build/Release/shiny.exe`; adjust these commands if necessary.
'''
    config = {
        "runtime.version": "Lua 5.4",
        "diagnostics.globals": ["sc"],
        "workspace.library": [str(api)],
        "workspace.checkThirdParty": False,
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir()  # Exclusive creation also catches a concurrent existing path.
    try:
        for name, contents in {
            "main.lua": SCENE,
            "smoke.replay": REPLAY,
            "README.md": readme,
            "AGENTS.md": agents,
            ".luarc.json": json.dumps(config, ensure_ascii=False, indent=2) + "\n",
        }.items():
            (destination / name).write_text(contents, encoding="utf-8")
    except BaseException:
        shutil.rmtree(destination)
        raise
    return checks


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", help="new game directory; it must not already exist")
    args = parser.parse_args(argv)
    destination = Path(os.path.abspath(os.path.expanduser(args.destination)))
    try:
        checks = create_project(destination)
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Created {destination}\n")
    print("Run from the new project directory:")
    print(command(["cd", str(destination)]))
    for check in checks:
        print(check)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
