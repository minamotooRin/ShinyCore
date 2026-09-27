# 流式对象与显式状态

`require("shiny.stream_objects")` 管理已发布地图块中的实体和复合 prefab。
它使用 `sc.spawn_many`、对象持久 ID 和分块存档，不保存运行时句柄、VM 或
求解器。游戏提供两个普通 Lua 函数：

```lua
local Objects = require("shiny.stream_objects")
local function prepare(object, saved)
    return {x = saved and saved.x or object.x, y = object.y},
           {opened = saved and saved.opened or false}
end
local function export(entity, data, object)
    return {x = entity.x, opened = data.opened}
end

-- 在块已发布的 init/update 中调用。新游戏传 nil；续玩时读取对应块记录。
local owner = Objects.load(chunk, saved_record, prepare)
local chest = owner.entries[1]
chest.data.opened = true
sc.set(chest.id, {x = 64})

-- update 中：保存成功才释放实体；失败保留对象并返回 nil,error。
local ok, err = Objects.unload(owner, "slot1", "main/forest:0:0", export)
if ok then sc.stream.release(0, 0) end
```

`prepare(object, saved_data)` 返回实体规格与房间内玩法数据；模块负责填入
`persistent_id`，不允许改名。`owner.entries` 保持地图对象顺序，包含 `object`、
活动对象的 `id/ids/children/components/data`；已删除对象没有实体。对象跨越块边界移动仍归原块所有；
静态导入几何的锚点保留由 [stream_world](stream-object-coverage.md) 协调，
任意移动超出导入几何时游戏仍须显式保留该锚点。
一个地图对象也可对应多层 prefab，整组实体归同一个根对象持久 ID；动态对象
跨块归属迁移仍由游戏处理。

prepare 可返回 `{entity=..., children=..., components=...}` 作为第一值，子对象
写法与 [prefab](prefab.md) 相同。根实体的 `persistent_id` 由模块填入；子对象
不得另填对象持久 ID。`entry.id` 是根句柄，`entry.ids` 为整棵树的句柄，
`entry.children[name]` 提供命名子对象及其组件数据。`export` 可接收第四参数
`entry`，显式选择子对象状态；恢复时由 prepare 重新构造整组对象。销毁整组对象
应调用 `Objects.destroy(entry)`；直接销毁根实体会使仍活动的子对象失去归属，
保存时会被拒绝。卸载时先销毁子对象，再卸载根对象；旧句柄全部失效。

子对象的长期引用用命名路径表示，不能保存 `entry.children.*.id`：

```lua
-- 假设 prepare 创建了 lid.lock 两级子对象。
local ref = Objects.reference(owner.entries[1], {"lid", "lock"})
-- ref = {room="main.lua", persistent_id="chest:1", children={"lid","lock"}}
local result = Objects.resolve(ref)
if result.status == "active" then sc.set(result.id, {color="#FFC98C"}) end
```

`reference` 只接受活动对象及其现有命名路径，返回可序列化的普通表；省略路径
表示根对象。`resolve` 不读取磁盘或创建块：根未加载、已删除、未发现或房间不活动
时分别返回 `unloaded/deleted/absent/room_inactive`；重建后子路径已移除则返回
`path_missing`。根已活动但不由本模块持有时返回 `unmanaged`；子句柄被外部销毁
时返回 `stale`。除 `active` 外均无 `id`。引用保留房间与根对象持久 ID，
重访后解析为新句柄；子节点不占用原生对象持久 ID 容量。必须用
`Objects.resolve` 解析带 `children` 的引用，原生 `sc.identity.resolve` 只识别根引用。

`Objects.snapshot(owner, export)` 生成独立记录，不写盘、不卸载。记录格式为
`{format=1, objects={ [persistent_id]={data=...} 或 {deleted=true} }}`。
`export` 返回显式普通数据表，不能直接返回完整实体快照；只选择需要恢复的
玩法字段。模块复制数据并拒绝环、元表、函数及非有限数，原生存档继续校验
序列化大小、深度和键类型。记录版本不符、引用未知地图对象时拒绝加载。

卸载前完成全部导出和所有权检查，再通过 `sc.save.write_chunks` 原子保存。
失败时保持 owner 和实体可用；成功后活动实体变为 `unloaded`、旧句柄失效，
游戏已销毁的对象保留 `deleted`。读取记录后重建会恢复删除标记，不复活该对象。
同一个 owner 只能成功卸载一次。单实体对象可 `sc.destroy`，复合对象使用
`Objects.destroy` 删除；不得在 owner
之外卸载、重建或转移它。准备与导出回调不得修改世界、切房间或执行应用 IO。

加载先完成全部规格准备，再登记对象为 unloaded，最后原子批量创建活动实体。
容量不足可能留下已发现的 unloaded ID，但不会留下半批活动实体；原有世界
对象不被移除。ID 注册表的容量仍按房间内所有已发现对象计算，未无限扩展。
准备失败不会运行批量创建；补足容量或修正数据后可重试。

模块不自行打开或释放 `sc.stream`，也不缓存所有块状态。存档槽是否存在及
读取错误由游戏处理，不能把损坏存档当作新游戏。读写目前是同步磁盘操作，
候选房间初始化禁止写盘，卸载保存仅在 update 中调用。
需要绘制、地形、导航及加载边界协调时，采用下述联合提交或 stream_world。

多个块同时进入时使用 `Objects.load_many({{chunk=...,saved=...}, ...}, prepare)`，
返回按输入顺序排列的 owners；全部对象规格准备成功后才执行一次原子创建。
跨块重复对象持久 ID 被拒绝，后一个块失败不会留下前一个块的活动实体。

多个块离开时使用 `Objects.unload_many({{owner=...,key=...}, ...}, slot, export)`。
所有快照通过预检后，用一次 `write_chunks` 提交，再统一释放实体；导出、
序列化或保存失败均不部分卸载。重复 owner、块 key 或对象归属会被拒绝。
空批次合法。一次提交仍受原生 changes 的 256 KiB 预算约束，不能绕过预算。
单块接口复用同一实现。这两种批操作各自提交，不构成加载与卸载的联合事务。

## 验证记录 · 2026-09-25

`tests/stream_objects_integration.py` 的五项测试通过真实无窗口宿主验证：
卸载/重访、新进程恢复、写盘失败保留对象、准备/导出失败、容量不足重试，
以及离线生成块经 `sc.stream` 读取、释放和重访。完整 Release 与无窗口
ASan/UBSan 各通过相关 CTest 5/5；轻量构建通过 4/4，其对象测试中的真实
块读取子项因 streaming 关闭而跳过。文档及代码 `diff --check` 通过。
未运行图形、性能或长期测试，以上不证明完整流式世界验收通过。

2026-09-26 补充：批量加载/卸载及其失败路径已通过该模块的 9 项定向测试；
本次未重复运行其他构建和广泛回归。

## 联合加载

`Objects.load_many(items, prepare, publication?)` 和
`Objects.load(chunk, saved, prepare, publication?)` 可接收可选发布参数：

```lua
local next_tiles = Tiles.prepare(view, loaded_chunks)
local owners = Objects.load_many(entering_items, prepare_object, {
    terrain = Tiles.terrain(view, next_tiles),
    navigation = Tiles.navigation(view, loaded_chunks),
    region = interest_region,
})
prepared = next_tiles
```

terrain 必填；navigation 和 region 可省略。region 必须已有 request 计划并通过
ready。模块先准备全部对象及其游戏数据，再把对象与新边界合为同一原生实体批次，
和地形、导航一起预检提交。旧边界和旧块引用只在成功后释放；返回的 owner 仅
包含玩法对象，不混入边界实体。saved 中的删除标记不创建实体，成功后保留其
已删除状态。对象字段仍由现有 spawn_many 契约校验，不维护第二套格式。

失败不出现部分活动对象，旧地形、导航、边界及待提交计划保持可用。与普通
load_many 一样，预先发现的 ID 可以留下 unloaded 记录。prepare 回调必须保持
无副作用；Lua 内存耗尽、延迟物理身体分配和 GPU 上传不属于此处的恢复保证。
单独 load/load_many 不处理离开对象；需要完整对象切换顺序时使用下述 transition。

2026-09-26：单项真实宿主联调通过，覆盖错误地形保留、对象与边界同时创建、
句柄对应、删除标记、恢复显式数据，以及磁盘保存后再次联合加载。仅执行该项
必要测试，没有重建原生程序、启动图形窗口或性能测试。

## 对象区域切换

在 update 中调用：

```lua
local owners, err = Objects.transition(
    {{owner=old_owner, key="forest:-1:0"}},
    {{chunk=new_chunk, saved=sc.save.read_chunk("slot", "forest:0:0")}},
    "slot", prepare_object, export_object,
    {terrain=next_terrain, navigation=next_navigation, region=interest_region})
if owners then
    active_owners, prepared = owners, next_tiles
else
    save_error = err
end
```

顺序是：导出全部离开对象的显式状态，准备全部进入对象，再次检查旧对象生命
周期，一次原子分块存档提交，联合发布新对象/地形/导航/边界，最后释放离开
对象。返回值仅包含进入对象的 owners，保留区域的 owners 由调用方继续持有。
空 leaving 数组不写存档。切换期间容量必须同时容纳新旧对象和边界。

磁盘保存失败返回 nil,error；准备或原生发布校验失败抛出 Lua 错误，可用 pcall
处理。两种失败均保留旧活动对象；预先发现的对象持久 ID 可留下 unloaded 记录。
若保存成功但发布失败，磁盘保留刚写入的有效快照，内存仍是原活动区域，允许
修正输入后重试；不声称磁盘和内存拥有跨进程崩溃原子性。保存目前同步执行，
异步流程使用下述拆分接口；GPU 资源提交和完整崩溃恢复仍未完成。
准备/导出回调不得修改世界。

2026-09-26：单项真实宿主测试通过，注入实际存档路径故障及无效地形，验证旧
实体、旧碰撞和句柄保留；成功切换后旧引用失效，重返恢复磁盘中的对象位置。
仅修改 Lua 协调逻辑，没有新增原生资源所有权或扩大测试范围。

批量保存/卸载项可附带 `extra` 普通数据表，与对象快照一起存为 snapshot.extra；
输入先复制、验证，再参与同一次分块写入。Objects 不解释这些字段，加载时仍由
调用方读取。stream_world 使用 `{format=1,tiles=...}` 保存图块覆盖。
`Objects.save_many(items, slot, export)` 使用相同校验和存档顺序，但保留所有活动
对象；返回 true 或 nil,error。仅 update 阶段调用，空 items 不写盘。

## 异步协调所用接口

`Objects.changes(items, export)` 生成独立 changes，不保存或释放对象。
`Objects.prepare_transition(leaving, entering, prepare, export, publication)` 完成
同一套快照与进入对象准备，返回 draft，draft.changes 可提交 write_chunks_async。
保存成功后，在 update 中调用 `Objects.commit_transition(draft)` 返回进入 owners。
它再次验证离开对象生命周期，联合发布成功后才释放旧实体，同一 draft 只能成功提交一次。
发布失败可修正外部容量等条件后重试；不得改动 draft 或待离开对象。
这些接口不管理原生请求及模拟暂停；通常直接使用 [stream_world](stream-world.md)。

2026-09-27：复合对象已接入普通加载、联合地形/边界发布和异步 World 切换。
定向真实宿主测试覆盖多层子对象重访与删除、原子失败、流式世界卸载恢复和
子对象精灵驻留；这不是跨平台或完整游戏验收。

同日补充：根 ID＋命名子路径的可序列化引用已通过新进程存档恢复与路径消失
检查；本模块的活动索引随卸载/删除清理，不保存运行时句柄。
