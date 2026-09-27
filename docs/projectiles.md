# 批量弹体契约

`sc.projectiles` 的七个接口、参数、调用阶段及三个记录类型均由原生注册元数据
提供给 `--api`，并生成 [LuaLS 注解](api.lua) 与 [字段参考](api-reference.md)。
浮点字段的读取校验和元数据共用描述，默认值来自原生 `ScProjectileSpec`。

```lua
-- load/init：只分配一次；省略容量时使用 project.limits.projectiles。
sc.projectiles.configure()
-- update：整批创建，返回与输入顺序对应的房间内弹体序号。
local ids = sc.projectiles.spawn{{x=40, y=60, vx=240, radius=3, life=2}}
-- update 中看到上一个已完成模拟步的命中。
for _, hit in ipairs(sc.projectiles.hits()) do
  -- hit.projectile 对应 ids；hit.target 为实体句柄，地形命中为 0。
end
```

| 接口 | 阶段与行为 |
| --- | --- |
| configure(capacity?) | load/init；容量 1..项目上限，nil/省略使用上限。项目默认 32768，0 禁用；配置前不分配池 |
| sprite(resource,x,y,w,h,width?,height?) | load/init，配置后；最多 64 个 PNG 区域；显示尺寸省略/nil 使用裁剪尺寸 |
| spawn(specs) | load/init/update；密集普通数组，空数组合法；整批预检后提交 |
| clear() | load/init/update；清空弹体和命中，保留序号进度、容量及图集登记；未配置时无操作 |
| count() | 所有脚本阶段；未配置时返回 0 |
| stats() | 所有脚本阶段；独立快照，包含 limit/capacity/used/available/sprites |
| hits() | 所有脚本阶段；独立快照，按弹体序号、命中比例、目标排序；暂停保留最近一步，clear 清空 |

弹体的 x/y/vx/vy/ax/ay 默认 0，范围 ±1000000；radius 默认 2，范围
0.001..256；life 默认 3 秒，范围 0.001..3600。mask/color 默认 0xFFFFFFFF，
为 32 位无符号整数；color 使用数值 RGBA。sprite 默认 0（纯色方块），非零时
必须已登记。terrain 默认 true，piercing 默认 false。数字必须有限，数字字符串、
未知字段、元表、稀疏数组和多余参数均被拒绝。

无效批次、容量不足和未登记图集不会部分生成弹体，也不消耗序号。
序号小于 2^52，仅在当前房间内递增；它不是实体句柄或对象持久 ID，不能作为
跨房间存档引用。命中包含 projectile/target/fraction/x/y，坐标为撞击时圆心。
更多碰撞与表现限制见 [地图碰撞](projectile-terrain.md) 和 [图集](projectile-atlas.md)。

本轮必要验证：Release 与全功能无窗口 ASan/UBSan 均通过
`tests/projectile_contracts.py`，以及既有容量、图集、批量字段校验三项测试。
覆盖严格调用、阶段限制、失败原子性、64 图集预算和真实命中快照；生成文档检查与
16 个项目 SDK 审计通过，版本 dev.43。记录在 `build/projectile-contracts-reviewed/`。
本轮没有渲染改动，不重复截图或性能测试；这不代表完整弹幕性能或产品验收。
