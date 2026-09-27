# 固定更新补间组合

`require("shiny.tween")` 使用普通 Lua 数值表，独立于世界、物理、GPU 和随机数。
目标字段可供 draw 读取，或由游戏显式提交到 sc.set；模块不自动修改实体。

```lua
local Tween=require('shiny.tween')
local panel={x=-100,alpha=0}
local timeline=Tween.sequence{
    Tween.parallel{
        Tween.new(panel,{x=20},.3,'out_quad'),
        Tween.new(panel,{alpha=1},.2,'linear'),
    },
    Tween.delay(2),
    Tween.new(panel,{alpha=0},.2,'linear'),
}
-- 固定 update：
local done,remaining=Tween.update(timeline,dt)
-- 离开界面：
Tween.cancel(timeline)
```

| 接口 | 契约 |
| --- | --- |
| `new(target,values,seconds,easing?)` | 正有限时长；默认 smooth，可选 linear/in_quad/out_quad。目标与目标值为无元表的表，字段必须为有限数值。复制目标值，实际开始时捕获起点。 |
| `delay(seconds)` | 有限非负等待；零立即完成。 |
| `sequence(items)` | 按稠密数组顺序执行，上一节点剩余时间传给下一节点，可嵌套。 |
| `parallel(items)` | 同时推进所有子节点，全部完成才结束；剩余时间由最晚结束的分支决定。 |
| `update(root,dt)` | 有限非负时间，返回 done 和本次未使用秒数。已完成/取消节点返回 true 与完整 dt。 |
| `cancel(node)` | 递归取消未完成节点，保留当前值，不跳至终点；重复调用无副作用。 |

`done` 和 `cancelled` 可读；自然完成时 cancelled 为 false。取消某个子节点会使
组合跳过它，不隐式取消父级；取消根节点则取消所有尚未完成的后代。重播应创建
新时间线。空组合立即完成，零 dt 不推进时间。

一个节点只能属于一条组合树；重复引用或交叉共享在组合构造时拒绝，失败不改变
子节点归属。只 update 根节点，避免同一帧重复推进子项。数组在构造时复制。
并行分支按声明顺序写入；通常分配不同字段，若写同一字段，靠后分支覆盖前者。
节点内部字段不属于可修改契约；目标表在执行期间须保持普通数值数据。

顺序组合延迟取起点，所以预先创建“0 → 10 → 20”不会在第二段跳回 0。
阶段边界会以零剩余时间开始下一节点并捕获起点。暂停由调用方停止 update；
推进不放在 draw 中，不运行隐式完成回调或使用墙上时钟。

## 示例与验证

`examples/tween` 自带固定版本标准模块、LuaLS 原生注解和离线回放。两个方块
以不同用时并行前进，等待后同时返回；Space 暂停，C 取消，R 重建。运行：

```powershell
.\build\full\shiny.exe examples/tween
python tests/tween_integration.py build/full/shiny.exe
.\build\full\shiny.exe examples/tween --headless --frames 160 --replay examples/tween/smoke.jsonl
```

2026-09-27：定向用例通过嵌套组合、分步/整步时间一致、剩余时间、延迟起点、
取消保值、节点归属、非法/非有限参数及大数插值。已登记 CTest；本轮直接运行脚本，
未重新配置或构建原生。新项目 --check-all 和 SDK 哈希检查通过；根模块及新项目
为 SDK dev.37，未携带 tween 的既有项目不改版本。

`build/tween-reviewed/parallel.png` 为 60 帧隐藏、静音原生截图，已查看：双轨位置、
说明、状态和操作提示无重叠。相同帧原生/无窗口 watch 一致，160 帧确认全部结束并
回到起点。截图只证明该时刻布局；没有据此宣称动画主观流畅度或平台验收完成。
本轮没有改字体、运行压力/全套或 native sanitizer。
