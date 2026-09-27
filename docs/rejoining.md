# 玩家席位与令牌重连

`shiny.rejoin` 管理主机的有界远端玩家记录。默认 3 个远端席位，可配 1..32。
断开后保留玩家 ID 与令牌 30 秒，重新连接的 peer 必须提交匹配令牌。
这是会话续接规则，不是账号认证、加密连接或完整联机游戏协议。

## 应用时间

网络开启时，`sc.net.time()` 返回应用单调经过秒数，在每次固定更新开始时
采样，同一更新和随后 draw 中保持不变。初次更新前为 0，房间切换不重置；
暂停或加载等待结束后的下一次更新包含实际经过的时间。

它不是 `sc.time()` 的房间模拟时间，也不是 UTC，不应参与离线玩法回放或写入
游戏存档。没有应用服务的自定义原生宿主返回 nil,error；网络关闭时不注册。
系统休眠期间如何计时依赖平台单调时钟，本轮未验证休眠行为。

## 主机流程

```lua
local R = require("shiny.rejoin")
local roster = R.new(3)
-- 每次 update 先清理到期记录，再处理已验证的协议消息。
local now = sc.net.time()
for _, player in ipairs(R.expire(roster, now)) do
    remove_remote_player(player)
end

-- 新玩家：令牌由主机签发，不能采用客机提供的新令牌。
local host_token = assert(sc.net.token())
local player, token_or_error = R.join(roster, peer, "", host_token, now)
-- 重连：token 来自此前主机的欢迎消息。
local restored_player, error_or_token = R.join(roster, new_peer, token, nil, now)
-- 原生 disconnect 事件或游戏协议超时后调用；返回需要暂停控制的玩家 ID。
local disconnected_player = R.detach(roster, peer, now)
```

`sc.net.token()` 使用操作系统随机源生成 128 位令牌，编码为 32 个小写十六进制
字符，不消耗玩法随机数。每个应用固定更新最多尝试 64 次；失败也消耗一次预算，
以免系统随机源失败时无限重试。失败返回 nil,error，没有弱随机回退。
候选初始化、检查模式和 draw 禁止签发；新更新重置预算，网络 service 不重置。

`shiny.rejoin` 校验令牌格式和当前记录内的唯一性；新玩家签发新令牌，重连保留
原令牌。随机生成不保证数学意义上的永不重复，遇到记录碰撞时应重新签发，
不得复用已到期玩家的旧令牌。真实双进程测试已使用操作系统签发结果；边界
规则的独立测试保留固定夹具。协议不得把令牌放入公开快照或诊断 watch。

平台实现位于 `src/platform/net_token.cpp`：Windows 使用
[BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom)，
macOS 使用 [SecRandomCopyBytes](https://developer.apple.com/documentation/security/secrandomcopybytes(_:_:_:))，
Linux 使用 [getrandom](https://man7.org/linux/man-pages/man2/getrandom.2.html)。
Linux 在熵池尚未准备时使用非阻塞错误返回；接口均检查系统返回值。
本轮仅 Windows 路径完成实测，另外两个平台保留明确未验收状态。

- 玩家 ID 是应用内递增的远端游戏 ID，与 ENet peer、原生实体句柄分离。
  连接替换保留玩家 ID；到期后重新加入分配新 ID，不复用旧 ID。
- 未断开的玩家拒绝被另一连接接管。同一 peer 不能重复占位。
- `detach` 只对当前活动 peer 生效，重复或旧连接的断开事件不能刷新期限。
- 重连必须在 `now < disconnect_time + 30` 时完成；恰好 30 秒已到期。
- 保留中的席位占用容量。每次更新调用 `expire` 后再处理新加入，避免到期
  记录未清理而占满席位。清理返回玩家 ID，游戏显式删除其对象与状态。
- 时间参数须非负、有限且单调不减。所有网络超时规则使用同一应用时间源。
  模块不自行检测心跳或关闭套接字；协议检测超时后显式断开并 detach。

## 跨房间

roster 是有界普通数据，变更后以及切房间前调用 `session:state(roster)` 显式
提交副本。新 VM 先通过 `sc.net.bind` 绑定原会话，再调用
`R.restore(session:state())` 校验并复制。应用仍持有原命名网络会话。
`restore` 拒绝版本、
字段、重复令牌/玩家/peer 和过大期限；不会创建套接字。

这些是同一次应用会话的数据，不能写入存档后在新进程恢复。`R.player(roster,
peer)` 查询当前连接对应的玩家，未知或失效 peer 返回 nil。直接修改 roster
内部记录不属于支持的用法。

命名会话的协议状态与 `sc.state` 分离，不进入检查点、自动 trace 或观察摘要。
它可与游戏存档并存，但不要再把 peer/令牌/应用时钟复制到 `sc.state`。
显式关闭会话或应用退出会释放协议状态；读取返回副本，Lua 修改后需再次提交。

`session:state()` 无参数时读取；传普通对象时原子替换；显式传 nil 清除。
单会话最多 64 KiB 编码后 JSON、嵌套深度 16；仅支持与 `sc.state` 相同的普通
数据类型和根对象。校验失败返回 nil,error 并保留原数据。`session:stats()`
提供 `state_bytes` 和 `state_capacity`。候选房间初始化和 draw 只能读取；
写入受网络阶段保护。传输故障后数据仍可读取，直到显式 close；同名新会话
不会继承已关闭会话的数据。此接口不自动恢复套接字或复制 VM。

验证脚本：`python tests/rejoin_integration.py build/full/shiny.exe`。包含
30 秒边界的注入时间测试和真实双进程断开、更换连接、主机切房间、错误令牌
拒绝及玩家恢复。完整四人玩法、弱网重连和心跳协议仍待整合。
