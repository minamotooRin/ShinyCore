# Crossing 后台检查点（2026-09-28）

Crossing 的磁盘 SAVE、LOAD 和最终结局检查点使用应用级 `sc.save` 工作线程。
提交写入时冻结带结局标记的记录，活动房间保持未完成；固定边界确认写入并
释放请求后，才将结局状态发布给共享状态和 UI。结局提交当帧暂停物理，
使磁盘与无磁盘内存模式的结局状态一致；失败后恢复当前房间的模拟。
读取先在后台选择完整快照，
释放请求后，下一次固定更新由 `load` 消费固定索引。等待期间菜单与绘制仍可更新。
没有磁盘目录的无窗口示例继续使用内存槽。

必要验证：`crossing_integration.py` 的真实键盘与手柄三关回放、保存与跨进程
恢复、磁盘写入故障、一次失败后重试均通过；trace 断言待提交时
`save_pending=true` 且 `complete=false`，恢复后的数据与已提交检查点一致。
损坏的读档记录留在原处，标题房间保持活动并释放失败请求；
写入失败后继续按左键，角色仍可移动。
`walkthrough.scenario.json` 的 2876 帧八项玩法断言通过。
隐藏、静音的原生故障截图 `build/crossing-checkpoint-async-20260928/ending-write-error-final.png`
已检查；它不能代替实体设备、慢盘或完整交互验收。
轻量发行包 `build/crossing-checkpoint-async-release-20260928-r3/` 已生成，并从源码
目录外运行包内十帧 smoke 回放。包内完整人工流程尚未执行。
