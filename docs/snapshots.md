# 客户端位置快照插值

`require("shiny.snapshot")` 是普通 Lua 模块，不依赖 ENet，也不修改实体、物理
或存档。它保存已验证的完整位置快照，按服务器 tick 插值，供客户端更新显示。
将 `lua/shiny/snapshot.lua` 复制到项目 `lib/shiny/`；项目创建工具会包含它。

```lua
local S = require("shiny.snapshot")
local timeline = S.new(32, 4) -- 快照数量、每份快照的对象数量上限
local shown = S.output(timeline) -- 只初始化一次，后续采样复用

-- update 中，游戏协议解码和验证房间/会话 epoch 后提交。
local accepted, reason = S.push(timeline, sequence, server_tick, {
    {id=1, x=host_x, y=host_y},
    {id=2, x=guest_x, y=guest_y},
})
-- sequence 为 uint32；server_tick 为单调递增整数。
-- estimated_server_tick 由游戏协议维护；60 Hz 下延迟 12 tick 即 200 ms。
local count, status = S.sample(timeline, math.max(0, estimated_server_tick-12), shown)
-- 仅 shown[1..count] 有效；将结果用于视觉位置，权威玩法状态独立保存。
```

## 契约

| 接口 | 语义 |
| --- | --- |
| `new(capacity=32,max_objects=4)` | 预分配全部缓冲；容量 2..64，对象上限 1..64 |
| `output(buffer)` | 创建可复用输出数组 |
| `push(buffer,sequence,tick,objects)` | 复制整份快照；成功 true，拒绝 false,reason，失败不改变时间线 |
| `sample(buffer,tick,output)` | 返回有效对象数及状态；允许小数 tick，不推进时间线 |
| `bounds(buffer)` | 返回最早和最新服务器 tick；空缓存返回 nil,nil |
| `reset(buffer)` | 清空时间线，复用存储，用于已验证的房间/会话 epoch 切换 |

`objects` 是普通稠密数组，每项只能包含 `id,x,y`。ID 为 1..2³²−1 的游戏
协议 ID，严格升序且不重复；它不是原生实体句柄，也不是自动生成的对象持久 ID。
位置必须有限且在 ±10⁹ 内。服务器 tick 范围 0..2⁵²−1。对象数量为零的快照合法。

重复/陈旧序号被拒绝；uint32 回绕正常处理，差值恰好半圈视为陈旧。
新序号必须带来更大的服务器 tick。满缓存淘汰最早快照；输入和输出的修改
不会改变已存快照。调用方不要直接修改 buffer 内部字段。

## 显示行为

- `empty`：没有快照，返回 0 个对象。
- `buffering`：查询时间早于最早快照，返回最早位置。
- `interpolated`：两份快照之间线性插值；仅插值两边都有的相同 ID。
- `held`：查询时间达到/超过最新快照，保持最新位置，不外推。

对象集合在到达新快照的 tick 时切换：新对象不会提前出现，消失对象此前保持
最后位置。对象瞬移或同 ID 被复用为其他对象时，协议应显式重置时间线，避免
在不相干的位置间插值。断流后缓存不会自行删除对象；断线与超时由游戏协议处理。

模块不负责服务器时钟估计、播放游标调速、房间版本校验、网络认证、预测或回滚。
这些信息不能从孤立的位置快照可靠推断。`sample` 应在 update 中写入显示数据，
draw 只读这些数据并提交绘制命令。

可运行示例：`shiny examples/snapshot`。示例离线模拟 20 Hz 发送、延迟、丢包与
断流；它不是四人联机游戏。真实 UDP 故障注入见 [弱网工具](network-testing.md)。
