# DUET / A SHARED SIGNAL

一个文件、两个玩家、一项合作目标：金色主机与青色客机站到各自的圆环上，保持一秒，让共享信号亮起。没有外部素材、服务器账号或服务端框架。

```sh
cmake -S . -B build-net -DSHINY_NETWORK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-net --parallel
./build-net/shiny examples/duet
```

在一扇窗口按 **Z / Space** 创建主机，再打开另一扇窗口按 **X / E** 连接默认的 `127.0.0.1:7777`。方向键控制移动；游戏中按 Z 返回菜单并关闭连接。局域网联机时，在菜单用左右键选择 IPv4 的一段，上下键修改数值，再按 X 连接主机的局域网地址。主机监听 `0.0.0.0:7777`，请允许防火墙上的 UDP 7777。

主机计算两名玩家的位置和合作进度。客机每秒发送 20 次方向输入，并平滑显示每秒 20 次的主机状态；客机不提交位置、不预测或回滚。超过 250 ms 没有新输入时，主机停止客机角色；已连接的对端超过 5 秒没有有效消息时返回菜单。使用 `--debug-keys` 时，F5 重载会返回初始菜单，需重新连接。

`--check` 只验证初始场景，不打开网络。`SHINY_NETWORK` 默认关闭；此时示例仍可检查和显示菜单，创建连接会明确提示功能未启用。

无窗口进程需要 `--realtime` 按真实时间运行，让 UDP 对端有机会交换消息。在两个终端运行：

```sh
./build-net/shiny --headless --realtime examples/duet --frames 270 \
  --replay examples/duet/replays/host.txt
./build-net/shiny --headless --realtime examples/duet --frames 240 \
  --replay examples/duet/replays/join.txt
```

尽快启动第二条命令；自动化测试会等待主机真正开始监听后启动客机，并为每次测试选择临时 UDP 端口：

```sh
python3 tests/network_integration.py ./build-net/shiny .
```

测试验证实际进程间握手、客机输入导致主机移动、主机快照驱动客机显示、双方完成合作目标、主机拒绝越界输入，以及丢失输入后角色停止。网络到达时序不确定，不能据此要求两个进程的最终 hash 或位置完全相同。协议字段和传输边界见 [网络说明](../../docs/networking.md)。
