# 用 Agent 开发 ShinyCore 游戏

目标是让每次修改都能在文本、回放与原生画面中被检查。先读项目的 AGENTS.md、project.lua 和引擎 docs/api.lua；再运行 `shiny --api` 获取当前二进制真正提供的接口。

## 一次完整的迭代

1. 使用 `python tools/new_game.py 新目录` 创建项目，或修改已有项目中的最小相关模块。
2. 在 project.lua 声明入口、rooms 和资源。流式房间可用 `stream_indexes` 将房间路径映射到已构建索引，使索引在 `init` 前由工作线程读取；房间的 `init` 仍调用同一路径的 `sc.stream.open`。每个房间要能在空 sc.state 下初始化；默认值属于游戏脚本。
3. 运行 `shiny --check-all 项目目录`，覆盖配置、模块、init、初始 draw、物理几何和资源预检。
4. 运行 `shiny --headless 项目目录 --frames 180 --seed 42 --replay smoke.jsonl`。示例回放使用版本 3 的设备快照；断言位置、接触、状态和场景行为，不只比较哈希。
5. 修改画面后运行原生程序，并通过 `--frames 90 --capture 截图绝对路径.png` 检查结果。
6. 使用 `tools/package.py 引擎程序 新包目录 --project 项目目录`，移动包后再运行验证。

检查模式不运行 update。条件加载、存档恢复和后续房间分支需要回放覆盖。无窗口构建不验证 GPU/声音输出；图形构建的 --check 会额外解码图片与音频，但仍不创建窗口。

## 数据和模块

`require("game.controller")` 加载项目内 Lua 文件并缓存。不要使用文件 I/O、外部模块或动态执行字符串。先在文件顶部加载依赖，draw 只读取已经准备好的状态。

`sc.get(id)` 是快照，必须用 `sc.set(id,{vx=90})` 提交。配置字段、数值、路径和句柄必须正确；不要捕获并忽略引擎报错以掩盖拼写错误。原生操作失败返回 nil,error 的接口应当明确处理。

局部 Lua 表用于当前房间逻辑；跨房间数据使用 `sc.state.set/get`，例如金币、任务标记和稳定的对象名称。不要把 entity/audio/joint ID 存入持久数据。启用 `--debug-keys` 后，F5 重建当前房间而保留显式状态；init 应基于已有数据重建世界。

存档是显式数据加房间入口。磁盘测试必须指定临时 `--save-dir`，避免依赖真实用户存档。新版 data_version 不读取旧记录，并明确报告版本不支持；使用独立进程测试保存后的重新启动恢复。

## 物理与地图

新项目显式使用 body.type。实体默认是 artwork，旧 dynamic 只是兼容写法。body=false 移除身体。Box2D 有接触容差；落地位置断言使用合理容差，行为回归检查可达性、接地和支撑关系。

Controller 和 Animation 是可编辑的普通 Lua 模块，没有隐藏的控制器类型。移动平台的 support 和速度由脚本使用；不要把跳跃手感塞回 C++。凸多边形需 3..8 个点；复合身体最多 4 个形状；重建身体会销毁关联关节。

Tiled 直接编辑 .tmj/.tsj 文件，设置 tile 的 collision 为 solid 或 one_way。只使用支持的子集，详见 Workshop README。对象层由 `sc.objects()` 返回原始记录，由 Lua 工厂决定生成哪些实体。

## 资源与发行

project.resources 集中声明 PNG、WAV、Ogg Vorbis 与字体；字形按需缓存，字体需覆盖游戏使用的文字并保留许可证。中英文布局使用 sc.measure 与 sc.text 的 font/wrap/align；复杂文字塑形和双向排版不支持。

资源路径相对于项目，使用正斜线，最多 127 字节。新增依赖保持版本和校验固定，不在游戏运行时联网获取素材。发行包只面向构建系统和架构；可选网络需单独开启并验证双进程交通。

保留用户已有修改，不自动提交/推送。测试失败先缩小触发条件；不要把旧验证记录当作新版本证据。hash 不包含任意 Lua 局部状态、关节和求解器缓存，不可当作完整存档。

## Device input and new replays

Use `sc.input.key_down/pressed/released(name)` and `sc.input.gamepad_down/pressed/released(name)` for game-defined controls. `sc.input.gamepad_axis(axis, deadzone, slot)` handles analog motion; optional slots select one of four pads. Names come from `shiny --api` and `docs/api.lua`; invalid names fail explicitly. For rebindable actions use the bundled `shiny.input` module. These queries read fixed-tick state, not text entry.

All keys belong to the game by default. Launch with `--debug-keys` to opt into host shortcuts. Use version-3 JSON Lines snapshots to exercise keys, axes, mouse, text and quick edges headlessly. See [input.md](input.md) and the bundled `smoke.jsonl`.
