# Barrage 后台结算存档（2026-09-28）

标题进入后才请求 `last_result` 摘要；读取期间 UI、设备和绘制继续更新。
结算时结果和 `show_result` 进入显式状态，`write_async` 复制该快照；只有工作线程
完成后才启用 NEW CHALLENGE。失败则释放事务，保留当前结算和结果，显示
RETRY SAVE 与 NEW CHALLENGE (UNSAVED)。无 `--save-dir` 的无窗口运行沿用
内存槽。标题的 LAST RESULT 使用已选定的索引调用 `load` 重建房间。

必要验证：`tests/barrage_result_async.py` 在 full/lightweight/headless 当前构建上
以真实失败结算验证磁盘写入、新进程恢复和阻塞目标路径导致的写入失败；
`result_io` 观察值为
saved/restored/failed；既有 `barrage_integration.py` 的键盘、手柄、六波、暂停、
读结果、重新挑战与失败检查通过；`walkthrough.scenario.json` 的 18138 帧、
九项六波断言通过。后台事务期间 SETTINGS 暂时禁用，因为设置提交仅允许固定更新。
隐藏、静音、未聚焦的原生成功与失败结算图已实际查看，最终图位于
`build/barrage-result-async-reviewed-20260928/result-saved-revised.png` 和
`result-unsaved-revised.png`；损坏槽标题见 `title-read-failed.png`。检查发现底部
提示遮挡 QUIT 后，改为副标题提示并重新捕获，现各按钮完整可见。图形采样只覆盖
失败结局；胜利结局共享保存路径，
但未在此轮重复做完整原生捕获。慢盘、强杀与实体输入/音频仍未验收。

轻量发行包与 ZIP 已生成在同一审查目录的 `package-r2/`、`package-r2.zip`；
包内项目从仓库外工作目录执行 `--check-all` 和 10 帧 smoke 回放通过。这里仍是
Windows 开发包，不代表最终干净系统安装或跨平台发行验收。
