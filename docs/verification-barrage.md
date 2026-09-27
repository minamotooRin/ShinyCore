# 组合弹幕基准初测 — 2026-09-24

**CPU 未达标；尚未执行正式 30 秒预热、180 秒测量、三次重复验收。**

新增 `benchmarks/barrage`：2000 个移动敌人共用流场，非穿透弹体真实扫掠并
按命中目标累计伤害，粒子启用速度、寿命和尺寸/RGBA 曲线。固定回放持续补充
至 24000 弹体、21000 粒子，以覆盖帧内死亡；模拟后计数仍须 ≥20000。
工具 `tools/benchmark.py` 提供短测和正式三进程模式，检查数量、实际命中率、
CPU/GPU 时间、进程内存及 1920×1080 截图。输出目录必须不存在，保留失败证据。

本地 CIM 读到 i7-8700、GTX 1060 3 GB，物理内存 17087016960 字节，桌面
1920×1080；原生 profile 确认 renderer 为 GTX 1060，而不是同时存在的 UHD 630。
图形构建为 GNU Release。普通 1080p 窗口被窗口管理器拒绝后，改为无边框
桌面模式；没有更改 OS 分辨率。实际原生截图已查看。

| 短测 | CPU p50 / p99 | GPU p99 | 结论 |
| --- | --- | --- | --- |
| 初始组合负载 | 14.69 / 28.27 ms | 4.00 ms | CPU 失败 |
| 保守范围排除后，基准工具短测 | 12.14 / 23.65 ms | 4.97 ms | CPU 仍失败 |

这些是 2 秒预热后选取 2 秒样本的诊断值，存在运行间波动，不是正式性能结论。
后一报告最小活动量为 2000 敌人、23219 弹体、20727 粒子；每完整壁钟秒的
实际命中率超过 1000。峰值进程驻留约 113 MiB。图形资源分配预算仍未计量。

优化仅在粗网格候选目标进入精确圆形扫掠前，使用已算好的扫掠包围范围排除
不可能相交项。边界保持包含相切情况；没有改变负载脚本、命中规则或采样数量。
优化前后 360 帧原生回放最终 JSON 完全一致。完整、轻量、ASan/UBSan 的
core、systems、features、complete_integration 回归测试通过，包括新增精确相切
与邻近未命中测试。

本地证据位于 `build/full/barrage-borderless-report.json`、
`build/full/barrage-bounds-report.json`、`build/full/barrage-runner-smoke/report.json`。
后续重点仍是弹体候选处理、Lua 批量边界和绘制提交的 CPU 耗时。
其余四类性能负载、Linux/macOS 平台、两小时耐久不在本记录范围。

## Result screen follow-up

Victory and defeat now have distinct headings, score/kills, elapsed time and seed.
`last_result` stores explicit summary data (sample data version 3), not live combat.
The focused integration check passes cross-process result restore and starting a
fresh playable challenge. Hidden native restored victory/defeat captures were
actually inspected in `build/sample-endings-reviewed/`; text and focus fit within
the panel. Audio and physical input remain unverified. See
[visual evidence](verification-sample-visuals.md) for the bounded capture workflow.
