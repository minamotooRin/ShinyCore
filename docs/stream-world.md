# 流式世界协调模块

`shiny.stream_world` 组合 Regions、Tiles 和 Objects；每个房间使用一个 owner，
不新增通用管理框架。项目应声明 streaming 模块以及绘制所需图片资源。
下例中的 player_x/player_y 由游戏角色逻辑提供；绘制读取原生相机。

```lua
local World = require("shiny.stream_world")
local world
return {
    init = function()
        world = World.new {
            index="built/map-world/index.json", name="forest", slot="slot",
            margin=1, capacity=16, residency=true, retain_images={"player"},
            prepare=function(object, saved)
                return {x=saved and saved.x or object.x, y=object.y}, {}
            end,
            export=function(entity, data, object) return {x=entity.x} end,
        }
        World.request(world, {{x=player_x, y=player_y}}, 0)
    end,
    update = function(dt)
        local changed, err, event = World.update(world, dt)
        if err then save_error=err end
        if World.status(world) then return end -- 停止玩法修改，保留待提交快照。
        -- 无 pending 时，可为新的玩家/相机关注区域发起下一次 request。
    end,
    ui_update = function(dt)
        World.ui_update(world)
        -- World.status(world).status == "failed" 时展示重试/取消控件。
        -- 控件调用 World.retry(world) 或 World.cancel(world)。
    end,
    draw = function()
        World.draw(world)
    end,
}
```

name 是稳定地图存档命名空间，块键为 `name:x:y`；移动资源路径不改变此名字。
slot、prepare 和 export 必填。回调契约见 [对象所有权](stream-objects.md)。
images 可传入图片路径到资源名的映射。`residency=true` 使本 owner 独占房间的
`sc.images` 集合；省略时沿用项目预载。`retain_images` 为本 owner 显式保留房间
独立绘制、弹体、粒子和材质采样等额外图片，默认空数组；它要求 residency=true。
图片仍须在项目声明，只有 stream=true 的图片进入驻留缓存，常驻图片校验后跳过。
自动依赖包含已准备的可见图块、动画所有帧、可见图像层，以及保留/进入块的对象
与复合 prefab 子对象精灵；原生端补齐绑定法线图。动态图集切换需事先列入 retain_images，不依赖首次
绘制时的同步加载。事务期间不要在模块外操作 sc.images。
margin 默认 1，capacity 默认 256；
加载边界和局部导航默认开启，可用 boundary=false、navigation=false 关闭。
cell_size 可配置导航单元大小；矩形导航总单元数仍不得超过 16,384，因此块
引用容量并不等于导航窗口容量。超限报错，不静默裁剪。

游戏可用 `World.path(world,sx,sy,gx,gy,budget?,radius?)` 查询当前已发布地图：
输入为世界像素位置，返回 `{status,visited,points}`；成功时点是世界像素单元中心。
`World.flow(world,gx,gy,budget?,slot?,radius?)` 以世界像素目标建立共享流场，
成功返回原生 `handle,status,visited`，后续用 `sc.navigation.refresh/steer` 推进与
批量引导。`sc.navigation.direction` 本身也读取世界像素位置。未加载端点返回
`path.status="unloaded"` 或 `flow` 的 `nil,"unloaded",0`；禁用导航时对应返回
`disabled`。路径的其他结果仍为 `ok`、`unreachable` 或 `budget_exhausted`。
非法坐标、预算、槽位和半径在报告未加载状态前仍会验证。
地图准备期间查询仍使用旧活动窗口；发布后旧流场句柄按原生生命周期失效。
此封装只处理已发布窗口。开发工具可从同一原生碰撞语义生成[静态跨块连通图](navigation-bake.md)；
`shiny.stream_route` 在图上给出跨未加载块的入口序列，游戏依序预取并用当前
`World.path` 验证每段。存档地图修改尚未回写静态图，路线不能代替局部碰撞和
加载边界校验。

request 接收像素区域数组与计划帧，只有一个 pending 请求。正常返回计划；若将卸载
位置已跨到别块但尚未迁移的对象，则返回 nil,error，计划已取消，当前世界保留。
update 在原生块已发布
后暂停模拟，批量异步读取进入块的持久状态；下一固定更新取得结果后准备绘制、
碰撞和对象，导出离开块。地形包含图块与对象层烘焙的静态碰撞，跨块对象的锚点
由索引覆盖关系保留。图片集合变化时先进入 images 阶段，在固定边界等待 CPU
解码和 GPU 上传；集合相同则不新增阶段。有离开块时再提交后台存档，下一固定更新
观察成功后联合发布对象、地形、导航、边界和已准备图片，替换 chunks、owners 和
prepared。图片在所有世界预检成功后提交，实体容量失败也不会先卸载旧图片。
首次加载也等待异步读取完成，但无需写盘；无进入块时跳过读取。保留块的对象不重建。
owner 表以 `x:y` 为键。
只保留当前活动块，不积累历史数据。空区域请求保存并卸载所有对象，清除地形
和边界，并将导航设为全阻挡。取消用 cancel，范围查询用 contains。

update 返回 changed,error,event：true 表示区域已发布，false 表示空闲/等待或
主动保存/取消完成；nil,error 表示事务读取、准备、写入预检或联合发布失败。
完成事件为 published、saved、transferred、patched、reloaded 或 cancelled，首次加载也返回 published。
准备/发布异常转为可重试的 failed 状态；配置和非法 API 调用仍可抛出 Lua 错误。
不要在模块外修改 owner、region
或其边界。事务期间不推进地图动画，draw 始终提交当前已发布的绘制列表。

## 等待与错误恢复

`World.status(world)` 空闲时返回 nil，否则返回独立状态表：kind 为 save/transfer/transition/patch/reload，
phase 为 read/prepare/images/write/publish，status 为 pending/complete/failed，另有 cancelling
和可选 error/request/operation。complete 表示当前阶段可继续，并非世界已发布；
仍须调用 update 完成后续阶段及清理。事务期间禁止 request、patch
及其他存档操作，玩法也必须停止修改对象/状态；模块暂停物理，结束后恢复原暂停值。
提交、观察、发布次序固定，磁盘快慢不会改变中间模拟帧数量。

存档读写或图片准备失败时宿主停止固定更新，UI/设备/网络继续运行。房间须调用 World.ui_update，
并在 UI 中提供 retry/cancel；无窗口未处理的 IO 失败会报错退出。读取重试保留原键列表
和已选索引；准备失败重试使用已读取的块数据重新准备；写入重试使用原快照，不再次
导出对象。若磁盘成功、实体容量等发布预检失败，旧区域仍保留；
修正问题后 retry 只重新发布，不重复写盘。图片失败重试保留当前准备集合；
成功图片不重复解码。取消释放候选图片，保留活动世界引用。首次图片提交前
World.draw 不提交地图图片层，避免绘制尚未驻留的背景。

cancel 放弃世界发布，不撤销已接受的磁盘写入。对象转移的双块写入一旦提交请求就
必须完成或 retry，不能 cancel，否则磁盘和活动对象可能指向不同归属。其他待读写
仍须结束；若取消后 IO
失败，ui_update 释放失败结果，使 update 能完成取消。成功的磁盘快照可能已改变，
取消不会恢复旧存档。cancel 返回 false 表示还需 update 收尾；没有事务时直接取消
区域请求并返回 true（此分支仅能在 update 调用）。重试、取消不会部分卸载旧对象。

新槽及其备份均不存在时，进入块按无持久记录恢复；已存在的损坏存档仍报错。
存档读写共用应用存档线程，原始块文件读取使用流式工作线程。单次进入状态合计
受 1 MiB 编码结果预算限制，最多 1024 个键，超限不部分发布。声明在
`project.stream_indexes` 的地图索引由内容线程预读；未声明的索引仍同步打开。
开启 residency 时图片按上述集合动态驻留，释放后可留在有界 LRU 缓存。
图片依赖集合的容量包含切换期间新旧集合的并集。绘制命令、延迟实体
身体创建与进程崩溃不属于完整事务保证。原生像素效果仍需单独验收。

2026-09-26：单项无窗口宿主场景验证首次加载、加载边界、跨块对象释放、磁盘
保存、重返状态恢复、绘制列表同步替换和清空区域；同时检查缺槽与损坏槽语义。
同项 ASan/UBSan 通过。未启动图形窗口、性能测试或全套回归。

## 跨块对象转移

活动的流式对象进入另一已加载块后，在 update 中按对象持久 ID 调用
`World.transfer(world, persistent_id)`。模块根据实体当前位置求目标块，同时保存
原块迁出标记与目标块的原始定义/显式状态；`World.update` 返回 `transferred` 后
归属正式切换。实体句柄不重建，存档和后续卸载由新 owner 管理。目标块未加载时
返回 `nil,error`，对象保持原归属；仍在原块时返回 `false`。应用应把目标块加入
关注区域，等待发布后再转移，并在事务期间停止玩法修改。模块不逐帧扫描实体位置；
移动规则决定何时调用此接口。若 request 将卸载一个已有对象离开的原块，它返回
`nil,error` 并取消该计划，避免静默卸载对象；先请求同时覆盖原块和目标块，完成
迁移后再缩小关注区域。

同一固定更新有多个对象跨块时调用
`World.transfer_many(world, {"npc:1", "npc:2"})`。它先验证所有对象和目标块，
再对每个受影响块导出一次，以一次异步存档提交所有迁移；成功仍由 update 报告
`transferred`。无须迁移的对象跳过，整批均未跨块时返回 false。若任一 ID、目标或
存档写入失败，所有对象保持旧归属；批量最多 4096 个 ID，实际写入仍受存档容量
限制。单对象 `World.transfer` 使用同一批量实现。

## 地图修改与主动存档

```lua
World.patch(world, {
    {x=12, y=-1, layer=0, gid=0}, -- 移除图块
    {x=13, y=-1, layer=0, gid=7},
})
local request, err = World.save(world)
```

patch 使用全局图块坐标、零基图层索引和 Tiled GID，只能修改已加载块的图块层；
图块层原本为空时可创建其单元数组。批次最多 16,384 项，重复单元以最后一项
为准。先复制受影响块并完整准备绘制与碰撞，原生地形/导航成功提交后才替换
活动数据；无效 GID、图层或未加载块均保留整个旧批次。返回输入项数，空批次
返回 0。开启 residency 且修改需要改变图片集合时，返回 `count,"pending"`，进入
kind=patch 的图片/发布事务；update 返回 patched 才表示地形、导航和图片已提交，
cancel 保留原修改前的地图。依赖集合不变仍立即提交。不要在 pending 时提前更改
依赖修改结果的任务/存档状态；原子性只覆盖本模块拥有的数据。普通整数和整数值浮点输入等价，不因 Lua 数字表示产生不同块键。

修改以稀疏图层/单元覆盖记录保存，不修改离线块文件；对象状态和图块覆盖共用
一次分块存档提交。gid=0 也保留为显式覆盖，重返时不会恢复被删除图块。只保存
当前块的覆盖记录，卸载后释放内存；存档仍受每次 256 KiB 数据预算限制。预算
不足明确失败，保留活动世界，不静默丢失修改。

World.save 在 update 中为所有活动块及共享状态提交独立快照，不卸载对象或改变
边界，返回请求号或 nil,error。已有事务或未完成区域请求时拒绝保存。请求号由模块
持有，不要直接调用原生 retry/release；通过 World.update 的 saved 事件确认完成。
空世界仍保存共享状态。退出前或检查点可显式调用；模块不会自动在退出时发起保存，
宿主会完成已接受的写入。patch 只更新活动状态，持久化发生在 save 或块卸载时。

2026-09-26：单项无窗口联调通过，覆盖批量错误保留、负图块行、整数值浮点
坐标、真实射线命中、导航阻挡更新、主动保存不卸载，以及修改/删除后两次
卸载重返恢复。本次仅改 Lua 和文档，未重建原生程序或扩大测试范围。

## 图片重载

`World.reload_images(world)` 要求 residency=true、已有已提交图片且没有区域/世界
事务。返回请求号或 nil,error；请求仍由 owner 持有。update 推进 images/publish
阶段，返回 reloaded 事件后新纹理可见；等待时暂停模拟，原地图和对象保持不动。
读图或上传失败沿用 status/retry/cancel；取消返回 cancelled，不清空地图或撤销存档。
正常重载不读取/写入块状态、不重建实体/地形。尺寸变化或新旧图共同超预算明确
失败，当前图片仍有效。该入口不触发文件监听或自动 F5 操作。
