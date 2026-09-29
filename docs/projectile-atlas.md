# 图集弹体

完整字段、阶段和错误语义见 [批量弹体契约](projectiles.md)。

在房间初始化中配置弹体池、登记已声明的 PNG 资源区域：

```lua
sc.projectiles.configure(32768)
local spark = sc.projectiles.sprite("effects", 0, 0, 16, 16, 24, 24)
sc.projectiles.spawn{{x=100, y=100, vx=120, radius=3, sprite=spark,
                      color=0xFFFFFFFF, life=2}}
```

`sprite(resource,x,y,w,h,width?,height?)` 返回房间内的 1..64 模板索引。
只能在 load/init 中、configure 后调用；整张 PNG 最大 8192×8192，裁剪必须位于
图片内。显示宽高默认等于裁剪尺寸，范围 `(0,4096]`，中心为弹体位置。
显示尺寸不改变圆形碰撞半径。`color` 为 RGBA 乘色，包括透明度。
未指定 `sprite` 或指定 0 时绘制边长为两倍碰撞半径的纯色方块。
模板索引不是对象持久 ID，不能跨房间保存后直接使用。

登记最多 64 个区域，共享房间纹理缓存；弹体仍使用专用连续数组，不创建
普通实体或逐弹体纹理。spawn 整批检查模板索引，任意无效项使整批失败。
clear 保留模板。绘制保持弹体创建 ID 次序，密集数组删除不改变透明覆盖次序；
只合并相邻同纹理项，不按纹理重新排序。预分配的索引链接在增删时维护次序，
显示索引缓冲区仅在增删后线性重建，不做全量排序。目前弹体统一位于基础
光照之后、粒子之前，不支持逐弹体层级。

无窗口加载读取 PNG 头部尺寸，用于相同的裁剪范围验证；这不代表验证全部
压缩数据或 CRC。图形资源预检另行实际解码图片。当前没有弹体旋转、序列动画、
法线贴图或材质接口；原生批次和 20,000 弹体性能门槛仍需独立验收。

示例为 `examples/projectile_atlas`，复用 Lantern 原创图集，包含裁剪、缩放、
透明着色与删除后的混合绘制。[验证记录](verification/projectile-atlas.md)。
