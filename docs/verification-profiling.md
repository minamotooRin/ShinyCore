# 性能采样基础验证 · 2026-09-21

范围：W0 的 CPU/GPU 计时、宿主帧控制、容量/进程内存统计和报告工具。
完整版尚未完成。基线 `1a91a4c`；本记录对应后续工作树改动。

| 验证 | 本机结果 |
| --- | --- |
| GNU 16.1 Release，图形开、网络/流式关，`build-dev` | CTest 14/14 通过 |
| GNU 16.1 Release，`build/full` | CTest 17/17 通过；高级渲染/调试器能力仍为 false |
| GNU 16.1 Release，无窗口、网络/流式开 | CTest 17/17 通过 |
| LLVM-MinGW 22.1.8，无窗口、网络/流式开，ASan/UBSan | CTest 17/17 通过，遇错退出开启 |
| 原生设置菜单 | 修改分辨率、缩放、VSync、音量与绑定；回放断言通过，1280×720 截图已检查 |
| 原生 GPU 计时 | OpenGL 查询可用；30 帧得到 28 个结果、2 个尾部待返回，无同步排空 |
| 回放与采样隔离 | 图形/无窗口相同回放的玩法字段一致；headless 限帧开关不改变状态；原生采样开/关的截图字节相同 |
| 报告故障测试 | 截断、重复 GPU 记录、NaN、错帧号、错误总数、缺样、时长不足、超限与不足负载均拒绝 |

CPU 实机为 i7-8700；采样头确认实际 renderer 为
`NVIDIA GeForce GTX 1060 3GB/PCIe/SSE2`。内存读取来自 Windows OS，未以 Lua
分配量代替进程内存。Linux/macOS 分支与原生 CI 检查已加入，尚未执行验收。
GitHub API 对基线 SHA 的 Actions 查询返回 0 个运行，不能据此声称远端通过。

## 可复现命令

```sh
cmake --build build/full --parallel
ctest --test-dir build/full --output-on-failure
python tests/test_profile.py build/full/shiny.exe --native
python tests/settings_integration.py build-dev/shiny.exe . --native --capture artifacts/profiling/settings.png
```

Sanitizer 使用 [现有 Windows 环境配置](sanitizer-verification.md)，在
`build-llvm-sanitizers` 重新构建并执行全套 CTest。测试包含真实 ENet 会话，
不代表四人协议或弱网验收已经完成。

一次 180 tick Lantern 工具短测保存到本机 `artifacts/profiling/`：
`native.jsonl`、`native-state.json`、`report.json`、`lantern.png`、`settings.png`。
截图来自实际渲染；目视检查通过。截取预热 0.5 秒后 1 秒的 60 个样本：

| 指标 | p50 | p95 | p99 |
| --- | ---: | ---: | ---: |
| CPU ms | 0.6863 | 1.0711 | 1.7473 |
| GPU ms | 0.121856 | 0.392192 | 0.681984 |
| 等待 ms | 15.8458 | 15.9895 | 16.0359 |

该区间 GPU 无缺样；180 帧总共返回 178 个 GPU 结果，2 个待返回，跳过 0。
进程驻留峰值 83,992,576 字节。此次窗口为 1152×648，普通 Lantern 负载，
不是 1080p 压力负载，也没有执行 30 秒预热、180 秒采样、三次重复。
以上仅证明计时/等待区分与报告管线可工作，不能用于完整版性能达标声明。

原生分配次数、实际 GPU draw call、图形资源字节数、五类压力场景、两小时
运行、实体手柄与输入法，以及 Linux/macOS 实机结果均仍待验收。
