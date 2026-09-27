# 存档槽契约

项目须声明 `project.id`；`data_version` 默认 1，读取时必须完全匹配。
槽名为 1..128 字节 ASCII 字母、数字、下划线或连字符。设置文件与游戏存档独立。
原生绑定在 `src/script/script_save.cpp`，七个接口及两种结果记录由同一来源
生成 `--api`、LuaLS 和[字段参考](api-reference.md)。

| 调用 | 成功结果 | 阶段 |
| --- | --- | --- |
| `list()` | 按槽名排序的 `ScSaveSlot[]` | load/init/update/draw/ui_update |
| `read(slot)` | `ScSaveRecord` 独立副本 | load/init/update/draw/ui_update |
| `write(slot)` | true | update，禁用检查模式 |
| `load(slot)` | true，表示已请求切换 | update，禁用检查模式 |
| `delete(slot)` | true，不存在也成功 | update，禁用检查模式 |
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

原有磁盘接口仍同步。通常在 init/update 读取并缓存菜单数据，draw 只画缓存；
接口允许只读阶段读取并不表示该操作无磁盘成本。后台保存使用 write_async /
write_chunks_async，加 status/retry/release 管理请求，见[异步事务](chunk-saves.md)。
请求未释放时禁止其他存档操作及切房间。流式世界模块尚未改用这套异步流程。

2026-09-27：`tests/save_contracts.py` 与已有跨房间/进程分块恢复用例通过 Windows
Release 和无窗口 ASan/UBSan。覆盖参数/阶段、独立副本、延后恢复、备份独存、
去重排序、旧格式和非法元数据回退。API 生成一致性及七个项目 SDK 审计通过；
全部 13 份样例 API 注解同步，六个带注解 SDK 更新到 dev.14。没有运行全套回归。
