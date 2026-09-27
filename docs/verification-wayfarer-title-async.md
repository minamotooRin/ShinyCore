# Wayfarer 标题异步存档读取（2026-09-27）

标题 `init` 不再遍历存档目录或读取检查点。首个固定更新提交空键批量读取；
宿主等待工作线程时继续调用 `ui_update` 和原生绘制。选择动作暂存，结果发布并
`release` 后执行，故首帧 Enter 仍能开始或继续。成功读取固定完整索引，
`sc.save.load` 使用该索引恢复房间与跨房间状态，避免再次同步读取。
无磁盘目录的无窗口运行仍用内存存档。损坏或不可读记录释放请求，保留文件，
标题显示重试和需确认的新旅程；完整诊断写入 stderr。新旅程的删除仍同步。

验证范围：

- `build/full/shiny.exe` 编译，Wayfarer `--check-all`、
  `tests/save_read_async.py`、`tests/wayfarer_integration.py`、
  `tests/wayfarer_route_commit.py` 和 3244 帧 `tools/scenario.py` 均通过。
  完整键盘/手柄路线、检查点恢复、结束后返回标题及新旅程均在集成测试中。
- 注入损坏的主检查点后，含首帧 Enter 和一次重试的回放仍停在标题，
  两次错误均可恢复，原文件仍在；
  错误、空槽和有效槽标题以及森林画面由 `--capture-hidden --mute` 生成的原生
  PNG 实际目视检查，分别位于 `build/wayfarer-title-error-20260927/` 和
  `build/wayfarer-title-async-native-20260927/`。错误页的“重试读取”可聚焦，
  文本未越界。没有重新检查已解决的字体栅格化问题。
- `build/wayfarer-title-async-package-20260927/` 已生成独立发行包，
  从源码目录之外运行包内 5 帧短回放通过。
- LLVM-MinGW ASan/UBSan 无窗口构建及 `tests/save_read_async.py` 通过。
  以原测试脚本运行 Wayfarer 3244 帧 sanitizer 完整任务触及其 30 秒超时，
  因此不把该项计为通过或内存错误；本轮未延长测试时间。

仍待实体键鼠/手柄、慢盘交互与人工完整游玩验收。当前应用的写入或删除会使
固定快照失效；外部进程直接改写同一槽位不在一致性契约内。
