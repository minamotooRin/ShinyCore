# 几何阴影示例

需要启用 `SHINY_ADVANCED_RENDER`。运行 `shiny examples/geometry_shadows`。

方向键移动光源，H 切硬阴影，S 切四样本软阴影。遮挡体包含旋转箱体、胶囊、
三角形、复合身体和地图墙；光源不遮挡自身。画面全部由原创几何组成，无下载资源。
暖色主灯通过逐帧 point 命令投影，右侧蓝色补光不投影；补光不占用实体池。
粉色装饰没有物理身体，通过 bounds 模式投影；O 切换其投影，不改变碰撞或位置。

```sh
shiny --headless examples/geometry_shadows --frames 6 --replay examples/geometry_shadows/smoke.jsonl
```

回放验证设置切换及状态。另已隐藏捕获并查看硬/软阴影，范围见
[视觉记录](../../docs/verification/systems/advanced-render.md)。
契约和限制见 [lighting.md](../../docs/presentation/lighting.md)。
