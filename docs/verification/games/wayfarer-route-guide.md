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

Windows 全功能构建上已通过 `test_stream`（慢读取、请求复用、预算回收和提示失败）、
`tests/stream_recovery.py`、`tests/nav_route.py`、
`tests/wayfarer_route_commit.py`（含已加载路段确认）、`tests/wayfarer_integration.py`、
`tests/test_sdk.py`、全功能/轻量 `tests/test_contracts.py`、
`tests/test_package_content.py` 和项目 `--check-all`。
后者的完整任务回放覆盖 16 块状态、结局恢复、道路提交及新旅程隔离。
LLVM-MinGW ASan/UBSan 下的 `test_stream` 与 300 帧无窗口 Wayfarer 预取回放通过。
打包后从仓库外工作目录执行 300 帧 `travel.jsonl`，粗路线与局部路线状态均为 `ok`，
图修订号为 2。隐藏、静音原生截图
`build/wayfarer-prefetch-final-reviewed/wayfarer-route.png` 已目视检查：远处
药师目标的入口标记落在道路附近，场景和中文 UI 未出现遮挡或异常。

该指引会向路线中第一个未发布入口块或待核实修改块发送 `sc.stream.prefetch`
缓存提示。提示无引用与提交帧，慢读取不单独阻塞固定更新；正式区域请求可复用
读取，但仍由确定帧控制发布。提示失败不直接暴露；正式请求在计划帧报告错误，
可按请求序号重试。
第 300 帧远处药师回放选中 `(0,0)` 预取，发行包从仓库外运行同一段回放也通过。
预取不读取该块的存档覆盖，也不控制角色沿局部路径移动。
实际通行仍由已加载区域的 `World.path` 和加载边界决定；硬件交互及
最终轻量发行包未在本次验证。

待核实块现在不会抹掉此前已确认的入口链。`Route.route` 返回到未知块近侧
边界格为止的 `points`；若静态图根本没有到未知目标块的入口，仍返回空点列和
`pending`，不会猜测路径。`tests/nav_route.py` 在真实 Lua VM 中检查未知中继、
未知终点、多入口前段及无入口四种情况。Wayfarer 在 `unverified` 时只对
`World.path` 确认可达的第一段绘制箭头，底部仍标示“前方路径待载”。
后台缓存提示先选择已确认前段首个未发布块，前段全部发布后再提示未知块；
定向测试检查这个次序。
`tests/wayfarer_route_commit.py`、完整任务回放、SDK 校验和 `--check-all` 通过。
隐藏、静音的实际原生渲染截图
`build/wayfarer-frontier-reviewed/wayfarer-route.png` 已目视检查，正常路线的
药师指引和中文 UI 无异常。另用 `build/wayfarer-frontier-fixture` 临时项目在
路线计算前将远处 `(0,0)` 标为待核实，真实渲染截图
`build/wayfarer-frontier-fixture-reviewed-v2/wayfarer-route.png` 已目视检查：箭头仍
指向已确认的下一入口，待载文案没有遮挡游戏元素；第 300 帧 watch 为
`route_status=unverified`、`route_local_status=ok`、`prefetch=(0,0)`。该夹具只用于
视觉检查，不是存档读取时序验收；原版项目和发行资源未改动。
