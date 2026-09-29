# Lua 快照插值验证 · 2026-09-23

新增 `lua/shiny/snapshot.lua`、自包含 `examples/snapshot` 和
`tests/snapshot_integration.py`。模块预分配有界快照/对象存储，采样复用调用方
输出；权威状态与显示数据分离。API 和能力边界见 [快照契约](../../network/snapshots.md)。

## 实际执行

- 真实受限 Lua VM：uint32 回绕、重复与乱序、非递增 tick、半圈歧义、
  稀疏/超容量/重复 ID/未知字段/NaN/Inf 拒绝，失败后旧时间线不变。
- 精确中间位置、出生/消失边界、输入/输出修改不影响缓存、环形覆盖、
  清空重用，以及 600 帧按 20 Hz 提交、60 Hz 采样。
- 示例在 220 帧断流保持，300 帧恢复插值；模块副本与标准库原件一致。
- Windows GNU `build/full`、网络关闭的 `build-dev`：各自
  `snapshot_integration` 和 `tools` 两项 CTest 全部通过。
- LLVM-MinGW 22.1.8 无窗口 ASan/UBSan：直接运行快照集成脚本通过；
  复用已验证的原生二进制，本轮没有 C++ 修改。
- 实际原生运行 150 帧并捕获 PNG；已目视检查三条轨道、文字与位置显示。
  相同输入回放的无窗口运行与图形运行，最终显式 snapshot watch 完全一致。

截图与 JSON 位于 `build/verification-snapshots-2026-09-23/`，截图来自原生
渲染，不是无窗口生成。复现：

```sh
python tests/snapshot_integration.py build/full/shiny.exe
build/full/shiny examples/snapshot --frames 150 --replay examples/snapshot/smoke.txt \
  --capture /absolute/path/native.png
```

## 未完成项

示例是离线传输模拟，没有真实联网玩法或服务器时钟估计；不能以此替代四人
主机权威示例、跨房间快照、重连令牌和弱网图形验收。没有宣称预测、回滚、
外推或完整状态复制。Linux/macOS 和长期运行本轮未验收。
