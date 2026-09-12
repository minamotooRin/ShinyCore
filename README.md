# ShinyCore C11 comparison baseline

This branch preserves the validated C11 implementation. The modern C++23 implementation is developed separately. Both use the same Lua authoring model and Lantern replay. This baseline was recovered from this task's pre-migration source/patch history and rebuilt as C11; it is not a C++ build in compatibility mode.

# ShinyCore

**小型原生 2D 引擎 · C11 内核 · Lua 游戏规则 · 为人和 LLM 共同编程设计**

ShinyCore 0.1 是一个可以编译、游玩、修改和自动验证的引擎首版。它以像素游戏为中心：固定时间步、精确瓦片碰撞、受控资源生命周期、可读的脚本和很短的反馈循环。随附原创探索小游戏 **LANTERN / THE QUIET BELOW**：携带灯笼进入洞穴，找回三枚灯火，打开通往记忆室的门。

设计受到《动物井》开发者关于专用引擎、像素渲染和快速迭代的[公开介绍](https://blog.playstation.com/2022/07/20/how-animal-well-taps-into-ps5-hardware-to-elevate-2d-pixel-art-platforming/)启发。本项目独立实现，没有使用该游戏的代码或素材，也不宣称具备其商业引擎的全部能力。

## 开始游玩

需要 CMake 3.24+、C11 编译器和首次构建时的网络。Lua 5.4.9 与 raylib 5.5 使用固定版本、SHA-256 校验并静态链接；无需安装 Lua、Python 包或游戏编辑器。macOS 需要 Xcode Command Line Tools。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/shiny examples/lantern
```

方向键 / A D 移动，Space / Z 跳跃，E / X 互动；支持标准手柄的方向键、左摇杆及底部/右侧面键。F1 显示统计，F2 显示碰撞框，F3 切换光照，F5 重新加载当前房间，P 暂停，O 单步，Esc 退出。

F5 从磁盘重新创建当前场景和 Lua 状态。配置、脚本、缺失资源校验失败时保留正在运行的场景并显示错误；图形构建还会预检图片解码与精灵网格。当前窗口的逻辑尺寸不能通过热重载改变。

Windows 使用 `cmake --build build --config Release` 和 `build/Release/shiny.exe`。Linux 原生窗口构建需要 raylib 的 GLFW/X11 或 Wayland 开发依赖，具体见其[官方构建说明](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux)。无需图形环境的构建见下文。

## 已实现

| 子系统 | 当前能力 |
| --- | --- |
| 模拟 | 固定 60 Hz、输入按住/按下/释放、可设置种子的随机数、稳定状态摘要 |
| 世界 | 纯文本瓦片、实体代际 ID、标签查询、实体重叠、房间切换 |
| 碰撞 | 矩形实体与实心瓦片、单向平台、高速轴向扫掠、稳定接地 |
| 图像 | 像素对齐、整数倍缩放与留边、PNG 精灵图集、脚本动画、图层、跟随相机 |
| 效果 | 有遮挡的动态点光源、环境光、粒子、世界/屏幕绘制指令 |
| 声音 | 44.1 kHz 合成音调、16 个播放声部、可关闭音频设备 |
| 迭代 | F5 场景重载、暂停/单步、统计、碰撞框、原生截图 |
| 自动化 | 无窗口运行、输入重放、JSON 状态/错误、API 自描述、项目脚手架 |
| 联机（可选） | 原生 UDP、可靠消息与状态通道、Lua 会话、主机权威双人示例、真实时间无窗口运行 |

游戏规则保留在 Lua 中；示例的跳跃缓冲、coyote time、收集物、开门条件和精灵动画均不在 C 内核中硬编码。

## 为 LLM 编程准备的工作流

从一个小项目开始，每次修改都能给出程序可检查的结果。

```sh
python3 tools/new_game.py /tmp/my-shiny-game
./build/shiny --api
./build/shiny --check /tmp/my-shiny-game
./build/shiny --headless /tmp/my-shiny-game --frames 300 --seed 42 \
  --replay /tmp/my-shiny-game/smoke.replay --snapshot /tmp/game-state.json
./build/shiny /tmp/my-shiny-game
```

- [LLM 编程指南](docs/llm-guide.md)：推荐修改流程、常见错误、验收方式。
- [完整 Lua API 与类型](docs/api.lua)：LuaLS 可读的参数、返回值、默认值和限制。
- [架构与语义](docs/architecture.md)：固定更新顺序、资源边界、确定性的适用范围。
- [示例说明](examples/lantern/README.md)：源码位置、房间和可重复的游玩路线。
- [AGENTS.md](AGENTS.md)：后续编程代理的入口。

一个项目就是一个包含 `main.lua` 的目录。入口返回一张场景表，`init()` 初始化、`update(dt)` 修改世界、`draw(alpha)` 排入绘制指令。`sc.get(id)` 返回副本，用 `sc.set(id, patch)` 提交明确修改；未知字段、错误动作名、失效 ID 和非法数值会直接报错。

## 无窗口验证

```sh
cmake -S . -B build-headless -DSHINY_GRAPHICS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless --parallel
ctest --test-dir build-headless --output-on-failure
./build-headless/shiny --headless examples/lantern --frames 430 \
  --replay examples/lantern/replays/tour.txt --seed 42
```

无窗口构建不下载或链接 raylib，不创建窗口、GPU 上下文或音频设备。它执行同一套 Lua 更新和 C 模拟；`--check` 检查入口配置、`init()`、初始 `draw()` 和精灵路径。图片解码和网格预检只在图形构建中提供。后续动态分支需要实际回放覆盖。

回放文本每行是 `frame mask`，帧号从 0 开始递增，输入保持到下一条记录，`#` 开头可写注释。掩码：左 1、右 2、上 4、下 8、跳跃 16、互动 32，可相加。全程帧号跨房间连续；`sc.tick()` 每个新房间从 0 开始。

验证路线在第 150 / 220 / 330 帧依次完成收集，第 430 帧位于记忆室，第 480 帧返回起点。测试断言实际位置和场景行为。JSON `hash` 是 C 世界的诊断摘要，不包含任意 Lua 局部变量，也不是可载入的存档。

## 打包

```sh
python3 tools/package.py build/shiny dist/ShinyCore
```

生成可搬移目录与 ZIP，附带示例、文档和依赖许可；已有目标会拒绝覆盖。macOS 包含可以双击的 `ShinyCore.app`。打包产物面向生成它的操作系统和 CPU 架构。开发签名不等同于 Apple 公证；面向其他用户公开分发时需自行完成平台发行流程。

## 范围与边界

这是面向小型像素游戏、可继续扩展的 **0.1 内核**。目前没有关卡 GUI、磁盘存档恢复、完整音频资源 API、复杂刚体、斜坡、动态实体互相推挤、多语言字体或主机平台导出。房间切换会重建 Lua 与世界；跨房间持久状态尚未提供。精灵动画由脚本选择帧，地图使用内置程序化瓦片风格。

原生网络默认 `SHINY_NETWORK=OFF`，启用时添加 `-DSHINY_NETWORK=ON`，运行 `examples/duet` 即可体验双人联机。网络模块独立于模拟内核，支持局域网与可达 IPv4 的公网直连；API、协议、构建和打包说明见 [networking.md](docs/networking.md)。本页的基础引擎体积、截图和打包验收均使用关闭网络的配置。

固定容量可在 `include/shiny/core.h` 调整：256 个实体、16,384 个瓦片、1,024 个粒子、每帧 512 条自定义绘制指令。原生后端最多缓存 64 张纹理，每帧绘制前 32 个可见点光源。Lua 有 16 MiB 分配上限和每回调 100 万条指令预算；这些是编程防错措施，不是运行敌对脚本或资源的安全隔离。

本机已验证 macOS arm64 的图形、音频初始化和无窗口执行；其他平台的 CI 配置已提供，尚不能代替实际运行结果。体积与详细验收记录见 [verification.md](docs/verification.md)。

ShinyCore 与原创示例采用 MIT；第三方许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。
