# Crossing 结局存档失败恢复（2026-09-28）

信号塔满足五盏灯和顺序机关后，游戏仅在结局检查点写入成功时进入完成页。
写入失败则撤销本轮 `complete` 标记，保留当前房间与已收集进度；
按当前存档键或暂停菜单 SAVE 可重试。第三关出口只显示已有 BEACON 标记。

`tests/crossing_integration.py` 使用原始 2876 帧输入走完三关，并将磁盘检查点路径
设为非空目录以注入真实写入错误。最终仍在信号塔的游戏态，观察字段
`crossing.save_error=true`、`campaign.complete=false`，故障目录原有文件未被删除。
另一个临时项目只让首次写入失败，继续输入 F6 后成功落盘并进入结局页。
正常场景清单的八项断言和完整集成回放通过；未在实体设备上手动触发重试。

隐藏、静音的原生捕获位于
`build/crossing-ending-save-fault-20260928/save-failed-reviewed.png`：失败横幅与出口
标记已目视检查。该截图只证明当时的原生画面，不代替人工操控、设备或音频验收。

轻量构建发行包 `build/crossing-ending-save-release-20260928/` 已按内容依赖生成；
从源码目录外启动包内引擎和游戏，十帧 smoke 回放通过。未进行完整包内人工游玩。
