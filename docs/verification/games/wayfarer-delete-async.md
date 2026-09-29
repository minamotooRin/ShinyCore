# Wayfarer 异步清理检查点（2026-09-28）

`sc.save.delete_async(slot)` 将磁盘槽、备份和引擎持有的块文件交给应用级存档线程。
提交只返回请求号；宿主在固定边界发布 `pending/complete/failed`，请求释放之前
禁止其他存档操作和切房。`status.operation` 为 `delete`，失败可重试或释放。
无磁盘目录时返回 `nil,error`，原有同步 `delete` 继续服务内存槽。

Wayfarer 在确认新旅程后等待删除完成，再清除共享状态并进入森林。失败时释放
请求，原标题房间继续运行，显示重试/返回模态。删除是可重复操作，但 I/O 故障
可能发生在移除主索引之后；失败时不承诺磁盘完全未变。空槽也提交清理，避免
前次中断遗留的块文件。此行为使样例完整回放顺延一帧至 3245 帧。

本轮执行的验证：

- Full 与 lightweight 构建、`test_save_chunks --async`、`save_delete_async.py`、
  `save_contracts.py`、API 生成一致性及完整构建 `test_contracts.py` 通过。
  定向原生测试覆盖工作线程、失败重试、槽/备份/块清理、缺失槽幂等，
  并检查块目录残留非引擎文件时报告失败；脚本测试
  覆盖事务排他、状态发布、失败释放与无磁盘目录。
- Wayfarer 键盘/手柄完整流程、道路发布断言、3245 帧场景断言和 `--check-all`
  通过。独立发行包位于 `build/wayfarer-delete-async-release-20260928-r2/`，从源码
  目录外运行包内短回放通过。
- 在 `checkpoint.json.bak` 放入非空目录注入删除错误；`wayfarer_integration.py`
  验证标题保持活动且进入 `delete_error` 模态。实际隐藏、静音原生截图
  `build/wayfarer-delete-fault-20260928/delete-error.png` 已目视检查：重试和返回
  按钮可见且焦点清楚。新旅程后的森林原生图
  `build/wayfarer-delete-async-visual-20260928/wayfarer-world.png` 也已检查。
- LLVM-MinGW ASan/UBSan 无窗口构建的 `test_save_chunks --async`、
  `save_delete_async.py`、`save_contracts.py` 通过。未重复耗时的完整 sanitizer
  游戏流程或性能长测。

仍待实体输入、真实慢盘交互与其他平台/硬件验收；上述短回放和截图不能替代它们。
