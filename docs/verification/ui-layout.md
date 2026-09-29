# UI 局部布局检查（2026-09-27）

`shiny.ui` 区分几何、可见顺序与文字更新。几何失效沿父链传播，每个节点缓存父级
分配的矩形，干净且分配不变的子树不重新布局。嵌套滚动限幅后的平移同步缓存位置，
避免后续局部更新使用旧坐标。保留普通 Lua 树，没有加入布局框架或新依赖。

已执行：

- 两项新的 Release 用例：文字/颜色修改保留几何、尺寸修改移动后续控件、独立子树
  复用、相同尺寸不失效、隐藏中修改后恢复、窗口变化与显式主题全量失效；嵌套
  滚动、网格列数/间距/锚点、最小尺寸、列表缩短和根显隐的增量结果与完整布局一致。
- 相关锚点、滚动范围/焦点显露/滚动条、列表选择、页签、模态和 Lua 检查接口通过。
- 原生检查器加入局部布局待更新标记，继续原始读取，不触发元方法。该协议检查在
  全功能无窗口 ASan/UBSan 下通过；full 和 sanitizer 的相关目标构建成功。
- 五个带 UI 的示例标准模块同步至 SDK dev.30；八个项目清单审计通过。API 注解未变，
  不含 UI 模块的项目继续使用此前版本。

```powershell
python tests/input_integration.py build/full/shiny.exe . InputTests.test_ui_incremental_geometry_and_text_invalidation InputTests.test_ui_incremental_matches_full_layout_after_nested_changes
python tests/native_ui_layout.py build/full/shiny.exe --output build/ui-layout-reviewed
```

两帧隐藏、不聚焦、静音、隔离存档的实际原生截图已查看：左侧按钮增高、隐藏后
尾部控件排列和焦点框正常；右侧文字改变且控件位置不动，无重叠。快照同时断言
独立矩形复用。命令、状态、日志、引擎/UI 哈希及图片位于 `build/ui-layout-reviewed/`。
捕获后仅补充原生检查器对待更新标记的读取，未改变渲染行为。

没有性能优化专项、压力长测或全套回归；这不代表 500 控件/万条列表性能门槛已验收。
真实设备和跨平台项目仍待完成，已解决的字体与已完成的 IME 检查未重跑。
