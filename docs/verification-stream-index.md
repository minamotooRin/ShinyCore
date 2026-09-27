# 声明式流式索引预读（2026-09-27）

`project.stream_indexes` 将房间路径映射到已构建的 JSON 索引。宿主先加载房间表，
应用内容线程读取并解析索引，结果就绪后调用 `init`；`sc.stream.open` 消费同一路径的
索引并立即提供元数据。候选失败不替换活动房间。未声明索引仍同步打开。

定向验证：

- Full 与 lightweight Release 构建通过；`test_content_loader`、`test_stream`、
  lightweight `test_script`、既有 `room_preload_integration.py` 通过。
- `stream_index_integration.py` 在无窗口与隐藏原生运行中验证成功提交、坏格式拒绝和
  原活动房间保留。两种原生帧见 `build/index-async-native-final/`；已目视检查。
- Wayfarer `--check-all`、45 帧日志回放通过；隐藏原生森林画面
  `build/wayfarer-index-async-final/wayfarer-world.png` 已目视检查。
- Windows LLVM-MinGW 无窗口 ASan/UBSan 下，内容加载器、流式模块、索引集成和
  Wayfarer `--check-all` 通过。Wayfarer 本地 SDK 为 dev.62，哈希审计通过。

验证限度：工作线程负责磁盘读取和 JSON 解析；索引条目的房间内结构构建、Lua
场景脚本、资源预检与标题存档操作仍在主线程。未做大索引主线程耗时、实际弱盘、
Linux/macOS 或长时间硬件验收；此项不等于整个房间加载都已异步化。
