# 弱网测试代理

`tools/net_proxy.py` 是仅依赖 Python 标准库的开发工具。它转发 UDP 数据报，
不解析 ENet 或游戏协议，不进入发行游戏。默认监听本机随机端口；启动后第一行
JSON 给出实际 `listen` 地址。

```sh
python tools/net_proxy.py --target 127.0.0.1:7777 --listen 127.0.0.1:7778 \
  --rtt-ms 150 --jitter-ms 30 --loss .05 --duplicate .03 --seed 734 \
  --duration 60 --report proxy-report.json
```

主机监听 7777，所有测试客机连接代理 7778。每个客机地址对应独立上游 UDP
套接字，主机可识别多个 peer。地址必须为数字 IPv4；停止后释放全部套接字。
`--duration 0` 表示运行到 Ctrl+C；正常停止输出统计，也可写入 `--report`。

## 注入语义

- RTT 配置分摊到两个方向；每方向独立延迟 `RTT/2 ± jitter/2` 毫秒。
  因此配置 150/30 表示每方向 60..90 ms，不计操作系统和实际网络开销。
- 每个收到的数据报独立进行丢包判定；未丢失的数据报再进行重复判定。
  重复副本各自采样延迟，允许乱序。概率作用于 UDP 数据报，不是应用消息。
- 固定种子和相同输入数据报顺序会产生相同注入决策；进程调度和 ENet 重传
  可能改变输入顺序。工具不声称可重放实际网络到达时间。
- 默认最多 16 个客户端映射、4096 个排队数据报、8 MiB 排队负载。
  可用 `--max-clients`、`--max-packets`、`--max-bytes` 调整。
  映射保留到代理退出；超过客户端数拒绝新地址，不驱逐已连接客机。
- 队列超限和发送失败分别计入 `overflow`、`send_errors`，不混入随机丢包数。
  报告分别统计上下行接收、丢包、重复、转发、峰值和停止时未交付数量。

## 自动验证与边界

```sh
python tests/test_net_proxy.py build/full/shiny.exe
ctest --test-dir build/full -R network_faults --output-on-failure
```

测试包括参数拒绝、固定种子和延迟范围、预算耗尽、真实 UDP 多客户端路由，
以及一主机三客机共四个原生进程的可靠有序往返。使用 150 ms RTT、±30 ms
抖动、5% 丢包、3% 重复包，每位客机发送并确认 80 条消息；报告必须证明上下行
实际发生丢包和重复，且无队列超限或发送错误。CTest 日志保留 JSON 统计。
不传可执行文件时只测代理，原生进程测试明确跳过。

以上代理测试只覆盖可靠传输。四人协作、20 Hz 快照、跨房间、令牌重连及
游戏协议故障使用 [Constellation](../../examples/constellation/README.md) 单独验证，
见[验收记录](../verification/games/constellation.md)。长时间联网尚未验收。
当前工具没有带宽限速、断网时间表或网络事件回放功能。
