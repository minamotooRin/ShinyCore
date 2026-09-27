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
import uuid
import sdk


ROOT = Path(__file__).resolve().parents[1]

SCENE = '''-- Start with one room and one controller. The public API is in .luarc.json.
local Controller = require("game.controller")
local player, controller

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
         body = {type="dynamic", friction=0, fixed_rotation=true}, gravity = 1,
         color = "#F9D98B", glow = 64},
    },
    init = function()
        player = sc.find("player")
        sc.camera.follow(player)
        controller = Controller.new(player)
        sc.state.set("visits", (sc.state.get("visits") or 0) + 1)
    end,
    update = function(dt)
        Controller.update(controller, dt)
        if sc.pressed("action") then
            if sc.get(player).x > 300 then sc.scene("rooms/second.lua")
            else assert(sc.save.write("checkpoint")) end
        end
        if sc.pressed("up") then
            local ok, error = sc.save.load("checkpoint")
            if not ok then sc.log(error) end
        end
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

All keyboard keys belong to the game by default; close the window to exit.
Use `--debug-keys` explicitly for F1/F2/F3/F5/P/O/Escape host controls.
Custom controls use `sc.key_down/pressed/released` and `sc.gamepad_*`;
keep binding names in Lua tables. Device replay format is documented at `{ROOT / "docs" / "input.md"}`.
The `.luarc.json` file links that same API for Lua Language Server completion.

- Keep game rules in Lua and assets in this project. Each scene returns a table.
- Keep package.json script/resource roots current; literal require dependencies are collected automatically.
- shiny-sdk.json pins the local SDK and engine version. Record intentional SDK edits with tools/sdk.py and a new local version before packaging.
- Use only documented `sc` APIs. Unknown configuration fields are errors.
- `sc.get()` returns a copy; apply changes with `sc.set(id, patch)`.
- Update simulation state only in `init()` or `update(dt)`. `dt` is always 1/60.
- Keep `draw(alpha)` free of side effects, including changes to Lua locals.
- Use `sc.random()` for seeded randomness. project-local `require` is available; file/process I/O is unavailable.
- Spawn in empty space. `#` tiles are solid and `=` tiles are one-way platforms.
- Preserve artwork and unrelated changes. Add a replay when changing game behavior.
- Run these checks from this project's directory, then visually inspect rendering changes:

```sh
{checks[0]}
{checks[1]}
```

The engine binary is built separately in its checkout. If the engine is moved,
update the executable commands here; SDK paths in `.luarc.json` remain relative.
On Windows, Visual Studio builds normally place the executable in
`build/Release/shiny.exe` after `cmake --build build --config Release`; adjust
the commands above if that configuration was built after project creation.
'''
    readme = f'''# My ShinyCore Game

An editable room, a small player, and two one-way platforms. Move with A/D or
the arrow keys, and jump with Space or Z. Edit `main.lua` to build your game.
The game/controller.lua module is ordinary editable Lua. Press E to save, UP to load, or E at the right side to enter the second room.

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
        "workspace.library": ["docs/api.lua", "lib/shiny"],
        "workspace.checkThirdParty": False,
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir()  # Exclusive creation also catches a concurrent existing path.
    try:
        (destination / "docs").mkdir()
        shutil.copy2(api, destination / "docs/api.lua")
        shutil.copy2(guide, destination / "docs/llm-guide.md")
        shutil.copy2(ROOT / "docs/input.md", destination / "docs/input.md")
        shutil.copytree(ROOT / "lua/shiny", destination / "lib/shiny", ignore=shutil.ignore_patterns("version.json"))
        shutil.copy2(ROOT / "LICENSE", destination / "lib/shiny/LICENSE.txt")
        sdk.write(destination, sdk.read(ROOT / "lua/shiny/version.json"))
        # All SDK references are project-relative; only the separately built executable has a host path.
        agents = agents.replace(str(api), "docs/api.lua").replace(str(guide), "docs/llm-guide.md").replace(str(ROOT / "docs/input.md"), "docs/input.md")
        readme = readme.replace(str(api), "docs/api.lua")
        (destination / "game").mkdir()
        (destination / "rooms").mkdir()
        shutil.copy2(ROOT / "examples/workshop/game/controller.lua", destination / "game/controller.lua")
        shutil.copy2(ROOT / "examples/workshop/game/animation.lua", destination / "game/animation.lua")
        project_id = "game." + uuid.uuid4().hex
        (destination / "project.lua").write_text('return {id="' + project_id + '", data_version=1, rooms={"main.lua", "rooms/second.lua"}}\n', encoding="utf-8")
        (destination / "rooms/second.lua").write_text('return {gravity=0, init=function() sc.message("SECOND ROOM / E TO RETURN") end, update=function() if sc.pressed("action") then sc.scene("main.lua") end end}\n', encoding="utf-8")
        for name, contents in {
            "package.json": json.dumps({"format": 1, "scripts": ["main.lua", "rooms/second.lua"],
                "files": ["smoke.replay", "README.md", "AGENTS.md", ".luarc.json", "docs/api.lua", "docs/llm-guide.md", "docs/input.md"]}, indent=2) + "\n",
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
