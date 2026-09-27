# 对象持久 ID

`persistent_id` 是对象在房间内的稳定字符串标识；`id` 是本次运行中的生成号
句柄。存档保存前者，恢复时重建对象并重新取得句柄。

```lua
local chest = sc.spawn { persistent_id = "forest/chest.7", x = 32, y = 24 }
local ref = sc.identity.reference(chest)
-- ref = { room = "main.lua", persistent_id = "forest/chest.7" }
sc.state.set("selected_chest", ref)

local result = sc.identity.resolve(sc.state.get("selected_chest"))
if result.status == "active" then
    sc.set(result.id, { x = 40 })
end
```

未指定时字段为 `""`，对象不进入索引。非空 ID 最多 127 个 ASCII 字节，允许
字母、数字、`_`、`-`、`.`、`/`、`:`；禁止首尾 `/`、空路径段和 `.` / `..` 段。
它是机器标识，显示名称可以独立使用中文。创建后不可修改 ID；重复活动 ID
会使创建失败。普通 prefab 表也可直接填写此字段，不自动派生子对象 ID。

`sc.spawn_many({entity, ...}, parents?)` 一次创建一批对象，返回与输入顺序一致的句柄
数组。每项使用 `sc.spawn` 的字段、默认值和校验；输入必须是无元表的连续
数组，空数组合法。整批预检几何、重复/活动 ID、实体及 ID 索引容量，任一
失败均不创建对象、不改变生成号或 ID 状态。已有 `unloaded/deleted` ID 可以
显式重建。接口仅允许 load/init/update，不在调用中推进物理或执行游戏回调。
这提供块对象提交的原子入口，但不自动承担地图绘制、地形或状态恢复事务。
可选 parents 通过同批索引原子创建[视觉层级](attachments.md)，不接受持久 ID 或
运行时句柄作为索引。存档恢复应先将父对象持久 ID 映射为批内索引。

| API / 状态 | 语义 |
| --- | --- |
| `declare(persistent_id)` | 登记为 `unloaded`；已有记录保持原状态 |
| `reference(id)` | 返回房间入口路径与对象持久 ID；拒绝无 ID 或已失效的实体 |
| `resolve(persistent_id或ref)` | 返回状态；只有 `active` 包含当前运行时 `id` |
| `unload(id)` | 释放实体、使旧句柄失效，保留 `unloaded` 记录 |
| `remove(persistent_id)` | 释放活动实体并记录 `deleted`；未加载的 ID 也可标记删除 |
| `absent` | 当前房间没有登记这个 ID |
| `room_inactive` | 引用属于其他房间；不推断该房间中的对象是否存在 |

`sc.destroy(id)` 同样留下 `deleted` 记录；显式使用同一 ID 再次 `spawn` 会
恢复为活动对象，并得到新句柄。读取可用于 draw，登记、卸载和删除只可用于
load/init/update。引用必须是仅含 `room`、`persistent_id` 的普通数据表。

`project.limits.identities` 默认 4096，范围 0..65536；0 关闭索引。容量计算
包含活动、未加载及已删除记录，模拟期间不扩容，也不通过删除回收 ID 槽位。
这使同一房间中的缺失、卸载与删除可明确区分。

索引归房间所有；房间结束时释放。持久引用可随显式状态或存档保存，但对象
属性、删除记录及未加载记录仍需游戏显式导出并在 init 中恢复。当前不自动
保存索引；游戏可通过[分块存档](chunk-saves.md)显式保存并恢复这些记录。
流式对象可通过 [Lua 对象模块](stream-objects.md)显式保存、卸载和恢复；
[流式世界协调模块](stream-world.md)已联合提交对象、地形、导航与资源，游戏通过
prepare/export 回调解释对象数据和持久状态。离线地图输出统一使用 `persistent_id`，
不自动把任意 Tiled 对象转换为游戏实体。
[对象模板](prefab.md)提供批量创建与显式视觉附着；[原生附着](attachments.md)
提供物理步进后的自动父子同步及层级插值。运行时父句柄不自动持久化，游戏应以
对象持久 ID 导出父子关系，重建对象后再恢复关系。
