# 精灵序列动画

`require("shiny.animation")` 提供普通 Lua 固定更新动画，不持有实体或执行回调。
图集字段仍由游戏通过 sc.set 写入，关闭视觉动画不会推进玩法随机数。

```lua
local Animation = require("shiny.animation")
local anim = Animation.new {
    idle = {loop=true, {frame=0, duration=.5}},
    walk = {loop=true, {frame=1, duration=.125, event="step"},
                      {frame=2, duration=.125}, {frame=3, duration=.125}},
    flash = {{frame=3, duration=.08, event="flash"}, {frame=0, duration=.08}},
}
Animation.play(anim, "walk")
-- update(dt)：切换或继续选中的片段。
local frame, done, events = Animation.update(anim, dt)
sc.set(player, {frame=frame})
for _,event in ipairs(events) do
    -- 显式处理音效或其他标记；模块自身不调用游戏代码。
end
-- draw 中只读，既不推进时间也不消费事件。
local current, finished = Animation.frame(anim)
```

- new 验证并复制命名片段，外部表修改不影响动画。片段是非空连续帧数组，额外
  仅允许 loop 布尔值（默认 false）；帧仅含 frame、duration、event。禁止元表、
  空洞及未知字段。frame 是 0..65535 整数，duration 是有限正秒数；event 可省略，
  否则为 1..128 字节字符串。实际图集尺寸仍由原生资源校验。
- play(anim,name,restart?) 选择片段；默认重复选择不重启，返回 false。切换或
  restart=true 从第一帧重来并返回 true。restart 仅接受布尔值；已完成片段
  同名播放也须显式 restart=true。
- 首帧事件排到下一次成功 update；同一次更新跨越的后续帧/循环首帧事件按顺序
  返回，不合并或去重。播放后再次切片会丢弃旧片尚未消费的入帧事件。update(0)
  可以消费首帧事件，frame 只读不会消费。
- anim.speed 默认 1，设为 0 停止时间推进（仍可消费首帧事件）；必须有限且非负。
  dt 同样有限且非负。非循环片段在最后一帧持续时间走完后 done=true，保持该帧，
  之后 update 不重复返回事件。末尾多余时间丢弃；顺序组合使用游戏逻辑或补间模块。
- 每次 update 最多推进 4096 次边界，另可有一个待消费的首帧事件。时间溢出或
  超出预算会报错，并保留原 index/elapsed/done/pending；不截断事件或部分提交。
  内部 clips/index/elapsed/pending 由模块维护，游戏仅修改 speed 或调用公开函数。

UI 暂停时由游戏决定是否调用 update，draw 不推进动画。纯表现事件不应替代
弹体命中、任务状态等权威玩法数据；动画关闭或降低视觉更新频率时应保留玩法逻辑。

## Barrage 接入与定向验证

Barrage 的 presentation.lua 定义 idle/walk/dash，冲刺是 .2 秒非循环序列。
每个敌人持有独立片段时间，五类敌人分别为 6/10/4/5/4 帧每秒；暂停菜单或升级
界面不会推进它们。沿用原创 keeper/wisp 图集；keeper 的 0..3 为右向帧，4..7
为反向帧，游戏统一使用右向帧并由 flip_x 控制方向，避免双重翻转。

`tests/animation_integration.py` 在原生 Lua VM 中检查数据隔离、首帧/跨帧/循环
事件、速度为零、完成与重启、严格参数及预算失败原子性。模块分发为 SDK dev.42，
四个携带项目的哈希审计通过，Barrage 打包依赖包含 animation.lua。

600 帧普通挑战回放的实体玩法字段、显式状态、观察值、RNG、tick、弹体、命中、
音频与输入和改动前一致，涵盖 3 次冲刺和 14 次命中。画面状态 hash 包含精灵帧，
不据此断言玩法相同。未重跑完整六波或性能/长时间验收。

`build/barrage-animation-reviewed/dash.png` 是 186 帧实际玩法，`poses.png` 是
原生绘制的明确姿态预览；均隐藏、静音且已目视。预览发现并修正了反向图集帧
误用，最终朝向、区域、HUD 和标签无异常。截图验证姿态与布局，不代替连续动画
观感或最终美术验收。仅改 Lua/数据，本轮不重建原生或重复 sanitizer 检查。
