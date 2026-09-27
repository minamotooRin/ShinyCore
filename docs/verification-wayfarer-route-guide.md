# Wayfarer 跨块路标验证

Wayfarer 将原生导航掩码烘焙为 16 块、24 入口的项目内 Lua 图。药师和道路
目标使用 `shiny.stream_route` 的下一跨块入口；小地图标记入口或尚未核实的
存档修改块。绘制箭头前，`World.path` 还会确认当前已加载路段可达；
未加载、受阻或预算耗尽时显示状态，不把粗路线当作通行许可。
`World.refresh_route` 只刷新图范围内已经发布的块，因此窗口
余量加载到负坐标时不会把范围外块交给图。存档数据版本为 3，旧槽明确拒绝。

原先每对连通分量取边界第一个可通行格，开放边界常把玩家引向地图上沿。
离线编图与运行时重建现都取候选格的中位值；入口仍保证可通行，路径仍是
粗粒度连通路线，不保证最短距离。定向测试检查 Wayfarer 横向入口位于
第 16 行、动态重建取相同中位规则，并覆盖图范围外余量块。

Windows 全功能构建上已通过 `tests/nav_route.py`、
`tests/wayfarer_route_commit.py`（含已加载路段确认）、`tests/wayfarer_integration.py`、
`tests/test_sdk.py`、`tests/test_package_content.py` 和项目 `--check-all`。
后者的完整任务回放覆盖 16 块状态、结局恢复、道路提交及新旅程隔离。
打包后从仓库外工作目录执行 300 帧 `travel.jsonl`，粗路线与局部路线状态均为 `ok`，
图修订号为 2。隐藏、静音原生截图
`build/wayfarer-route-local-reviewed/wayfarer-route.png` 已目视检查：远处
药师目标的入口标记落在道路附近，场景和中文 UI 未出现遮挡或异常。

该指引不自动预取远处待核实块，也不控制角色沿局部路径移动。
现有 `sc.stream.request` 绑定提交帧，直接用于机会式预取会在慢盘时阻塞
固定更新，并可能与后续同块区域请求的帧号冲突；预取需要独立的非阻塞缓存提示。
实际通行仍由已加载区域的 `World.path` 和加载边界决定；硬件交互及
最终轻量发行包未在本次验证。
