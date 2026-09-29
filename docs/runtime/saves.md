# 存档槽契约

项目须声明 `project.id`；`data_version` 默认 1，读取时必须完全匹配。
槽名为 1..128 字节 ASCII 字母、数字、下划线或连字符。设置文件与游戏存档独立。
原生绑定在 `src/script/script_save.cpp`，接口及结果记录由同一来源
生成 `--api`、LuaLS 和[字段参考](../api-reference.md)。

| 调用 | 成功结果 | 阶段 |
| --- | --- | --- |
| `list()` | 按槽名排序的 `ScSaveSlot[]` | load/init/update/draw/ui_update |
| `read(slot)` | `ScSaveRecord` 独立副本 | load/init/update/draw/ui_update |
| `write(slot)` | true | update，禁用检查模式 |
| `load(slot)` | true，表示已请求切换 | update，禁用检查模式 |
| `delete(slot)` | true，不存在也成功 | update，禁用检查模式 |
| `delete_async(slot)` | 请求号或 nil,error；完成由 status 确认 | update，磁盘目录必需 |
| `read_chunk(slot,key)` | 块数据或 nil | load/init/update，禁用检查模式 |
| `write_chunks(slot,changes)` | true | update，禁用检查模式 |

参数数量、槽名/类型及阶段错误抛 Lua 错误。read/write/load/块接口的预期存档
失败返回 `nil,error`；list/delete 的目录或文件操作失败抛 Lua 错误。
read 的槽不存在仍返回错误；read_chunk 的槽或块不存在返回单个 nil，损坏数据
或无效块 key 保留错误。块参数与原子提交细节见[分块存档](chunk-saves.md)。

默认无窗口运行使用最多 16 个内存槽；磁盘存档用 `--save-dir`，list 最多列出
128 个不同槽。主文件和备份合为一项，只有备份也会列出；两份均无效时返回
`valid=false,error`，不掺入局部元数据。无效槽名及无关文件不占槽位。

read 返回 format/project/data_version/scene/state/chunk_count，不返回原生块索引。
frame 和 saved_at 是可选元数据（引擎写入时都有），若存在必须是 0..2^53−1 的
整数；saved_at 为 Unix 秒，不参与玩法确定性。额外文件字段不通过 read 暴露。
修改返回表不会修改磁盘、当前 sc.state 或已选中的块快照。

load 验证记录并暂存恢复状态；只有候选房间成功后才提交，不立即替换当前世界。
存档记录显式状态与场景，重建对象，不保存运行时句柄、Lua VM 或求解器缓存。
当前格式为 3，拒绝其他格式，不提供旧版兼容或迁移。

原有 `read/list/write/delete` 磁盘接口仍同步；只读阶段可调用不表示没有磁盘成本。
后台读写和删除使用 `read_chunks_async`、`write_async`、`write_chunks_async`、
`delete_async`，以 `status/retry/release` 管理请求，见[异步事务](chunk-saves.md)。
请求未释放时禁止其他存档操作及切房间。Wayfarer 标题和流式世界已使用异步接口。

早期存档契约验证记录见[分块存档验证](../verification/systems/chunk-saves.md)；
当前异步删除的定向证据见[Wayfarer 删除验证](../verification/games/wayfarer-delete-async.md)。
