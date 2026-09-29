# 性能采样

`--profile FILE` 默认关闭，文件已存在时拒绝覆盖。采样不会启用 trace、状态
hash 或 Lua 调试钩子。以下是工具自检，不是完整版性能验收：

```sh
./build-dev/shiny examples/lantern --mute --frames 180 --replay examples/lantern/replays/tour.txt --profile artifacts/profile.jsonl
python tools/profile_report.py artifacts/profile.jsonl --runs 1 --warmup 0 --duration 0
```

先创建输出目录。`--frames` 是模拟 tick 数，不能替代墙钟采样时间。标准报告
默认要求三个独立文件、预热 30 秒、测量 180 秒；录制应超过 210 秒，并额外
留出尾部帧以收取延迟 GPU 查询。不要同时开启 trace/record。示例阈值：

```sh
python tools/profile_report.py run1.jsonl run2.jsonl run3.jsonl --cpu-p99-max 16.67 --gpu-p99-max 16.67 --rss-max-mib 512 --minimum entities=2000 --minimum projectiles=20000 --minimum particles=20000 --min-hits-per-second 1000
```

这些参数只检查指定计数，不能证明“敌人正在更新”“所有对象位于视口内”等
游戏语义。完整压力场景还必须自己断言这些条件。工具不会将普通示例认定为
压力验收场景；五类完整负载仍待实现。流式负载另用 `--cpu-max 33.3`。
命令输出 JSON；检查失败、记录损坏或时长不足均返回非零。

## JSON Lines v1

- `header`：引擎版本、是否图形运行、GPU 计时能力、实际 GL renderer 名称、
  是否同时录制输入/trace。无 GPU 数据时明确报告 `headless` 或 `unsupported`。
- `frame`：从零开始的宿主帧号、累计模拟 tick、实际执行的 `steps`、
  `elapsed_ms`（本帧开始距采样开始）、`wall_ms`、`cpu_ms`、`wait_ms`。
  多次模拟追帧的时间合计在同一宿主帧；暂停显示帧可以有零个 tick。
- `gpu`：源宿主帧号及 `gpu_ms`，允许延迟出现。工具按源帧选择采样区间。
- `end`：总宿主帧、已返回 GPU 样本、尾部尚未返回、查询池满而跳过的数量。
  缺失 end 视为不完整记录，不能作为通过证据。

`cpu_ms = wall_ms - wait_ms - diagnostic_ms`。CPU 包含输入、脚本、模拟、
正常资源加载/房间提交、渲染提交及输入事件轮询。`wait_ms` 是交换画面/
VSync、限帧与 headless realtime 等待。`diagnostic_ms` 是 trace 与输入录制
写出。容量扫描、进程内存采样和 profile 本身的写出在被计时帧之后执行；
它们仍会影响墙钟吞吐量。调试采样有开销，不承诺零扰动。

`phases` 分开记录 update/draw 脚本、模拟、物理、普通实体推进、弹体、粒子、
音频、房间事务、绘制提交和输入轮询。物理/实体/弹体/粒子是 simulation 的
子项，不应与 simulation 再相加。room 包含候选加载、校验与资源准备，不是
细分的文件读取/GPU 上传统计。初始化在测量开始前完成。

GPU 使用 OpenGL `GL_TIME_ELAPSED` 和固定 8 槽查询池；只读已可用的结果，
不使用 `glFinish`，不等待查询，结束时也不强制排空。范围包括该帧的目标
纹理绘制和最终屏幕批次，排除 swap/VSync。要求 GPU 阈值时，测量区间任何
缺样都判失败，不能填零或以 CPU 时间替代。

## 计数和限制

实体、弹体、粒子及绘制队列均记录实际数量和容量。`draw_commands` 是 Lua
队列长度，**不是** GPU draw call。命中累计数属于当前房间；命中率检查遇到
重置会失败，每个完整一秒窗口检查实际命中增量。默认统计分位数采用 nearest
rank；每次运行独立报告 p50/p95/p99/max，不合并三个运行来掩盖失败。

`resident_bytes` 与 `peak_resident_bytes` 来自 OS；后者是进程启动以来的
驻留内存峰值，包含初始化。Windows 使用 WorkingSet/PeakWorkingSet，Linux
使用 statm/getrusage，macOS 使用 task_info/getrusage。它们不是全部已提交
虚拟内存，也不是 GPU 内存。无法读取时为 null。Lua 当前分配另列 `lua_bytes`。
原生分配次数、实际 GPU draw call 与图形资源字节数仍为 null，尚未实现。
不能据此宣称通过完整内存、分配或 GPU 资源预算验收。

本轮 Windows 实测见 [验证记录](../verification/systems/profiling.md)。Linux/macOS
实现与 CI 调用已加入；未在本机执行的平台不算验收完成。
