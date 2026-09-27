# UI 样式定向检查（2026-09-27）

本轮交付 `sc.image.slice`、UI 九点锚定、`skin/theme.skins` 和 ui_panels 示例。
保留原有绘制队列与普通 Lua 树，不增加依赖或渲染资源所有者。

已执行：

- Windows Release `image_regions`：非法 slice 原子拒绝、单命令字段、源区域、
  普通/缩小/不对称翻转与对角几何，以及原有透明排序/裁剪约束通过。
- 两个新的 UI 用例：九锚点随逻辑尺寸变化、row/overlay 对齐、非法布局补丁
  原子拒绝；皮肤复制、主题回退、false 关闭与非法样式补丁通过。相关页签
  布局定向检查通过。
- 全功能无窗口 LLVM-MinGW ASan/UBSan：`image_regions` 和皮肤边界用例通过，
  启用 halt_on_error。没有运行全套回归、性能或压力长测。
- full/lightweight 构建成功，轻量引擎执行 ui_panels 单帧回放通过。API 生成检查
  和完整注解/参考文档一致性用例通过；14 份示例注解同步，8 个项目 SDK 清单
  审计通过。本轮更新的 SDK 为 dev.28，未改动旧 snapshot 示例的模块版本。

原生视觉证据：

```powershell
python tests/native_ui_slices.py build/full/shiny.exe --output build/ui-slices-confirmed
```

两次均为隐藏、不聚焦、静音、隔离存档的单帧实际渲染。`geometry.png` 的四角
像素验证普通/水平翻转/对角变换、小于边框总尺寸、裁剪、透明度及零宽目标；
`anchors.png` 验证九个布局位置及中心焦点。两张均已实际查看：边角没有拉伸，
小尺寸没有角块重叠，裁剪无外溢，示例标签/焦点没有相互覆盖。命令、输入、状态、
日志和引擎哈希在同目录，manifest 标记目视通过。

这不代表所有材质/法线/光照组合、真实设备、跨平台或完整 UI 负载已验收。
UI 增量布局与完整 IME 分句属性仍待完成。已解决的中文字体问题未重新验证。
