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
        if sc.input.key_pressed("e") or sc.input.gamepad_pressed("west") then
            if sc.get(player).x > 300 then sc.scene("rooms/second.lua")
            else assert(sc.save.write("checkpoint")) end
        end
        if sc.input.key_pressed("up") then
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

CONTROLLER = '''-- Fixed-tick movement and jump buffering stay in ordinary game Lua.
local Controller = {}
function Controller.new(id) return {id=id,coyote=0,buffer=0} end
function Controller.update(c,dt)
    local input=sc.input
    local p=sc.get(c.id)
    c.coyote=p.grounded and .1 or math.max(0,c.coyote-dt)
    local jump=input.key_pressed("space") or input.key_pressed("z") or input.gamepad_pressed("south")
    c.buffer=jump and .12 or math.max(0,c.buffer-dt)
    local left=input.key_down("a") or input.key_down("left") or input.gamepad_down("dpad_left")
    local right=input.key_down("d") or input.key_down("right") or input.gamepad_down("dpad_right")
    local axis=(right and 1 or 0)-(left and 1 or 0)
    if axis==0 then axis=input.gamepad_axis("left_x") end
    local vx,vy=axis*90,p.vy
    if p.grounded then
        if p.support~=0 then vx=vx+sc.get(p.support).vx end
        if axis~=0 and p.normal_y<-.5 then vy=-vx*p.normal_x/p.normal_y end
    end
    if (input.key_down("s") or input.key_down("down") or input.gamepad_down("dpad_down")) and jump then
        sc.physics.drop(c.id,.25);vy=50;c.buffer=0
    elseif c.buffer>0 and c.coyote>0 then
        vy=-220;c.buffer=0;c.coyote=0
    end
    sc.set(c.id,{vx=vx,vy=vy,flip_x=axis<0})
end
return Controller
'''

REPLAY = '''{"version":3}
{"frame":0,"keys":["d"],"gamepad":{"connected":false}}
{"frame":30,"keys":["d","space"],"gamepad":{"connected":false}}
{"frame":31,"keys":["d"],"gamepad":{"connected":false}}
{"frame":90,"keys":[],"gamepad":{"connected":false}}
{"frame":120,"keys":["a"],"gamepad":{"connected":false}}
{"frame":150,"keys":[],"gamepad":{"connected":false}}
'''

PROJECT_GUIDE = '''# 用 Agent 开发本项目

先读根目录 `AGENTS.md`、`project.lua` 和 `docs/api.lua`。`shiny --api` 显示当前
可执行文件真正提供的模块；不要假定开发机上有引擎源码或额外工具。

1. 在 `project.lua` 声明房间、资源与容量；房间初始状态应能从空 `sc.state` 创建。
2. 游戏规则留在普通 Lua 中；`require("game.controller")` 加载本项目模块，
   `require("shiny.input")` 等加载 `lib/shiny/` 内固定版本的标准模块。
3. 从项目目录运行 `shiny --check-all .`，检查全部声明房间和初始绘制。
4. 运行 `shiny --headless . --frames 180 --replay smoke.jsonl`，检查玩法快照和
   显式状态。回放是版本 3 JSON Lines 设备快照。
5. 改动画面后，用图形构建的引擎以 `--capture-hidden --mute --capture` 执行有界
   截图并实际查看 PNG；无窗口运行不能证明像素、声音或实体设备行为。

`sc.get(id)` 返回副本，修改实体请用 `sc.set(id, patch)`。实体和声音句柄只在
当前房间有效；跨房间和存档只保存显式数据及对象持久 ID。存档测试使用隔离目录，
避免覆盖真实用户记录。`draw` 只提交绘制命令，不在其中首次加载模块或修改玩法。

输入读取 `sc.input` 的固定帧快照；命名和可重绑定动作可用 `shiny.input`。
键名与函数签名以 `docs/api.lua` 为准，回放格式见 `docs/input.md`。
`--debug-keys` 才启用宿主调试快捷键。引擎报错应保留字段路径与房间上下文，
不要吞掉错误；检查失败后修复最小触发条件再重跑相关命令。
'''


def command(arguments: list[str]) -> str:
    return subprocess.list2cmdline(arguments) if os.name == "nt" else shlex.join(arguments)


def create_project(destination: Path) -> list[str]:
    api = ROOT / "docs" / "api.lua"
    if not api.is_file():
        raise OSError("engine API documentation is missing; run this tool from a complete ShinyCore checkout")
    if os.path.lexists(destination):
        raise FileExistsError(f"destination already exists; refusing to overwrite: {destination}")
    executable = "shiny.exe" if os.name == "nt" else "shiny"
    candidates = [ROOT / "build" / name / executable for name in ("full", "lightweight", "Release", "")]
    binary = next((str(path) for path in candidates if path.is_file()), "shiny")
    def commands(exe: str) -> list[str]:
        return [command([exe, "--check-all", "."]),
                command([exe, "--headless", ".", "--frames", "180", "--replay", "smoke.jsonl"]),
                command([exe, "."])]
    portable, checks = commands("shiny"), commands(binary)
    agents = f'''# Working on this ShinyCore game

Read the authoritative Lua API at `docs/api.lua` and the workflow at `docs/llm-guide.md`.

All keyboard keys belong to the game by default; close the window to exit.
Use `--debug-keys` explicitly for F1/F2/F3/F5/P/O/Escape host controls.
Custom controls use `sc.input.key_down/pressed/released` and `sc.input.gamepad_*`;
keep rebindable actions in Lua tables. Device replay format is documented at `docs/input.md`.
The `.luarc.json` file links that same API for Lua Language Server completion.

- Keep game rules in Lua and assets in this project. Each scene returns a table.
- Keep package.json script/resource roots current; literal require dependencies are collected automatically.
- shiny-sdk.json pins the local SDK and engine version; keep it in sync if the bundled SDK is intentionally changed.
- Use only documented `sc` APIs. Unknown configuration fields are errors.
- `sc.get()` returns a copy; apply changes with `sc.set(id, patch)`.
- Update simulation state only in `init()` or `update(dt)`. `dt` is always 1/60.
- Keep `draw(alpha)` free of side effects, including changes to Lua locals.
- Use `sc.random()` for seeded randomness. project-local `require` is available; file/process I/O is unavailable.
- Spawn in empty space. `#` tiles are solid and `=` tiles are one-way platforms.
- Preserve artwork and unrelated changes. Add a replay when changing game behavior.
- Run these checks from this project's directory, then visually inspect rendering changes:

```sh
{portable[0]}
{portable[1]}
```

The engine binary is built separately. Keep `shiny` on PATH or replace only that
command word with your executable path; all project files and LuaLS paths are relative.
'''
    readme = f'''# My ShinyCore Game

An editable room, a small player, and two one-way platforms. Move with A/D or
the arrow keys, and jump with Space or Z. Edit `main.lua` to build your game.
The game/controller.lua module is ordinary editable Lua. Press E to save, UP to load, or E at the right side to enter the second room.

From this directory, after building ShinyCore:

```sh
{portable[0]}
{portable[1]}
{portable[2]}
```

`smoke.jsonl` uses version-3 input snapshots to move right, jump, stop, then move left.
Headless mode prints a JSON snapshot and exits nonzero for script errors.
The API reference is `docs/api.lua`; see `AGENTS.md` for the LLM workflow.
Keep `shiny` on PATH or replace that command word with your built executable path.
The project and its local Lua modules move together without source-checkout paths.
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
        (destination / "docs/llm-guide.md").write_text(PROJECT_GUIDE, encoding="utf-8")
        shutil.copy2(ROOT / "docs/input.md", destination / "docs/input.md")
        shutil.copytree(ROOT / "lua/shiny", destination / "lib/shiny", ignore=shutil.ignore_patterns("version.json"))
        shutil.copy2(ROOT / "LICENSE", destination / "lib/shiny/LICENSE.txt")
        sdk.write(destination, sdk.read(ROOT / "lua/shiny/version.json"))
        # All SDK references are project-relative; only the separately built executable has a host path.
        (destination / "game").mkdir()
        (destination / "rooms").mkdir()
        (destination / "game/controller.lua").write_text(CONTROLLER, encoding="utf-8")
        project_id = "game." + uuid.uuid4().hex
        (destination / "project.lua").write_text('return {id="' + project_id + '", data_version=1, rooms={"main.lua", "rooms/second.lua"}}\n', encoding="utf-8")
        (destination / "rooms/second.lua").write_text('return {gravity=0, init=function() sc.message("SECOND ROOM / E TO RETURN") end, update=function() if sc.input.key_pressed("e") or sc.input.gamepad_pressed("west") then sc.scene("main.lua") end end}\n', encoding="utf-8")
        for name, contents in {
            "package.json": json.dumps({"format": 1, "scripts": ["main.lua", "rooms/second.lua"],
                "files": ["smoke.jsonl", "README.md", "AGENTS.md", ".luarc.json", "docs/api.lua", "docs/llm-guide.md", "docs/input.md"]}, indent=2) + "\n",
            "main.lua": SCENE,
            "smoke.jsonl": REPLAY,
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
