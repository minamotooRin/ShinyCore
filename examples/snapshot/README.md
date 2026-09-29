# Snapshot interpolation

```sh
build/full/shiny examples/snapshot
build/full/shiny --headless examples/snapshot --frames 400 --replay examples/snapshot/smoke.txt
```

三行分别显示权威位置、最近收到的位置和延迟插值位置。离线模拟 20 Hz 快照、
4..6 帧延迟、定期丢包和每六秒一次的一秒断流。断流时显示停在最后快照，
不预测或外推。它是插值机制演示，不会打开网络套接字。

`lib/shiny/snapshot.lua` 是可随项目移动的标准模块副本。真实四进程弱网验证
见 `tests/test_net_proxy.py`；模块契约见 `docs/network/snapshots.md`。
