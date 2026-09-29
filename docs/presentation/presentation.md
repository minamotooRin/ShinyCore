# 显示插值

`sc.presentation` 的显示插值把前后两个固定帧的显示位置混合起来，不写回世界、Box2D、存档、
输入或随机数。默认关闭；像素取整独立由 `sc.camera.set{pixel_snap=...}` 控制。

同一命名空间的 attach/detach/attachment 提供[原生视觉附着](../world/attachments.md)。
附着同步固定世界姿态，显示插值沿父链计算；只读绘制不回写模拟。

```lua
-- init：按已配置的房间容量分配历史数据。
sc.presentation.interpolate(true)
sc.camera.set{pixel_snap=false}

-- update：瞬移显式跳过本帧插值。相机切镜头时单独跳过。
sc.set(player, {x=800, y=100})
sc.presentation.snap(player)
sc.camera.follow(player)
sc.presentation.snap_camera()

-- draw：手写附着标记、精灵命令或点光源时使用显示姿态。
local p = sc.presentation.pose(player)
sc.text("Player", p.x, p.y-14, 10, "#FFFFFF")
```

原生实体、实体光源、动态遮挡、弹体和粒子自动使用显示位置。法线与实体共用显示角度。
流式地图使用显示相机的可见范围及视差锚点。UI 保持屏幕坐标；没有对象身份的普通绘制命令
不会被猜测或自动补间，应在 draw 中用 pose 计算它们的位置。

## 时间与生命周期

- 更新前捕获上一个固定状态，更新后只读混合；同一模拟帧可以多次绘制。
  `draw(alpha)` 的 0 对应前一帧，1 对应当前帧；实时画面最多滞后一个固定帧。
- 无窗口、`--frames N`、加载边界等待、调试暂停/单步使用 alpha=1，显示最新完成状态。
  新房间没有历史数据，首次绘制直接显示初始状态；切换失败不改变活动房间的历史数据。
- 实体位置和角度插值，角度沿最短弧。尺寸、形状、颜色、纹理帧和图层使用当前值；
  快速旋转每帧超过 π 时仍选择最短弧，可改用 Lua 中的显式连续角度绘制。
  弹体和粒子只插值位置，颜色/大小曲线在固定更新中推进。
- 更新中生成的对象直接显示当前位置，销毁即时隐藏。实体按完整生成号匹配，槽位复用不会
  继承旧对象位置。批量弹体交换删除、粒子稳定压缩时，其历史位置随对象一起移动。
- `snap(id)`、`snap_camera()` 只跳过当前帧混合，不改变模拟位置。它们和 interpolate
  可在 load/init/update 调用，draw/ui_update 不可修改设置。
- 第一次开启必须在 load/init。之后可在 update 开关；关闭保留存储，重新开启从新状态
  建立历史。没有开启过的房间不分配历史缓冲区。

`sc.get()` 始终返回模拟数据。`sc.presentation.pose()` 在 draw 返回显示姿态，
其他阶段返回固定姿态。相机转换也遵循同一规则：update 的瞄准/点击使用固定相机，
不受绘制频率影响；draw 中的标记、鼠标光标使用显示相机。
不要把 draw 的结果写入后续玩法状态，否则作者代码仍可能把显示频率引入玩法。

`sc.camera.read()` 的配置字段 x/y/zoom/rotation 保持固定值；center_x/y、visible、
anchor_x/y、view_zoom/view_rotation 是当前调用阶段的有效显示值。anchor 不包含震动，
供地图视差使用；to_screen/to_world 使用包含震动的中心，像素取整发生在混合之后。

`sc.presentation.stats()` 报告 enabled、reserved、ready，以及实体/弹体/粒子历史容量和
bytes（数据大小，不含分配器开销）。历史数组按项目容量或显式弹体容量加载时分配；
捕获、混合、删除和瞬移不扩大容量。原生嵌入调用方先配置池，再开启插值；每个固定帧
在修改世界前调用 `sc_presentation_capture`，`sc_step` 在没有预捕获时提供兜底捕获。

[相机示例](../../examples/camera/README.md)包含开关与瞬移演示。CPU/脚本测试验证分数 alpha、
最短角度、生成号、压缩删除、光源遮挡对齐，以及同一回放开启/关闭插值后的玩法与存档一致性。
原生像素效果仍未验收；没有用无窗口结果替代 GPU 验证。

2026-09-26 必要验证：Release 的 presentation、script、camera、occlusion、
presentation_replay 通过；无窗口 ASan/UBSan 的 presentation、script、occlusion、
presentation_replay 通过。流式图块视差检查及相机示例四帧开关/瞬移回放通过。
完整和轻量图形构建编译、API 文档一致性检查通过。未运行性能、长时间或图形设备测试。
