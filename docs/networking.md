# 原生网络与联机

网络是可选模块：C++23 网络层以 RAII 管理 ENet 的 UDP 资源，Lua 游戏决定消息格式、玩家身份和同步方式。模拟内核不读取套接字，不依赖 ENet，也不会自动复制实体。默认构建关闭网络，只有显式开启时才下载并静态链接固定版本的 ENet。

C++ 接口在 [`include/shiny/net.h`](../include/shiny/net.h)，独立于 Lua、图形和模拟内核。原生调用方可以用 `ScNet::poll` 指定 0..1000 ms 等待；Lua 的 `session:poll()` 始终不等待。一个会话及其生命周期由单一线程管理。

```sh
cmake -S . -B build-net -DSHINY_NETWORK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-net --parallel
ctest --test-dir build-net --output-on-failure
./build-net/shiny examples/duet
```

独立无窗口主机加 `-DSHINY_GRAPHICS=OFF`。运行时使用 `--headless --realtime`，按真实时间推进固定 60 Hz 更新；普通 `--headless` 仍尽快完成离线回放。网络到达时间不在回放文件或世界状态 hash 中，联机运行不承诺离线回放的确定性。

打包时显式包含联机示例，工具会检查可执行文件的 `--api` 能力，拒绝给关闭网络的二进制打出联机包：

```sh
python3 tools/package.py build-net/shiny dist/ShinyCore-Network --with-network-examples
```

包内同时保留 LANTERN、DUET、网络文档和原始依赖许可。运行命令在包根目录的 `README.txt`；双击默认应用仍启动 LANTERN。

## 最小接口

原生 C++ 接口位于 `include/shiny/net.h`：`ScNet` 独占连接资源，工厂返回 `std::expected<std::unique_ptr<ScNet>, std::string>`，消息使用 `std::span`，接收事件持有自己的 `std::array`。Lua 绑定在调用可能触发错误跳转的 Lua API 前结束所有原生临时对象与异常处理作用域，防止跳过 C++ 析构。

```lua
-- 在 init/update 中显式创建；DUET 选择等玩家按键后才创建。
if not sc.net.available then error("build with SHINY_NETWORK=ON") end
local session, err = sc.net.host("0.0.0.0", 7777, 8)
-- 客机使用 sc.net.join("127.0.0.1", 7777)。只接受数字 IPv4。
if not session then error(err) end

-- 在 update 内轮询；poll 不等待数据，nil 表示当前没有事件。
for _ = 1, 64 do
    local event, problem = session:poll()
    if problem then error(problem) end
    if not event then break end
    if event.type == "connect" then
        local ok, failure = session:send(event.peer, "hello", "reliable")
        if not ok then error(failure) end
    elseif event.type == "receive" then
        sc.log("bytes=" .. #event.data .. " channel=" .. event.channel)
    elseif event.type == "disconnect" then
        -- 游戏层移除玩家和该玩家的输入缓存。
    end
end
session:flush()
-- 完成后 session:close()；session:port() 可读取实际本地端口。
```

`host` 返回监听会话，端口 0 让操作系统选择空闲端口；`join` 返回连接中的会话，远端端口必须为 1..65535。必须等待 `connect` 事件后使用其 `peer` ID 发消息。每个会话有独立的 peer ID；旧连接断开后，其 ID 不会指向新连接。连接建立前失败可能产生 `peer=0` 的 disconnect 事件。

`send(0, data, channel)` 广播到该会话所有已连接对端；没有已连接对端时返回失败。`disconnect(peer[, reason=0])` 开始有序断开；继续 poll 直到 disconnect 事件，期间不能再向该 peer 发送。`close()` 立即关闭整个会话，丢弃待发消息，可重复调用。`rtt(peer)` 返回连接的往返时间估计，单位毫秒。

| 通道 | 语义 | 适用消息 |
| --- | --- | --- |
| `reliable`（默认） | 可靠、有序，丢包会重传 | 握手、关键状态变更 |
| `state` | 不可靠、有序列控制，旧包可被跳过 | 可被下一条替代的输入/快照 |

通道之间没有统一的到达顺序。每条应用消息为 **0..1200 字节**，可包含 NUL；不要发送 Lua 表、内存结构体或无界 JSON。每个 VM 最多 4 个会话、每个主机最多 32 个 peer（默认 8）、每个 peer 最多 256 条待处理发送命令。达到上限显式返回 `nil, error`；参数类型错误抛出 Lua 错误。`send` 成功代表消息进入本地发送队列，不代表应用层已经接收；广播在中途分配失败时可能已经部分发送。轮询并定期 `flush`，不要在一个更新中无界处理事件。

会话由当前 Lua VM 持有；显式 `close`、垃圾回收、场景切换和 VM 关闭都会释放原生资源。使用 `--debug-keys` 时，F5 成功重载会重建 VM，旧连接随之关闭。网络创建、poll、send、flush、disconnect、close 属于有副作用的操作，不能在 `draw` 中调用。关闭网络的构建仅提供 `sc.net.available = false`，不注册会话工厂或方法；游戏在创建前检查 available，并提供所需提示。

## DUET 的主机权威协议

所有消息具有四字节头：ASCII `DU`、协议版本 `1`、消息类型。整数使用 `string.pack` / `string.unpack` 的显式大端格式，没有本机字节序或结构体填充依赖。

| 类型 | 通道 | 总长 | 头之后的字段 |
| --- | --- | --- | --- |
| HELLO = 1 | reliable | 4 | 无 |
| WELCOME = 2 | reliable | 4 | 无 |
| INPUT = 3 | state | 10 | 序号 u32；X、Y 各一个 i8，范围 -1..1 |
| STATE = 4 | state | 18 | 序号 u32；两人 X/Y 各一个 u16（1/8 像素）；进度 u16（0..1000） |

客机发送 HELLO，主机确认版本和阶段后返回 WELCOME。跨通道的快照可以先于 WELCOME 到达；尚未就绪的客机会丢弃这些早到快照。主机只接受客机方向，按固定速度、归一化斜向移动和场地边界计算位置；站上匹配圆环一秒的条件也只由主机判断。

输入和快照均为 20 Hz。序号使用 u32 模运算剔除重复/陈旧消息；完整消息长度、通道、版本、阶段和字段范围均校验。无效协议会关闭示例会话。主机在 250 ms 没有新输入后停止客机角色；握手或已连接对端 5 秒没有有效消息会超时，菜单上的空闲主机可以持续等待。客户端只平滑显示已收到的位置，没有预测、回滚或断线续局。

## 适用边界

ENet 提供传输可靠性，不提供加密、身份认证或玩家账号。当前接口没有 DNS、IPv6、HTTP/WebSocket、自动发现、NAT 穿透、房间匹配、中继或云服务；跨公网连接需要双方自行解决可达地址、防火墙和端口转发。协议版本校验不是身份认证，主机权威也不代表可以安全运行陌生 Lua 项目。

已验证能力以实际构建和测试记录为准，不从 ENet 支持的平台推导 ShinyCore 的平台兼容性。可直接复用此传输边界编写适合游戏的服务器规则，无需引入实体复制框架或把游戏协议放进引擎。

## 本机验收（2026-09-12）

macOS arm64、AppleClang 21、CMake 4.3.1：网络启用的 Release 与 ASan/UBSan 构建各通过全部 8 组 CTest；网络关闭构建通过 5 组。传输测试使用实际 UDP 回环，覆盖二进制与最大消息、有序可靠消息、状态序号、定向/广播、发送队列积压和恢复、断开排空、失效连接 ID、端口重用及失败连接超时。Lua 测试覆盖真实脚本绘制保护、VM 清理和分配失败。

4 项双进程测试验证握手与合作目标完成、非法方向拒绝、输入过期停止，以及端口已占用时 `--check` 仍不监听。原生客机与无窗口主机也实际交换位置，菜单和联网画面均完成截图检查。公网、丢包模拟和 Windows/Linux 实机运行尚未验收；CI 已配置网络开关与多平台构建。

C++23 网络启用的无窗口 Release 二进制为 **401,000 字节**；保留的 C11 网络启用版本为 **369,320 字节**，差额 **31,680 字节**。两者均未剥离符号。相同 LANTERN 项目、seed 42 和 480 帧回放的全部游戏状态相同，每个程序重复运行也得到相同结果。可复用 `tools/compare_engines.py C11_BINARY CPP23_BINARY` 生成 JSON 对比；版本/语言作为元数据报告，其他状态差异会返回非零退出码。比较体积时应匹配图形、网络、优化与符号选项，工具不会修改或剥离二进制。

原 C11 构建的网络关闭/开启大小分别为 312,808 / 369,320 字节，网络增量为 56,512 字节；这组数字仅描述保留基线，不是 C++23 的网络增量。关闭时没有下载 ENet，二进制没有 ENet 符号。体积会随编译器与功能变化，数字不是跨平台上限。交付包测试还验证了资源搬移，以及关闭网络的二进制拒绝 `--with-network-examples`。
