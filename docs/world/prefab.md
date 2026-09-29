# 对象模板

`shiny.prefab` 用普通 Lua 表组合实体默认值、命名子对象和游戏组件数据。
项目携带模块副本；本页数组覆盖修复最早随 SDK `1.0.0-dev.59` 分发，
具体项目版本以其 `shiny-sdk.json` 为准。

```lua
local Prefab = require("shiny.prefab")
local definition = {
    entity = {w=20, h=10, body=false, solid=false},
    children = {
        lamp = {x=20, y=0, w=4, h=4, angle=math.pi/4,
                solid=false, color="#FFC98C"},
    },
    components = {health=3, inventory={"key"}},
}
local item = Prefab.spawn(definition, {
    entity = {x=100, y=100, angle=math.pi/2, persistent_id="gate"},
})
sc.set(item.id, {x=120}) -- 子对象自动同步，无需 Prefab.update。
local lamp = item.children.lamp.id
sc.presentation.attach(lamp, item.id, {x=22, y=0, angle=math.pi/4})
local health = item.data.health
Prefab.destroy(item)
```

`merge(defaults, overrides)` 返回独立表：仅含字符串键的映射逐字段递归合并；
含任意数字键的表按数组整体替换，即使下标稀疏或覆盖值为空表。需要合并的字典
请使用字符串键。合并前拒绝元表和循环，不修改输入；省略 overrides
等价于空表。components 只保存在实例 data 中，不自动执行游戏逻辑。

spawn 按名称排序，将根和命名子对象通过 `sc.spawn_many(specs, parents)` 一次
创建。子对象直接使用实体字段，x/y/angle 为局部偏移；原生端同时预检容量、
对象持久 ID、刚体/速度限制和最终世界姿态。失败不创建实体、不改变生成号，
也不留下失败创建的 ID 记录。实例提供 id、ids、children[name].id、data。

叶子仍可直接写实体字段；多层对象用 `{entity=..., children=..., components=...}`
包装一个子对象。每层按名称排序，深度优先装入同一原生批次；`children`、
`components` 可省略，包装对象只接受这三个字段。例如：

```lua
local item = Prefab.spawn{entity={x=100,y=80,body=false},children={
    lamp={entity={x=20,w=8,h=8,solid=false},components={lit=true},children={
        glint={x=3,y=-2,w=2,h=2,solid=false},
    }},
}}
local glint = item.children.lamp.children.glint.id
assert(item.children.lamp.data.lit)
```

实例的 `ids` 按根、已排序子树的前序排列；销毁按逆序释放。最多 32 条父边；
无效子对象或容量不足使整批失败。子对象组件只存于实例，不自动执行规则。

子对象使用[原生视觉附着](attachments.md)：刚体根完成物理步进后自动跟随，
支持层级显示插值。模块不再保留自己的坐标计算或 update 函数。修改根用 sc.set，
修改局部姿态用 sc.presentation.attach；读取用 sc.get 或 presentation.pose。
每个对象可显式填写不同 persistent_id；模块不派生 ID。

模板 children 支持上述多层命名树；原生批量接口至多 32 条父边。
尺寸、翻转、图层和 solid 不继承。纯视觉子对象建议 solid=false，不能
持有刚体或非零速度。原生销毁父对象会保留直接子对象；Prefab.destroy 则明确
销毁整个实例：先验证所有句柄，再按创建逆序销毁，首次成功返回 true，重复调用
返回 false。如果游戏提前销毁成员，destroy 报错并保留其余成员，由游戏清理。

实例表和运行时句柄不是存档格式。[持久关系示例](../../examples/attachments/README.md)
展示按对象持久 ID 保存父子关系，在新房间中原子重建。
流式地图对象可由 [stream_objects](../content/stream-objects.md) 持有整棵 prefab；它复用
`Prefab.plan` 展开数据，并在原生联合提交后用 `Prefab.bind` 填入句柄。
这两个低层函数不单独创建实体，普通游戏对象直接使用 `Prefab.spawn`。

## 定向验证

`tests/prefab_integration.py` 覆盖合并、局部中心/角度、自动同步、失败创建不保留 ID、
容量不足与失效成员；`test_attachment` 和 `attachment_integration.py` 覆盖前向父索引、
循环、深度、最终姿态、批量失败不改变世界/草稿及 Lua 严格参数检查。
上述检查在 Release 和全功能无窗口 ASan/UBSan 通过。

`attachment_save_integration.py` 执行示例的 40 帧保存/解除/重置/读档回放，验证
保存的关系/姿态与恢复结果相同、句柄重新生成。隐藏原生图
`build/attachment-save-reviewed/restored.png` 已实际查看，原生/无窗口固定字段一致。
Crossing 使用此模块的短冒烟检查见同目录记录；未运行全套测试或性能负载。

2026-09-27：新增嵌套命名子对象及逐节点组件数据，仍由一次 `spawn_many`
原子创建。定向宿主检查覆盖两级附着、祖先移动、深层失败回滚和逆序销毁；
隐藏静音原生图 `build/prefab-nested-reviewed/attachments.png` 已实际查看，
三个旋转示例的小亮点均跟随灯体。使用该模块的五个项目 SDK 更新为 dev.56；
流式对象的复合 prefab 归属已在后续工作整合。

2026-09-27：修复稀疏数组覆盖值 `{}` 未清空旧数据的问题。先在真实宿主中复现
断言失败，修复后 prefab 与流式复合对象用例通过；五份项目本地模块/SDK
更新为 dev.59，内容检查与 SDK 审计通过。Crossing 渡台、Wayfarer 森林的隐藏
原生截图已目视检查，后者与修改前逐像素一致。无样例玩法或渲染规则改动。
