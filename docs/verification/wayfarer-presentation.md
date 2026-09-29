# Wayfarer 动画与采集反馈

旅人和信使采用项目自带的 shiny.animation，各自持有 idle/walk 时钟。横向输入改变
朝向，纵向移动和停步保留朝向。所有表现时间仅在正常玩法更新推进，菜单、对话、
背包及加载等待时冻结；重建房间重置，不进入存档。仍复用原有 keeper 图集。

药草角标与采集共用 Quest.in_reach，保留既有严格矩形距离判定及两种靴子的范围。
采集保留原粒子/声音，增加累计数量提示与最多八条 0.8 秒飘字；只存坐标和时间，
不保留会因块卸载失效的实体句柄。draw 读取当前实体，模态界面隐藏采集提示。

必要验证：

- --check-all 通过；tests/wayfarer_presentation.py 在原生 Lua VM 验证独立时钟、
  横向朝向保持、反馈容量/过期和装备范围边界。
- 修改前后完整 3243 帧键盘回放均采齐 24 株、清路并交付。比较 scene/tick/rng、
  实体物理快照、contacts/audio/input/state/settings、弹体计数及 quest/courier
  观察字段一致；不比较含视觉字段的诊断 hash，不声称所有逐帧轨迹已比较。
- walkthrough.jsonl 的 185/192 帧隐藏、静音原生截图已实际查看：采集前角标围住
  当前药草，采集后飘字和 HUD 下方的数量提示可见，未遮挡 HUD。信使可见。
  静态图不代替连续动画、真实设备或完整美术验收。
- 发行依赖闭包含 presentation.lua、quest.lua 与本地 shiny.animation；SDK 审计
  通过，保持 dev.46。没有原生或标准模块改动，无需重建或重复 sanitizer。

证据：build/wayfarer-presentation-reviewed/ 的 before/after.json、comparison.json、
manifest.json 和 native/ 两张图；工具已加入对应 gather-ready/gathered 捕获项。
未运行性能/耐久测试、真实设备或完整控制器回放。地景与前景美术、人工 5–10 分钟
体验及最终发行验收仍待完成。
