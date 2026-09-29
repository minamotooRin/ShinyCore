# 功能开发结项核对（2026-09-28）

用户明确将原计划中过于复杂的正式性能、实体设备和长时验收改为**简单、必要的
验收**。本轮据此核对可运行功能、三款游戏流程、项目自包含、发行候选包及实际
画面；原严格指标保留为后续专项质量验证，不将未执行项目写成通过。

| 本次必要检查 | 当前证据 | 结果 |
| --- | --- | --- |
| 完整和轻量构建 | `cmake --build --preset full`、`cmake --build --preset lightweight` | 通过 |
| UI 改动与自包含项目 | 横向及原纵向滚动定向测试 4 项、SDK 工具检查、6 个受影响项目 `--check-all`；见[横向滚动记录](verification/systems/ui-horizontal-scroll.md) | 通过 |
| 三款样例完整自动流程 | `crossing_integration.py`、`wayfarer_integration.py`、`barrage_integration.py`，分别覆盖三房间/结局、24 草药及存档恢复、六波/升级/重开 | 通过；不据此声称真人 5–10 分钟或最终美术品质 |
| 实际原生画面 | 本轮[横向 UI 截图](verification/systems/ui-horizontal-scroll.md)已目视检查；三款样例的关键状态见[既有视觉记录](verification/games/sample-visuals.md) | 本轮受影响画面通过；实体设备与全程动画不在此次检查内 |
| 三款独立发行候选包 | ZIP 解压到源码目录外，PATH 仅保留 Windows 系统目录；各自 `--check-all` 和有界无窗口回放通过，详见[发行候选记录](verification/release/release-candidates-20260928.md) | 通过 |

原计划的正式五负载三次长测、CPU/GPU/资源预算、两小时循环，以及实体手柄、
输入法、音频和 Linux/macOS 当前提交的完整平台覆盖**尚未验收**，不属于用户
调整后的本次结项门槛。已知的严格弹幕长测首轮 CPU p99 为 17.16 ms，超过
原 16.67 ms 目标；其余四类固定负载尚缺，见[性能记录](verification/systems/projectile-performance.md)。
GitHub Actions 工作流已配置，但仓库 API 在本次查询时返回总运行数 0，
不能用配置代替实际跨平台结果。此前中文字体模糊问题已解决，不重复测试。
