# 粒子存储与基础发射

`project.limits.particles` 指定房间粒子容量，默认 32768，范围 0..65536。
0 关闭系统并释放列存储。房间加载期间分配，模拟和绘制不扩容。

```lua
sc.emit(100, 80, 48, "#77E8C8CC", 80, 1.2)
```

参数依次为世界位置、数量、RGBA 颜色、最大速度、基础寿命。数量范围
0..32768，速度默认 30、范围 0..1e6，寿命默认 0.5 秒、范围 0.001..60。
方向均匀随机，速率为最大速度的 35%..100%，寿命为基础寿命的 60%..100%，
方块大小 1..3 像素。粒子承受世界重力的 15%，透明度随剩余寿命线性降低。

发射整批预检；容量不足时报错，包含 used/requested/capacity，不产生部分
粒子，也不消耗视觉随机数。关闭系统仍可发射零个粒子。成功的粒子发射也
不会改变玩法 `sc.random` 的序列。Lua 只能在 load/init/update 发射，draw 禁止。

原生 `ScParticles` 按位置、速度、寿命、尺寸、颜色分列，活动区间为 `[0,count)`。
更新按创建顺序压紧存活项，只遍历活动数量，不扫描全部空槽；数据列预先分配。
稳定顺序避免删除改变透明覆盖。粒子没有实体、刚体、持久 ID 或逐帧 Lua 回调。
绘制使用白色纹理批量四边形和视口剔除，仍在弹体之后、UI 之前。

`--profile` 分别记录活动粒子数与已分配容量。状态诊断忽略活动区间以外的
残留列数据；它仍不是存档或完整未来状态。

## 可配置发射器

`sc.particles.define(spec)` 在 load/init 登记只读模板，返回房间限定句柄。
每个房间最多 64 模板，容量为零时拒绝登记；不能在 update/draw 定义或修改。
`sc.particles.burst(id,x,y,count)` 在 load/init/update 整批发射，返回数量。
count 范围 0..65536，其余容量和随机数失败语义同基础 emit。旧房间句柄无效。
`sc.particles.stats()` 返回 capacity、used、available、emitters。

| spec 字段 | 默认 | 范围与含义 |
| --- | --- | --- |
| speed_min / speed_max | 30 / 30 | 0..1e6，最小不大于最大 |
| life_min / life_max | 1 / 1 | 0.001..60 秒，最小不大于最大 |
| angle_min / angle_max | 0 / 2π | -1e6..1e6 弧度，最小不大于最大，0 朝右、π/2 朝下 |
| gravity | 0.15 | 世界重力的 -10..10 倍 |
| curve | 见下文 | 2..8 个关键点，按归一化寿命插值 |
| texture | 无 | `{resource,x,y,w,h}`，已声明 PNG 的整数裁剪区域，必须完全位于图片内 |
| blend | `"alpha"` | `"alpha"` 或 `"additive"`，普通透明或按源透明度加色 |

curve 的每个关键点必须恰好包含 `{time,size,color}`：time 从 0 严格递增至 1，
size 为 0..4096 像素，color 为 32 位整数 RGBA。尺寸和各颜色通道分别线性
插值，颜色四舍五入到字节。默认曲线是 `{0,2,0xffffffff}` 到 `{1,0,0xffffff00}`
（此处简写值，实际字段须具名）。曲线颜色已经包含完整透明度，不再叠加
基础 emit 的寿命淡出。模板数据复制后保存在原生侧，不引用原 Lua 表。

`require("shiny.particles")` 提供每发射器一次 Lua 更新的辅助模块：

```lua
local P = require("shiny.particles")
-- init
local sparks = P.new({speed_min=30, speed_max=90, life_min=.5, life_max=1}, 120)
-- 固定 update(dt)，每秒 120 个，保留不足一个粒子的余数
P.update(sparks, dt, 100, 80)
P.burst(sparks, 100, 80, 20)
```

rate 范围 0..65536，update 的 dt 范围 0..1 秒；rate、enabled 可以显式修改。
enabled=false 或应用模拟暂停时不积累发射量。容量失败不推进余数；不会隐藏
错误或自动减少发射数量。逐粒子运动与曲线计算在原生固定更新执行。

纹理引用项目稳定资源名，不是路径；最大图片尺寸 8192×8192。所有模板复用
房间纹理缓存，不为粒子创建独立纹理。位置锚点为左上角；曲线 size 定义
显示矩形最长边，按裁剪区域保持宽高比。未指定纹理仍绘制方块。

普通透明 RGB 公式为 `source*alpha + destination*(1-alpha)`，叠加为
`source*alpha + destination`，结果限于目标颜色范围。纹理透明度与曲线透明度
相乘。不同纹理或混合模式之间只拆分连续批次，保持创建次序；完成后恢复
普通透明状态。画面目标已完成合成，复制到最终视口时不会再次乘透明度。
不提供正片叠底，以免把后端不正确处理纹理透明区域的公式作为公开能力。

示例为 `examples/particles`、`examples/particle_materials`。原生像素验证使用
`python tests/particle_materials.py build/full/shiny.exe --output build/full`，需要
开发依赖 Pillow。以上功能不代表组合弹幕性能门槛已经通过。
