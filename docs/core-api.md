# 核心调用契约

当前完整构建的 181 个注册函数均有结构化 contract；轻量构建按模块裁剪后也没有
缺失条目。原生表生成 [LuaLS 注解](api.lua) 和 [参考](api-reference.md)，这不代表
所有开放字典、资源格式和复杂组合约束都有封闭字段模式，也不是完整版验收结论。

本轮为核心接口补上参数、默认值、返回值与阶段，描述位于
`src/script/core_contract.cpp`，注册继续引用同一函数实现。

- rect/circle/text/clip 只允许 draw；坐标范围 ±1000000，screen 默认 false。
  显式 nil 不能代替 screen；text options 位于第七参数，先提供 screen 布尔值。
- clip 只接受零个或四个参数；零参数弹出。每次 push/pop 消耗绘制命令，最多
  32 层，draw 结束后验证平衡。
- measure 返回独立 width、height，接受最多 4096 字节有效 UTF-8；text 最多
  511 字节。measure 的 font/wrap 接受省略或 nil；text options 的 font/wrap/align
  默认空字体名/0/0。无窗口和图形使用同一度量；本轮只验证调用，不重新验收字体。
- map 的第四参数和 tile 的第三参数存在即为写入，nil 不是读取。写入仅允许
  load/init/update。map 只访问已载入的有限 Tiled 图层；tile 是 ASCII 碰撞格。
  tile 越界读取在有边界房间返回 #，在无边界世界返回 .；越界写入报错。
- objects() 返回原生有限地图对象的独立副本；保留输入字段并合入层偏移 x/y，
  无地图时为空数组。它不自动返回流式世界当前对象集合，也不接受参数。
- random 只接受零个或两个参数，推进玩法随机数。两个 Lua 整数使用闭区间整数
  采样，否则按浮点数插值；min 必须不大于 max。粒子使用独立视觉随机数。
- emit 的 speed/life 默认 30/0.5；tone 的 duration/volume 默认 0.1/0.2，
  这些可选参数不接受显式 nil。life/duration 的最小值 0.001 已修正为可实际传入，
  避免 float 常量提升到 double 后意外拒绝该边界值。容量不足仍显式失败。
- scene 的可选状态为普通对象，省略则继承；显式 nil 报错。活动异步存档请求须
  先释放，图像变更须提交或取消。失败候选不能替换活动房间。
- tick/time 表示房间固定步数及 tick/60；应用模拟暂停时仍递增。设备快捷读取使用
  当前回调的输入视图；自定义动作与多控制器优先使用 shiny.input 和 sc.input。

必要验证：完整 Release、轻量 Release 和全功能无窗口 ASan/UBSan 的
core_api_contracts 检查通过，覆盖条目完整性、阶段、可选参数、最小寿命和绘制命令。
Release 另运行既有 Tiled 读取/编辑及粒子容量原子性两项检查。生成一致性用例、
完整/轻量联合文档检查与 16 项目 SDK dev.46 审计通过。
证据在 `build/core-api-contracts-reviewed/`。没有修改渲染效果、重做旧截图或运行压力测试。
