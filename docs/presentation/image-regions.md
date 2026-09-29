# 图片区域绘制

`sc.image(resource, x, y, w, h, options)` 在 draw 中提交图片命令，资源名必须
来自项目的 image 声明。第六参数可省略，也可继续传 screen 布尔值。

```lua
sc.image("tiles", 64, 32, 16, 16, {
    source_x=32, source_y=0, source_w=16, source_h=16,
    flip_x=true, color="#FFFFFFC0",
})
```

options 必须是无元表的普通表，未知字段被拒绝：

| 字段 | 默认值 | 约束 |
| --- | --- | --- |
| source_x / source_y | 0 | 非负源像素坐标，最大 8192 |
| source_w / source_h | 0 | 同为 0 表示整图；否则必须同时为正，最大 8192 |
| flip_x / flip_y | false | 水平/垂直翻转 |
| diagonal | false | 在翻转后交换采样的 u/v，供图块变换使用 |
| color | #FFFFFFFF | #RRGGBB 或 #RRGGBBAA，包含透明度 |
| screen | false | true 使用屏幕坐标，否则使用世界坐标 |
| layer | 不指定 | 指定 -32768..32767 整数时参与场景图层排序 |
| slice | 不指定 | 普通表 `{left=0,right=0,top=0,bottom=0}`，非负源像素，最大 8192 |

源区域必须在 PNG 尺寸内；无窗口与图形环境使用相同资源尺寸检查。
整图模式要求源坐标也为 0。目标坐标范围为正负一百万，宽高为 0..4096，
所有数值必须有限。源区域选择、翻转与透明度不创建普通实体，不改变玩法状态。
未指定 layer 的命令沿用原有顺序和裁剪栈。显式 layer 的图片与原生地图层、
实体按层号从小到大混合绘制；同层先地图，再按提交顺序绘制图片，最后按
稳定槽位绘制实体。同层透明图片不因纹理切换而重排。

分层图片在场景光照之前绘制；screen 只决定坐标如何解释，不再表示 UI 覆盖。
分层图片不能嵌套在 `sc.clip` 中，draw 校验会拒绝，避免重排后裁剪失效。
普通 UI 图片不要指定 layer。排序暂存区在房间资源准备时分配，逐帧复用。

## 九宫格面板

```lua
sc.image("panel", 24, 24, 200, 80, {
    screen=true, slice={left=4, right=4, top=4, bottom=4},
})
```

slice 相对选定源区域（或整张 PNG），相对两侧的和必须小于源宽/高，保留正的
中心区域。边角保留源像素对应的逻辑尺寸，边沿只沿一轴拉伸，中心沿两轴拉伸。
目标小于边框总宽/高时，两侧按比例共同缩小，中心缩为零，不产生边角交叠。
宽/高为零不输出四边形。翻转和对角交换同时变换源采样和对应边框宽度。

仍只占一个 draw 命令，后端最多提交九个相邻四边形；不创建实体、不改变透明
排序，继续遵守裁剪、染色、材质与图层契约。非法 slice 在命令入队前拒绝。
`ScImageOptions`、`ScImageSlice` 的字段、默认值及调用阶段现由 `--api` 生成。

这是[流式图块绘制](../content/stream-tiles.md)使用的底层接口。原生九宫格证据见
[UI 样式检查](../verification/systems/ui-style.md)；不据此宣称所有材质/光照组合已验收。
