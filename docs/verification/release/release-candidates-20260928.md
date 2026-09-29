# Windows 发行候选包移动检查（2026-09-28）

使用当前完整构建为 Crossing、Wayfarer、Barrage 分别生成自包含目录与 ZIP：
`build/final-candidates-20260928/`。三份 ZIP 解压到系统临时目录、源码树之外，
从解压目录运行包内引擎，PATH 仅含 Windows 系统目录。各自 `--check-all`
通过，并分别运行 435、300、180 帧无窗口原样回放；结束场景均为 `main.lua`。
三款完整任务流程另由源码项目的集成测试验证；上述短回放只检查搬移后的启动、
运行资源及依赖闭包，不取代完整流程或图形像素验收。

| 包 | ZIP 字节 | SHA-256 |
| --- | ---: | --- |
| `crossing.zip` | 5,523,032 | `6b8be7ba5f10f4e15eb6cbfe06c8e4f273264cbc8b2586651ef7746b4ce45e41` |
| `wayfarer.zip` | 17,936,955 | `d85d8c1ad1122ac0478b3b626a81475cc71a0e3229782d93df002e16b20deddd` |
| `barrage.zip` | 4,732,276 | `f6fe9c103d9c4e12ae3d74218f03f0ff9a077ae47a508beadac767e0ee0ac707` |

包内引擎每份 6,760,448 字节。测试使用的是本机 Windows 环境，不声称干净
系统安装、实体音频/手柄或 Linux/macOS 发行验收。验证用的临时解压目录
位于系统 Temp；自动安全审查拒绝了递归清理命令，因此未自动删除。
