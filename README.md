# ShinyCore

**小型原生 2D 引擎 · C++23 + Lua · 为人和 LLM Agent 共同开发设计**

当前工作树为 **1.0 开发中版本，尚未完成完整版验收**。新增系统与限制见 [开发状态](docs/implementation-status.md) 和 [新增接口](docs/new-systems.md)。
性能诊断使用 `--profile` 和 [采样报告工具](docs/profiling.md)，区分 CPU 处理、GPU 时间与限帧等待。

ShinyCore 提供固定 60 Hz 模拟、Box2D 刚体、Tiled 地图、跨房间数据与磁盘检查点、精灵图集、音频文件和中英文字体。游戏就是一个可读、可回放的 Lua 项目；角色控制、动画、收集和关卡规则保持在普通 Lua 模块中。

## 构建与运行

需要 CMake 3.24+、支持 C++23 与 `std::expected` 的工具链，以及首次获取依赖时的网络。Lua 5.4.9、Box2D 3.1.1、yyjson 0.12.0、raylib 5.5 与字体实现均固定版本并校验；无窗口构建不下载 raylib。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/shiny examples/workshop
```

Windows 可使用现代 MSVC、GCC/MinGW，或 LLVM-MinGW + Ninja。 Windows 运行目标为 10 1903 或更新版本；可执行文件内嵌 UTF-8 进程清单，命令行及文件路径支持中文等 Unicode 字符。Visual Studio 多配置构建使用 `cmake --build build --config Release`，程序在 `build/Release/shiny.exe`。更换编译器时使用新的构建目录。Linux 图形构建需要 GLFW 对应的 OpenGL/X11 开发库，CI 提供安装配置。

也可使用 Ninja 预设：`cmake --preset lightweight`、`cmake --build --preset lightweight`、`ctest --preset lightweight`。另有 `full` 与 `headless` 预设。`full` 请求全部模块。高级渲染已提供独立的 [材质/片元着色器能力](docs/materials.md)及[最多四 pass 后处理链](docs/postprocess.md)，附 Bloom、调色、扭曲示例；[几何遮挡与软阴影](docs/lighting.md)已接入，[法线贴图](docs/normal-maps.md)已接入，已完成[定向原生视觉检查](docs/verification-advanced-render.md)，完整 GPU 验收仍待完成；开发工具提供 `--debug-stdio` 协议，支持帧步进、Lua 行断点、步入/步过/步出及调用栈、局部变量和显式状态分页检查；并可用 `--debug-keys` 下的 F4 打开原生检查面板（Agent 可通过 panel 命令选择页面；已有隐藏截图检查，实体按键待验收），详见 [调试协议](docs/debug-stdio.md)。`--api` 报告构建模块，连接后的 ready 事件列出已实现命令。运行开发工具和测试需 Python 3.11+，图集构建和截图检查依赖 `python -m pip install -r tools/requirements.txt`；发行游戏不需要 Python。

Windows 已使用 LLVM-MinGW 22.1.8 实测 ASan/UBSan；[检测结果与复现命令](docs/sanitizer-verification.md)包含运行库 PATH 和遇错退出设置。

Workshop 展示中文 UI、Tiled 图层、箱子、斜坡、移动平台、Lua 动画和存档。A/D 移动，Space 跳跃，Down+Space 穿透单向平台，E 保存，Up 读取，走到右端切换房间。原创 Lantern 和可选联机示例 Duet 仍保留。

`shiny examples/tween` 演示普通 Lua 数值的顺序、并行、等待和取消；固定更新推进，
契约及定向验证见[补间组合](docs/tween.md)。

`shiny.animation` 提供[精灵序列、速度与入帧事件](docs/animation.md)，Barrage 的
角色和敌人已接入；动画数据为普通 Lua 表，推进发生在固定更新中。

完整构建的 182 个注册函数现均带结构化调用契约，见[核心 API 语义](docs/core-api.md)。
复杂资源格式与开放数据字典仍需结合各模块文档；这不是完整版验收声明。

[应用与共享状态契约](docs/application-state.md)说明暂停、设置补丁、跨房间状态、
观察数据和模块缓存的行为；这些调用的类型与阶段也可通过 `--api` 获取。

批量弹体的[字段、容量与调用阶段](docs/projectiles.md)可直接从 `--api` 读取，
并同步生成项目内 LuaLS 注解；批量错误保留原有弹体与序号。

实体可通过 `sc.presentation.attach` 建立[原生视觉附着](docs/attachments.md)，
支持物理更新后自动跟随和嵌套旋转插值；对象持久引用与存档仍使用显式数据。
运行 `shiny examples/attachments` 可体验保存、解除附着、重置和读档；示例通过
对象持久 ID 重建关系，`sc.spawn_many` 同时预检实体与父子关系。

`shiny examples/snapshot` 展示 20 Hz 位置快照的延迟插值、丢包和断流保持；
它是无需网络的可视化示例，模块契约见 [快照插值](docs/snapshots.md)。

`shiny examples/constellation` 是[四人主机权威协作示例](examples/constellation/README.md)，
覆盖 20 Hz 快照插值、跨房间及临时令牌重连，需要启用网络模块。

默认所有键交给游戏，关闭窗口退出。显式使用 `--debug-keys` 后启用 F1 统计、F2 实体边界、F3 光照、F5 重载、P 暂停、O 单步、Esc 退出。F5 和切换房间会重建 VM 与世界，继承显式 `sc.state`；候选脚本、资源或窗口尺寸校验失败时，图形运行保留旧场景并报告错误。

样例视觉检查可使用 `python tools/capture_samples.py build/full/shiny.exe --output build/captures --case wayfarer-dialogue`。
它通过 `--capture-hidden --capture FILE.png --frames N --mute` 生成真实原生截图，窗口保持隐藏且不获取焦点；
仍需要图形驱动/显示环境，不是无窗口模拟。输出目录须为新目录；检查范围及限制见[视觉记录](docs/verification-sample-visuals.md)。

## 当前能力

| 子系统 | 基础能力 |
| --- | --- |
| 模拟 | 固定 60 Hz、输入边沿、种子随机数、回放和 JSON 诊断摘要 |
| 物理 | 统一 Box2D；静态/运动学/动态身体，矩形/圆/胶囊/凸多边形及最多 4 个复合形状 |
| 交互 | 刚体推挤、斜坡、单向平台、传感器、真实形状重叠/平移扫掠/射线、过滤、距离/转轴/竖直滑动关节 |
| 地图 | Tiled 有限正交 `.tmj`、内嵌/外置图集、图块层与对象层、运行时编辑；兼容 ASCII 地图 |
| 脚本 | 项目内 `require` 缓存、严格字段校验、独立 VM、Lua 控制器与动画片段 |
| 状态 | 深拷贝跨房间数据、[存档槽](docs/saves.md)、原子替换与有效备份；严格版本匹配，不接受旧格式 |
| 设置 | 窗口/无边框、整数/平滑缩放、VSync、四路音量、动作绑定；独立持久化及应用失败回退 |
| 图像 | PNG 图集、旋转/翻转、图层、[相机跟随/缩放/旋转/震动](docs/camera.md)、[显示插值](docs/presentation.md)、粒子与光照 |
| 音频 | WAV 音效 32 声部、Ogg Vorbis 音乐 2 流、音量/音高/循环/暂停/淡入淡出；保留合成音调 |
| 文字 | 声明 TTF/OTF 字库、UTF-8、回退、换行/对齐；无窗口与图形共用度量 |
| Agent 工作流 | `--api`、LuaLS 注解、`--check-all`、脚手架、回放、截图、自定义游戏打包 |
| 可选网络 | ENet 原生 UDP、可靠/状态通道、应用级会话、双人及四人主机权威示例；默认关闭 |

`shiny examples/ui_panels` 展示[九宫格皮肤与九点锚定](examples/ui_panels/README.md)，
只需默认轻量构建；图片边角保持尺寸，UI 仍由普通 Lua 数据描述。

## 文本优先的开发流程

```sh
python tools/new_game.py ../my-game
./build/shiny --api
./build/shiny --check-all ../my-game
./build/shiny --headless ../my-game --frames 180 --replay ../my-game/smoke.jsonl
```

`project.lua` 声明项目 ID、入口、房间、资源和存档版本；省略时默认读取 `main.lua`。场景返回包含 `init/update/draw` 的表。`sc.get(id)` 返回副本，使用 `sc.set(id, patch)` 提交修改。`draw` 只排入绘制指令。

- [API 与 LuaLS 类型](docs/api.lua)
- [Agent 开发指南](docs/llm-guide.md)
- [架构与运行语义](docs/architecture.md)
- [Workshop / Tiled 工作流](examples/workshop/README.md)
- [Aseprite PNG＋JSON 动画导入](docs/aseprite.md)
- [Tiled JSON 地图与外部 TSX 图集](docs/tiled-tsx.md)；[JSON/XML 对象模板](docs/tiled-templates.md)；[地图与组属性](docs/tiled-map-properties.md)；[跨块对象覆盖](docs/stream-object-coverage.md)
- [声明式流式地图索引与固定帧提交](docs/streaming.md)
- [联机协议与 API](docs/networking.md)
- [当前开发树验证与限制](docs/verification-complete-dev.md)

无窗口运行默认把存档放在内存中；测试磁盘恢复需显式指定 `--save-dir DIR`。图形运行默认写入系统用户数据目录。`--check` 和 `--check-all` 不执行 update，也不允许写入或读取检查点。

```sh
cmake -S . -B build-headless -DSHINY_GRAPHICS=OFF -DSHINY_NETWORK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless --parallel
ctest --test-dir build-headless --output-on-failure
./build-headless/shiny --headless examples/workshop --frames 200 \
  --replay examples/workshop/replays/smoke.txt --save-dir ./test-saves
```

回放每行是 `frame mask`，帧从 0 开始，输入保持到下一条记录。左/右/上/下/跳跃/互动的掩码分别为 1/2/4/8/16/32。帧号跨房间连续，场景 `sc.tick()` 重新计数。

## 键盘与手柄

Lua 可通过 `sc.input` 查询桌面键盘、鼠标和最多四个手柄。例如 `sc.input.key_pressed("escape")`、`sc.input.gamepad_down("south")`、`sc.input.gamepad_axis("left_x")`。摇杆轴为 `[-1,1]`，扳机为 `[0,1]`，默认逐轴死区 `0.2`；第二参数传 `0` 可关闭死区。没有设备时返回中立值；拔出手柄会产生释放边沿。

旧六动作 API 继续有效，键盘和手柄可同时使用。新接口读取固定模拟帧快照，也支持新版 JSON Lines 无窗口回放。完整控件名见 `--api` 和 [Lua 注解](docs/api.lua)，格式及语义见 [输入说明](docs/input.md)。运行 `shiny examples/input` 可查看设备状态，或添加 `--replay examples/input/demo.jsonl --frames 125` 查看演示。

## 发行包

```sh
python tools/package.py build/shiny dist/MyGame --project examples/workshop
```

Windows 生成 BAT 启动器，Linux 生成 shell 启动器，macOS 生成 `.app`；均包含游戏、文档、许可和 ZIP，拒绝覆盖已有目标。不传 `--project` 时打包 Lantern；`--with-network-examples` 需要网络开启的引擎。包对应构建它的系统与 CPU，签名、公证和商店发行流程由项目负责。

打包时审计原生运行库；非系统库通过 `--runtime LIBRARY LICENSE` 显式携带，
缺失或架构不匹配时失败。报告分列二进制、运行库、资源、Lua 库和符号大小。
项目的 `package.json` 可显式选择房间、资源和动态模块；普通 Lua 依赖和流式地图块
自动收集，裁剪后再次检查包内项目。脚手架及三款样例已提供清单。
本地 `shiny-sdk.json` 记录标准模块版本、要求的引擎版本和文件哈希；
打包会拒绝未记录的修改，定制模块可显式登记为项目自己的 SDK 版本。
平台依赖及当前验证范围见 [发行依赖](docs/packaging.md)。

## 范围与兼容性

引擎没有自建关卡 GUI；关卡编辑使用 Tiled 的明确子集。没有复杂文字塑形、任意 Lua VM/物理世界快照、完整刚体编辑器或主机平台导出。移动平台和跳跃手感由 Lua 控制器处理。默认光照遮挡使用 ASCII 实心格；高级模块支持地图多边形与动态身体遮挡，原生显示仍待验收。

默认世界容量：4096 实体、16384 格内存地图、16 图块层、32768 粒子、4096 绘制指令、64 纹理。实体、粒子与绘制等容量可在 project.limits 配置。独立弹体系统仅在配置后分配。Lua 16 MiB，每回调 100 万条指令；显式状态最多 256 KiB、深度 16。脚本和原生资源必须可信，这些限制不是安全沙箱。

同一二进制、平台、种子和回放可用于行为回归；不承诺浮点跨架构逐位一致。`hash`/`state_hash` 是诊断摘要，不是完整恢复格式。Box2D 替代 0.1 碰撞器，数值轨迹可能变化；旧 `dynamic` 字段作为兼容写法保留，新项目使用 `body`。

C11 与 C++23 的 0.1 标签保持不动；[历史迁移记录](docs/verification-v0.1.md) 的体积和验收结果不代表 0.2。第三方与字体许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。
