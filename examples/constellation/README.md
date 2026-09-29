# Constellation：四人协作联机示例

一名主机和三名客机分别控制一个彩色光点。四人同时进入对应光环并停留两秒，
切换第二间房；再次连接全部光环即完成。几何美术与提示音由引擎原生绘制和合成。
这是短小的联机测试玩法，不属于三款要求 5–10 分钟流程的小样。

## 运行

需要 `SHINY_NETWORK=ON` 构建。仓库根目录运行四次：

```powershell
.\build\full\shiny.exe examples/constellation
```

一个窗口按 **H** 主持，其余按 **J** 加入。默认服务器为 `127.0.0.1:7778`。
跨机器时用菜单方向键选择和修改 IPv4 地址；端口在 `game/config.lua` 设置。
主机监听所有 IPv4 网卡。同机手动操作时，每个窗口只读取自身焦点输入。

- 方向键或 WASD 移动。
- 客机按 R 主动离开并以原临时令牌重连；位置、身份和当前房间由主机恢复。
- Escape 关闭本窗口的会话并返回菜单；菜单中再次按 Escape 退出。
- 菜单重新加入会请求新身份。令牌只保存在当前进程，不写存档，也不跨程序重启恢复。

## 结构与约束

`game/server.lua` 在 60 Hz 固定更新中决定位置、计时、切房间及结算；
客机每秒最多发送 20 次方向输入，不能上传位置或完成状态。
`game/client.lua` 使用 20 Hz 快照和有界 `shiny.snapshot` 插值，不做预测。
`game/protocol.lua` 定义版本化二进制消息，验证长度、通道、枚举、范围和对象顺序。
状态通道允许丢包；欢迎、切房间、离开和拒绝消息使用可靠通道。

应用持有命名会话 `coop`，房间 VM 重建后重新绑定；协议数据存放在
`session:state()`，仅公开不含令牌的 `coop` watch。`lib/shiny/` 是项目内固定副本。
连接后 3 秒未握手则断开；有效输入停止 0.25 秒后归零，5 秒后判定客机超时。
从主机检测离开起保留身份 30 秒，过期后不接受旧令牌。
临时令牌用于恢复会话，不提供账号认证或加密。主机退出后不迁移主机。

## 自动验证

在引擎仓库根目录执行：

```powershell
python tests/constellation_integration.py build/full/shiny.exe
python tests/constellation_integration.py build/full/shiny.exe --weak
python tests/constellation_integration.py build/full/shiny.exe --native --weak
python tests/constellation_faults.py build/full/shiny.exe
```

前两项启动一主机三客机，注入真实输入回放并验证两次协作及一次重连。
`--weak` 通过外部代理注入 150 ms RTT、±30 ms 抖动、5% 丢包、3% 重复包；
`--native` 使用原生主机窗口并保存截图。故障测试实际等待 30 秒过期窗口，约需 42 秒。
报告位于可执行文件同目录的 `verification-constellation/`。
回放只记录输入，不声称固定网络到达时间；网络测试必须使用实时主循环。
`replays/host.jsonl`、`client.jsonl`、`rejoin.jsonl` 是测试直接使用的输入文件。
项目自带相对路径 LuaLS 配置和本地 API 注解，可整体移动并离线开发。

发行包：

```powershell
python tools/package.py build/full/shiny.exe dist/constellation --project examples/constellation
```

输出目录必须不存在。完整验收范围见仓库的 `docs/verification/constellation.md`。
