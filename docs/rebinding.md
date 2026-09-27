# 操作重绑定

Crossing 与 Barrage 的 **SETTINGS → CONTROLS** 可编辑当前项目的命名动作。
左侧选择动作，右侧显示来源；键盘 Tab/方向键、鼠标或手柄方向键/肩键与南侧确认
均可导航。作者提供的动作名也是列表项稳定 ID。

- **KEY/MOUSE** 替换该动作的全部键鼠来源，保留手柄按钮及轴。
- **PAD** 替换手柄按钮来源；**AXIS** 替换轴来源，记录方向、稳定槽位及 .2 死区。
- 录入先等待相关输入松开。轴在幅度 ≤.25 时准备，之后偏转 ≥.65 才录入；多轴
  同时达到阈值按固定轴目录选择。按键/按钮/鼠标使用快照按下边沿，支持短按。
- 录入中 Escape 或任一手柄 Back 取消；非录入状态 Escape/东侧按钮返回。
  这两个录入取消键不作为新来源。保留旧默认绑定中的 Escape 不受影响。
- 新来源与其他动作重复时提示冲突并等待重试；同轴相反方向不冲突，未指定槽位
  的手柄来源视作覆盖所有槽位。已有作者定义的共享绑定不会被自动删除。
- **CLEAR ACTION** 清空当前动作，`menu` 动作保留至少一个来源；**DEFAULTS**
  恢复创建动作时的默认草稿。**BACK** 丢弃未应用修改。
- **APPLY** 先原子保存设置，成功后再替换活动绑定。失败显示错误并保留旧配置与
  编辑草稿。只替换当前 profile，保留显示/音量与其他 profile。菜单捕获及回中
  屏蔽继续生效；修改绑定不会把确认或持续按住输入泄露到游戏。

`Input.new(defaults,profile)` 保留独立的 `actions.defaults` 副本；
`Input.copy_bindings(bindings)` 复制普通来源表，`Input.label(source)` 生成显示名称。
重绑定界面为普通 Lua 模块 `shiny.rebind`，不引入原生 API、依赖或新存档格式：

```lua
local panel=Rebind.new(actions,on_back)
-- 在场景 update 中调用；配置应用遵循 sc.settings.apply 的调用阶段。
Rebind.update(panel,dt)
-- 在 draw 中调用：
Rebind.draw(panel)
```

共享 Shell 已将 `Shell.update(shell,dt,actions)` 的动作表交给 Settings。
独立使用时调用 `Settings.new(on_back,actions)`；只有带 profile 的动作表才显示
CONTROLS 按钮。项目按静态 require 收集依赖，不需要引擎源码路径。

当前面板不编辑组合键、多套设备预设或死区数值；这些数据仍可由 Lua 声明。
手柄录入固定到捕获时的 1–4 槽位，显示 `@槽位`。实体设备与真实热插拔仍待验收。
