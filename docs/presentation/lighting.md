# 几何遮挡与软阴影

`SHINY_ADVANCED_RENDER=ON` 提供 `geometry_shadows` 和 `sc.lighting`；默认构建
保留轻量 ASCII 格阴影。advanced_render 汇总标志跟随同一构建开关；[法线贴图](normal-maps.md)
已接入独立 normal_maps 能力，已有定向原生检查，完整验收仍未完成。

```lua
-- load/init/update；未提供的设置保持不变，整批预检后提交。
sc.lighting.configure{softness=8,samples=4}
-- 这里的实体只显示光源图案；实际照明由 draw 命令提供。
local lamp=sc.spawn{x=80,y=100,w=16,h=16,color="#FFE4AF",
    body={type="kinematic",shape="circle"}}
local info=sc.lighting.stats() -- draw/ui_update 也可读取
-- draw：显式光源不创建实体、句柄或持久数据，每次 draw 开始自动清空。
local p=sc.get(lamp)
sc.lighting.point{x=p.x+8,y=p.y+8,radius=200,color="#FFE4AF",ignore=lamp}
sc.lighting.point{x=260,y=120,radius=100,intensity=.4,color="#709EFF",shadows=false}
```

point 字段由原生元数据生成到 ScPointLight 注解与 API 参考。x/y 必填，范围
±1000000；radius 默认 128，范围 0.001..1024；intensity 默认 1，范围 0..1。
color 为 #RRGGBB 或 #RRGGBBAA，默认白色，alpha 乘入强度，RGB 不做自动提白。
shadows 默认 true；softness/samples 省略时继承提交时的全局设置，可逐灯覆盖。
shadows=false 时使用单个中心样本且不查询遮挡。ignore 默认 0（无排除），非零必须
是当前房间的有效实体句柄，排除该实体全部形状。所有字段预检后才追加命令。

既有实体 glow 继续从实体中心生成光，沿用提白色调、0.95 强度和全局采样设置，
自动忽略自身身体。它与 point 共用可见光预算。若同一对象由 point 控制，应不设置
glow，避免重复发光。显式 point 数据及质量设置不写入物理世界、玩法 hash 或存档。

softness 是光源采样半径（像素，0..32，默认 0），samples 为 1..8 整数，默认 1。
零半径实际只使用一个中心样本；一个样本也始终位于中心。多个样本均匀分布在
光源圆周，等权叠加生成近似软阴影；不使用随机数，不影响模拟、命中或存档。
这是有限采样的二维面积光近似，未提供物理精确光照或 HDR。

遮挡来自同一房间的已提交几何：

- ASCII 实心格合并为行矩形；不再逐光线查询格子。
- Tiled 和流式地形的矩形/凸多边形，坐标与局部修改由既有地图提交路径提供。
- 实体的静态、运动学、动态身体，包括旋转及最多四个局部复合形状。
- 圆固定近似为 16 边形；胶囊为 18 点双半圆轮廓。多边形使用已有顶点。

默认忽略无身体装饰、非 solid 身体、传感器、单向平台。glow 自动排除自身，point
通过 ignore 显式排除指定实体的全部遮挡形状。
不把求解器的虚拟世界边界当作可见遮挡。光源位于其他闭合遮挡体内或边界上时，
该样本不投出光。

## 独立视觉遮挡

```lua
local decor=sc.spawn{x=120,y=80,w=32,h=48,angle=.3,sprite="sign"}
sc.lighting.occluder(decor,"bounds") -- load/init/update，不创建物理身体
sc.lighting.occluder(decor,"none")   -- 保留物理、绘制和交互，只关闭该实体投影
local mode=sc.lighting.occluder_mode(decor) -- draw/ui_update 也可读
```

| 模式 | 遮挡规则 |
| --- | --- |
| body（默认） | 身体存在且 solid，排除 sensor/one_way；使用现有身体形状 |
| bounds | 使用随实体 angle 旋转的 w/h 矩形，与身体是否存在及物理过滤无关 |
| shape | 使用实体当前存储的几何（含复合形状），忽略身体存在性及 solid/sensor/one_way；默认无身体实体为矩形 |
| none | 该实体不参与遮挡；地图地形不受影响 |

这不是纹理 alpha 轮廓提取；bounds 使用完整矩形。设置可在固定 update 中修改，
下一次绘制读取最新位置、尺寸、角度和形状。忽略光源自身/ignore 的规则仍然生效。
未知模式或失效句柄整次拒绝；body 清除覆盖，恢复默认规则。

每个房间按 project.limits.entities 在加载期分配控制记录，更新期间不扩容。
记录绑定完整生成号句柄，销毁、槽复用或切房间不会把旧设置带给新对象。
设置只属于显示数据，不修改刚体、实体字段或玩法 hash，也不自动存档；恢复对象时
由游戏重新应用。stats.occluder_overrides 只计活着的非默认覆盖，override_capacity
为记录容量；这两项在无窗口可读取，不需要 GPU。遮挡多边形仍使用原有 8192 总预算。

可见轮廓使用 128 条径向基础射线，并在每个遮挡顶点方向及两侧补射线，
保留窄遮挡和尖角；返回排序闭合轮廓。光照仍在世界合成后相乘，屏幕 UI 在其后。
法线贴图已接入；分高度遮挡及高级相机变换接入仍待实现。

渲染器在候选房间准备时分配固定缓冲，帧间复用，不修改世界或同步物理。
最多 8192 个收集区域内的遮挡多边形、32 条 point 命令、32 个可见光源，其中
最多 16 个投影；复合身体分别计数，glow 与 point 光源合并计数。命令槽包含
视口外/零强度命令，可见光预算排除视口外、零强度或 alpha 为零的光。
收集区域为相机视口向外扩展 1056 像素；无可见投影光时不收集。
命令超限在 point 中抛错且不追加，合并可见光预算在 draw 结束时统一检查，
无窗口和图形语义相同。候选超限保留旧房间；活动房间失败明确报告并停止运行，
不静默丢弃光源或遮挡。
候选可暂时额外持有一套缓冲。暂未实现按项目配置这些容量。

stats 返回 softness、samples、effective_samples、occluders、required_occluders、
occluder_capacity、lights、light_capacity、shadow_lights、shadow_capacity、point_commands、
normal_maps、normal_capacity、normal_target_bytes、normal_budget_bytes、status、error。
另有 occluder_overrides 和 override_capacity，语义见上文。
point_commands 是当前/最近一次 draw 已接收命令数量，normal_maps 是已绑定关系数；
原生提交用量在无窗口下保持零、status 为 pending；不能用于决定玩法。
关闭调试光照时保留最后一次提交数据。修改设置后状态重新 pending。

## 示例与验证

[示例](../../examples/geometry_shadows/README.md)展示移动光源、旋转身体、胶囊、
三角形和复合遮挡，可切换硬/软阴影；项目自带 LuaLS 注解及六帧回放。

2026-09-26：纯 CPU 定向测试覆盖地形/修改、两种顶点绕序、旋转身体、圆/胶囊、
复合间隙、自身排除、容量失败、缓冲复用和样本等权对称。无窗口接口测试覆盖
设置原子拒绝、边界、调用阶段、诊断和示例切换。
逐光源扩展另外覆盖逐帧命令清空、颜色/范围/失效 ignore 拒绝、命令追加原子性、
无投影轮廓、16 灯/8 投影收集、glow 合并预算及元数据的 draw-only 调用阶段。
这些定向检查在 Release、无窗口 ASan/UBSan 和关闭模块构建下均通过。
独立遮挡扩展的同类检查也通过：装饰矩形及旋转、复合 shape/bounds 差异、none、
设置失败原子性、默认恢复、失效/复用句柄、房间重置；检查同时确认不创建物理世界
或改变玩法 hash。示例增加 O 键切换无身体装饰的投影，实际像素仍未验收。
原生像素、柔化质量、光照负载及 GPU 资源生命周期尚未验收；本轮未启动窗口。
上述 CPU 与 Lua 接口检查在 Release 和无窗口 ASan/UBSan 下均通过；轻量构建
通过能力拒绝及接口缺省检查。API 文档与各项目注解已生成并检查一致性。
仅执行本次改动相关检查，没有全套回归、性能优化或压力测试。

2026-09-27：提高示例地面可见度，实际查看硬阴影与四样本软阴影，并检查
原生提交状态及像素差异。详见[视觉记录](../verification/systems/advanced-render.md)；
这不替代光照负载、全部几何组合和 GPU 生命周期验收。
