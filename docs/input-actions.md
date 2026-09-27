# UI 与命名动作消费

`shiny.input` 每次 `Input.update(actions)` 解析当前玩法输入并清除上一轮玩法消费标记，
同时接收独立 UI 更新传入的消费结果。
`Input.dispatch` 按给定顺序调用已启用上下文；高优先级上下文应放在前面。
被消费的动作在后续 `down/pressed/released/axis` 查询中均不可见。

## 连续摇杆与扳机

动作来源可写为 `{axis='left_x',direction=-1,deadzone=.2,slot=1}`。
axis 支持 left_x/left_y/right_x/right_y/left_trigger/right_trigger；direction
为 -1 或 1，默认 1，选择轴的负/正方向。deadzone 默认 .2，范围 [0,1)，与原生
输入相同，逐轴将死区外幅度重新映射到 0–1；slot 可省略使用选中的手柄，或指定
1–4。断开返回零。Input.new/Input.bind 在发布前验证完整来源：来源数组须连续，
每项恰有一个键盘、鼠标、按钮或轴控制，字段及对应设备名称必须有效；
方向和死区仅供轴使用，slot 仅供手柄。错误不会替换原绑定。

```lua
local actions=Input.new({
    left={{key='a'},{axis='left_x',direction=-1}},
    right={{key='d'},{axis='left_x'}},
    throttle={{axis='right_trigger',deadzone=0}},
},'player')
Input.update(actions)
local horizontal=Input.axis(actions,'left','right') -- -1..1
local throttle=Input.value(actions,'throttle')     -- 0..1
```

Input.value 返回同动作所有来源的最大强度；键盘/鼠标/按钮按住时为 1，否则为 0。
Input.axis 返回正动作强度减负动作强度；原有数字按钮组合仍保持 -1/0/1。
down 为强度大于零，pressed/released 表示越过死区边界，不会因死区内的零点噪声
触发动作；键盘/按钮的短按边沿语义不变。二维移动可只在向量长度大于 1 时归一化，
保留小幅偏转的低速移动。默认是逐轴死区，不是径向死区。

Input.save 原样保存方向/死区/槽位至独立设置；无须原生 API 或额外依赖。
共享设置现可通过 [CONTROLS 面板](rebinding.md) 编辑并原子保存这些来源。
使用 Input.bind 修改绑定，空来源表可禁用动作。创建与修改时复制来源表，之后
修改调用者的表不会改变生效绑定；直接修改 `actions.bindings` 不受此保证。
保存前再次校验复制，错误不会写入设置。
消费后的 value 与 axis 同样为零；已被 UI 消费的偏转保持屏蔽直到回中。
`Input.consume_sources(actions,{axes={left_x=true}})` 显式独占整条轴：包含该轴的
动作整体被消费，包括中立/释放帧和该动作的其他绑定来源。

## UI 消费

`UI.update(ui, dt, width, height, actions)` 的最后一个参数可选。提供后，UI 将
处理过的设备来源映射到动作绑定，无需将游戏动作命名为固定的“确认”或“跳跃”。
单个动作绑定多个来源时，只要一个活动来源被消费，本轮整个动作均被消费。

```lua
local Input = require('shiny.input')
local UI = require('shiny.ui')
local actions = Input.new { jump = {{key='space'}, {key='enter'}} }
local ui = UI.new {id='menu', children={{id='ok',kind='button',text='OK'}}}
return {
    init=function() UI.layout(ui,384,216) end,
    update=function(dt)
        Input.update(actions)
        UI.update(ui,dt,384,216,actions)
        if Input.pressed(actions,'jump') then
            -- 在这里执行玩法跳跃；Enter/Space 被按钮消费时不会到达这里。
        end
    end,
    draw=function() UI.draw(ui) end,
}
```

- 模态窗口消费全部已定义动作，包含在其回调中关闭窗口的那一轮。
- 普通控件消费导航和激活键（含按钮/复选框/页签/列表的 Space）；输入框焦点
  消费键盘来源；滑条消费左右、Home/End 和 PageUp/PageDown 调整。
- 确认回调即使清空焦点，本轮触发确认的按键仍被消费。
- 鼠标左键在控件上按下后，即使移出控件才释放，也消费释放事件。
- 手柄导航/激活按按钮名称映射到动作，包含绑定中显式指定的手柄槽位；
  当前 UI 没有按玩家分开的焦点树。
- 不相关的来源保持可用。消费不更改原始设备快照，也不修改录制文件。

自定义高优先级上下文可使用 `Input.consume(actions,name)`，或
`Input.consume_sources(actions,{all=true})`；后者还支持 `keyboard=true`、
`keys={enter=true}`、`mouse={left=true}`、`buttons={south=true}`，检查按住、按下和
释放。每轮只调用一次 `Input.update`，否则会清除已消费标记。

独立 `ui_update` 已在玩法前运行。两个回调使用同一个 actions，并分别解析：

```lua
ui_update=function(dt)
    Input.update(actions, 'ui')
    UI.update(ui, dt, 384, 216, actions)
end,
update=function(dt)
    Input.update(actions) -- 默认 'game'；接收 UI 消费，再执行玩法。
    if Input.pressed(actions, 'jump') then jump_requested=true end
end,
```

每个回调只解析一次；即使菜单关闭，也保留 ui 分支的 Input.update 调用，以清除
上一显示帧的捕获。两路按住历史独立，UI 先查询不会吞掉未消费动作的玩法边沿。
UI 消费记录累积至下一固定更新；最近 UI 捕获覆盖同一显示帧的连续补帧。
已消费且仍按住的逻辑动作会屏蔽到松开，包括释放边沿，避免恢复游戏时意外跳跃。
在同一次固定 update 中消费动作也遵循上述松开规则，不要求 UI 使用独立回调。
一个动作有多个来源时，这个屏蔽持续到所有来源松开；随后新按下恢复。
原始 sc.input 与录制文件不变；直接读取它们的玩法须自行决定 UI 优先级。

样例菜单 `Shell.update(shell,dt,actions)` 与 `Settings.update(panel,dt,actions)`
同样接受可选动作表。Shell 使用 `menu` 动作打开/关闭暂停；未传动作表时使用
Escape/Start。菜单及其关闭帧消费全部动作，切换帧不再激活菜单控件。若菜单与
玩法都在固定更新中运行，依次调用 `Input.update(actions,'ui')`、
`Shell.update(shell,dt,actions)`、`Input.update(actions)`，最后仅在 Shell 返回 true
时推进游戏规则。Crossing 展示此用法与独立持久化的键盘/手柄动作配置。

用 UI.set 隐藏根节点时，整棵树停止接收输入，焦点/捕获在 UI.update 中清除；
显示后重新选择焦点。只修改 Lua 节点字段而不标记布局变更不属于支持的更新方式。

无窗口设备回放测试覆盖键盘/手柄激活与释放、无关绑定保留、关闭模态窗口
不穿透、鼠标移出后释放及正常点击。真实设备与原生图形验收未执行。
