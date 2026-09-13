# ShinyCore

**小型原生 2D 引擎 · C++23 + Lua · 为人和 LLM Agent 共同开发设计**

ShinyCore 0.2 提供固定 60 Hz 模拟、Box2D 刚体、Tiled 地图、跨房间数据与磁盘检查点、精灵图集、音频文件和中英文字体。游戏就是一个可读、可回放的 Lua 项目；角色控制、动画、收集和关卡规则保持在普通 Lua 模块中。

## 构建与运行

需要 CMake 3.24+、支持 C++23 与 `std::expected` 的工具链，以及首次获取依赖时的网络。Lua 5.4.9、Box2D 3.1.1、yyjson 0.12.0、raylib 5.5 与字体实现均固定版本并校验；无窗口构建不下载 raylib。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/shiny examples/workshop
```

Windows 可使用现代 MSVC，或 GCC/MinGW + Ninja。Visual Studio 多配置构建使用 `cmake --build build --config Release`，程序在 `build/Release/shiny.exe`。更换编译器时使用新的构建目录。Linux 图形构建需要 GLFW 对应的 OpenGL/X11 开发库，CI 提供安装配置。

Workshop 展示中文 UI、Tiled 图层、箱子、斜坡、移动平台、Lua 动画和存档。A/D 移动，Space 跳跃，Down+Space 穿透单向平台，E 保存，Up 读取，走到右端切换房间。原创 Lantern 和可选联机示例 Duet 仍保留。

F1 统计、F2 实体边界、F3 光照、F5 重载、P 暂停、O 单步、Esc 退出。F5 和切换房间会重建 VM 与世界，继承显式 `sc.state`；候选脚本、资源或窗口尺寸校验失败时，图形运行保留旧场景并报告错误。

## 当前能力

| 子系统 | 0.2 能力 |
| --- | --- |
| 模拟 | 固定 60 Hz、输入边沿、种子随机数、回放和 JSON 诊断摘要 |
| 物理 | 统一 Box2D；静态/运动学/动态身体，矩形/圆/胶囊/凸多边形及最多 4 个复合形状 |
| 交互 | 刚体推挤、斜坡、单向平台、传感器、射线、过滤、距离/转轴/竖直滑动关节 |
| 地图 | Tiled 有限正交 `.tmj`、内嵌/外置图集、图块层与对象层、运行时编辑；兼容 ASCII 地图 |
| 脚本 | 项目内 `require` 缓存、严格字段校验、独立 VM、Lua 控制器与动画片段 |
| 状态 | 深拷贝跨房间数据、版本化 JSON 检查点、原子替换、显式迁移函数 |
| 图像 | PNG 图集、旋转/翻转、整数倍像素缩放、图层、相机、粒子与近似点光源 |
| 音频 | WAV 音效 32 声部、Ogg Vorbis 音乐 2 流、音量/音高/循环/暂停/淡入淡出；保留合成音调 |
| 文字 | 声明 TTF/OTF 字库、UTF-8、回退、换行/对齐；无窗口与图形共用度量 |
| Agent 工作流 | `--api`、LuaLS 注解、`--check-all`、脚手架、回放、截图、自定义游戏打包 |
| 可选网络 | ENet 原生 UDP、可靠/状态通道、Lua 会话、主机权威双人示例；默认关闭 |

## 文本优先的开发流程

```sh
python tools/new_game.py ../my-game
./build/shiny --api
./build/shiny --check-all ../my-game
./build/shiny --headless ../my-game --frames 180 --replay ../my-game/smoke.replay
```

`project.lua` 声明项目 ID、入口、房间、资源和存档版本；省略时默认读取 `main.lua`。场景返回包含 `init/update/draw` 的表。`sc.get(id)` 返回副本，使用 `sc.set(id, patch)` 提交修改。`draw` 只排入绘制指令。

- [API 与 LuaLS 类型](docs/api.lua)
- [Agent 开发指南](docs/llm-guide.md)
- [架构与运行语义](docs/architecture.md)
- [Workshop / Tiled 工作流](examples/workshop/README.md)
- [联机协议与 API](docs/networking.md)
- [本次验证与限制](docs/verification.md)

无窗口运行默认把存档放在内存中；测试磁盘恢复需显式指定 `--save-dir DIR`。图形运行默认写入系统用户数据目录。`--check` 和 `--check-all` 不执行 update，也不允许写入或读取检查点。

```sh
cmake -S . -B build-headless -DSHINY_GRAPHICS=OFF -DSHINY_NETWORK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless --parallel
ctest --test-dir build-headless --output-on-failure
./build-headless/shiny --headless examples/workshop --frames 200 \
  --replay examples/workshop/replays/smoke.txt --save-dir ./test-saves
```

回放每行是 `frame mask`，帧从 0 开始，输入保持到下一条记录。左/右/上/下/跳跃/互动的掩码分别为 1/2/4/8/16/32。帧号跨房间连续，场景 `sc.tick()` 重新计数。

## 发行包

```sh
python tools/package.py build/shiny dist/MyGame --project examples/workshop
```

Windows 生成 BAT 启动器，Linux 生成 shell 启动器，macOS 生成 `.app`；均包含游戏、文档、许可和 ZIP，拒绝覆盖已有目标。不传 `--project` 时打包 Lantern；`--with-network-examples` 需要网络开启的引擎。包对应构建它的系统与 CPU，签名、公证和商店发行流程由项目负责。

## 范围与兼容性

0.2 没有自建关卡 GUI；关卡编辑使用 Tiled 的明确子集。没有复杂文字塑形、任意 Lua VM/物理世界快照、完整刚体编辑器或主机平台导出。移动平台和跳跃手感由 Lua 控制器处理。光照遮挡仍使用 ASCII 实心格，不包含 Tiled 多边形和动态身体。

单世界限制：256 实体、16384 格地图、16 图块层、1024 粒子、512 绘制指令、64 纹理。Lua 16 MiB，每回调 100 万条指令；显式状态最多 256 KiB、深度 16。脚本和原生资源必须可信，这些限制不是安全沙箱。

同一二进制、平台、种子和回放可用于行为回归；不承诺浮点跨架构逐位一致。`hash`/`state_hash` 是诊断摘要，不是完整恢复格式。Box2D 替代 0.1 碰撞器，数值轨迹可能变化；旧 `dynamic` 字段作为兼容写法保留，新项目使用 `body`。

C11 与 C++23 的 0.1 标签保持不动；[历史迁移记录](docs/verification-v0.1.md) 的体积和验收结果不代表 0.2。第三方与字体许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。
