# 流式世界导航坐标 · 2026-09-28

`World.path` 和 `World.flow` 接受世界像素位置，统一使用当前已发布导航窗口的
原点与单元大小。路径点返回单元中心的世界像素坐标；流场句柄继续交给
`sc.navigation.direction/refresh/steer` 使用。候选块尚在读取或准备时，查询只看
活动地图；端点不在已加载覆盖内返回 `unloaded`，关闭导航返回 `disabled`。
地图提交后才替换坐标窗口，地图修改沿用原生导航的脏区域版本。

Wayfarer 巡逻 NPC 改用 `World.flow`，不再读取世界内部块表和硬编码块尺寸。
定向流式事务测试覆盖负块坐标、世界像素路径点、未加载目标、切换后的新窗口
和清空世界；完整 Wayfarer 键盘/手柄任务集成回放通过。隐藏静音原生世界画面
位于 `build/wayfarer-navigation-reviewed-20260928/wayfarer-world.png`，已目视检查。
Wayfarer 本地 SDK 固定为 dev.74；精简发行包及 ZIP 位于
`build/wayfarer-navigation-release-20260928/`。包复制到仓库外并限制为 Windows
系统 PATH 后，3245 帧场景的五项断言通过。

此接口只覆盖当前已加载区域。远距离稀疏世界的全局路线仍需与实际碰撞几何
一致的离线连通摘要，不能把本接口当作已完成的长程寻路。
