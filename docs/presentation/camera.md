# 相机与坐标

相机归房间所有，不依赖图形模块。普通构建和无窗口回放共用 `sc.camera`。

```lua
sc.camera.set{zoom=1.5, rotation=.2, pixel_snap=false, bounds=false}
sc.camera.follow(player)
sc.camera.shake(8, .4, 42)
local mx, my, inside = sc.input.mouse()
if inside then
    local world = sc.camera.to_world(mx, my)
    -- world.x / world.y 可用于瞄准和选择
end
```

- `set(patch)` 整体校验后提交。未知字段、非法类型、非有限数和越界值失败时保留旧值。
  可在 load/init/update 调用；draw/ui_update 只能读取。
- `x/y` 是未缩放视口的左上角锚点，范围 ±1,000,000；设置任一项解除跟随。
  缩放和旋转围绕 `x + width/2, y + height/2`，单独修改它们保留跟随。
- `zoom` 为 0.125..8，默认 1；`rotation` 为弧度，正值让画面顺时针旋转，
  输入 ±1000，提交后归一到 ±π。`pixel_snap` 默认 true，向下取整相机和绘制对象原点；
  高清或分数运动可设 false。它与窗口的整数/平滑缩放设置独立。
- `follow(id)` 立即对准实体中心并应用边界，后续固定更新按 `smoothing` 接近目标。
  smoothing 默认 0.16，范围 0..1；0 保持当前位置，1 立即跟随。`follow(nil)` 解除跟随。
  销毁目标自动解除；失效/跨房间句柄不能重新跟随。
- `bounds` 默认 `"map"`；流式地图解除有限边界后自动变为无界相机。
  false 显式关闭地图约束；`{x,y,w,h}` 设置自定义矩形。宽高须为正，矩形端点在 ±1,000,000 内。
  边界按旋转、缩放后的视野外接矩形约束；范围比视野小时对齐可见区域左/上边缘。
  像素取整可越过边界不足一个世界像素，震动也可暂时超出边界。
- `shake(amplitude,duration,seed?)` 替换当前震动，幅度为 0..256 世界像素，时间 0..60 秒。
  时间向上取整为固定帧数，线性衰减；任一为 0 即取消。默认 seed=1，支持 uint32。
  每帧从独立整数混合计算偏移，多次 draw 不推进，不消费玩法或粒子随机数。
  游戏模拟暂停时，相机跟随与震动仍随固定更新推进；调试暂停不推进。
- `read()` 返回配置、跟随目标（0 为无）、实际中心和可见世界外接矩形 `visible`。
  anchor_x/y 表示不含震动的有效锚点，view_zoom/view_rotation 表示有效缩放和旋转。
  `to_world(x,y)` / `to_screen(x,y)` 返回 `{x,y}`，都采用当前阶段的像素取整和震动；开启显示插值时 draw 使用显示视图，其余阶段使用固定帧。
  输入是逻辑视口坐标，不是桌面像素；窗口缩放和黑边已由输入层处理。鼠标的第三返回值
  `inside` 标记是否在视口内，转换函数不会自动裁剪坐标。

地图、实体、世界绘制、弹体、粒子、遮挡和光源使用同一变换；屏幕 UI 保持固定。
法线随相机旋转，光源高度按 zoom 换算。带 `screen=true,layer=...` 的图像保持屏幕坐标，
仍在指定世界图层、光照前绘制。后处理作用于变换完成的世界画面，普通屏幕 UI 在其后绘制。

相机设置不自动写入存档；游戏可显式存储所需字段并用 set 恢复，不保存 target 运行时句柄。
现有诊断 hash 包含位置和跟随句柄，不包含新增视觉参数；需要比较相机设置时显式 watch/read。
[显示插值](presentation.md)默认关闭，可在 init 预留历史数据并开启；玩法查询保持固定状态。

[相机示例](../../examples/camera/README.md)提供旋转、缩放、震动与鼠标世界坐标演示。
必要验证覆盖 CPU 数学、Lua 原子提交/阶段/房间重置及无窗口回放。
原生 GPU 像素仍未验收，本次未启动图形窗口。

2026-09-26 必要检查：Release 的 camera、camera_api、core、script、occlusion 通过；
无窗口 ASan/UBSan 的 camera、camera_api 通过；示例四帧回放通过。
完整和轻量图形构建编译通过，API 文档检查通过。流式图块、重复图像层、独立图像图集的
三个相关检查，以及 Wayfarer 的两帧启动绘制检查通过；流式绘制现提交世界坐标，
默认读取原生相机的可见范围，保留视差和图层排序。
只重跑了修正涉及的检查，未运行全量、性能或长时间压力测试。
