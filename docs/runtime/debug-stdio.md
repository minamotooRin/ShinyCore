# Agent stdio 调试协议

用 `-DSHINY_DEVTOOLS=ON` 构建，再以管道启动：
`shiny PROJECT --headless --debug-stdio --frames 1000`。默认轻量构建不包含通道
代码，并拒绝该参数。启用模块也不自动监听；必须显式传入 --debug-stdio。
与 --check/--check-all 互斥；同样可以用于图形宿主，但本轮未做窗口验收。

初始化后暂停在第一帧之前，stdout 先返回 ready 事件，列出协议版本 1 及实际
支持命令。每个请求是一行 UTF-8 JSON，例如：

```json
{"id":1,"command":"step","count":3}
{"id":2,"command":"state","path":["quest"],"offset":0,"limit":16}
{"id":3,"command":"quit"}
```

如需调试首次加载，追加 `--debug-load`（必须与 --debug-stdio 一起使用）。ready 后
自动在第一条 Lua 可执行语句发出 breakpoint，通常为 project.lua 或入口脚本；
收到该事件后设置断点，再 continue。覆盖项目配置、require、入口、init 及初次
draw。此时窗口尚未创建；语法错误或进入 Lua 之前的资源错误仍直接报告 stderr。
不加该参数仍在初始化后暂停。ready 的 loading_breakpoints=true 表示支持加载断点，
break_on_entry 表示本次是否启用了首次加载暂停。

请求 ID 为 0..4503599627370495 的整数或不超过 64 字节的字符串；响应原样
携带 id、ok、frame，成功响应另有 room、tick、paused、pending_steps 和 result。
无法解析 ID 的错误使用 null，不伪造来源。stdout 只写协议行，日志和运行错误
写 stderr；结束输出 terminated 事件，--snapshot 仍可单独输出完整诊断文件。

| 命令 | 语义 |
| --- | --- |
| status | 当前帧、房间和暂停状态 |
| pause | 在固定更新边界或下一次指令钩子暂停，取消剩余单步 |
| continue | 继续固定更新 |
| step | 在暂停且没有未完成单步时执行 count 帧（默认 1，1..1000） |
| entities | 分页读取活动实体的句柄、对象持久 ID、标签、位置和速度，按槽顺序 |
| state | 读取显式 sc.state 数据，可传 path |
| watches | 读取 sc.debug.watch 暴露的数据，可传 path |
| breakpoints | 用项目相对 file 和 lines 数组替换该文件断点；空数组清除 |
| step_in / step_over / step_out | 在 Lua 暂停处步入、步过或步出 |
| stack | Lua 暂停处分页读取调用栈，level 从 0 开始 |
| locals | 读取指定 level（默认 0）的局部变量；传 variable 可分页展开局部表 |
| ui | 不传 tree 时列出已注册界面；传 tree 名时分页读取实际控件树 |
| resources | 分页读取声明资源名称、类型、路径、尺寸和 CPU 字体缓存信息 |
| metrics | 实体/弹体/粒子/绘制容量、Lua 字节数、字体字节数与缓存字形数 |
| panel | 图形宿主的原生检查面板：选择 section、offset 和 UI tree；不传字段读取配置 |
| quit | 请求宿主正常关闭 |

step 先确认请求，到达目标帧后另发 stopped 事件；不会更改游戏自己的模拟暂停
标志。暂停时仍可处理命令，网络与图形设备轮询仍运行。这里的帧步进指完整的
固定更新；停在 Lua 回调内部时须使用源码单步或 continue，不能执行帧单步。
房间切换后检查自动指向当前活动房间。
切换及 F5 重载的候选房间沿用已设置的断点。响应和 breakpoint 包含 candidate、
room、phase；phase 为 0 加载/初始化、1 固定更新、2 绘制、3 存档迁移、4 UI 更新，
无 Lua 回调时 0 也可表示空闲，不能单凭 phase 判断是否停在源码。首次项目入口
解析前 room 为内部入口占位名，具体源码以 file/line 为准。
候选暂停期间检查读取候选世界、VM 和暂存状态；原生画面仍显示旧活动房间，
不执行候选绘制、不提交状态或应用服务。继续后只有全部准备成功才提交；失败
保持原来的图形恢复行为。任一加载断点 quit 正常退出并释放候选；加载暂停不
增加模拟帧数。断点会取消未完成的帧单步，继续使用源码单步或 continue。
图形运行中候选房间加载失败时，已在旧房间完成的模拟帧仍计入单步数量，并输出
trace 与 stopped；失败不回退帧号，也不额外执行一帧补偿。

源码断点示例：先发送 `{"id":4,"command":"breakpoints","file":"main.lua","lines":[9]}`，
再发送 continue。命中时输出 breakpoint 事件，包含 frame、paused，以及可靠时的
file/line。源码单步和回调内暂停使用同一事件。断点每文件最多 128 行、最多 64
个文件；响应 verified=false 表示仅登记行号，不保证它是可执行行。

stack/locals 的 limit 默认 32、最大 128，栈最多检查 1024 层。整数以
`{"type":"integer","value":"9223372036854775807"}` 返回，避免 JSON 数字丢精度；
变量列表中表、函数及 userdata 只返回类型，字符串预览最多 512 字节。不执行表达式或元方法，
不提供修改变量接口。检查仅在当前暂停内有效，继续后没有可复用的 Lua 对象引用。

展开局部表时，`variable` 使用 locals 列表返回的零基 index；路径使用实际 Lua 键，
因此普通 Lua 数组通常从 **1** 开始。例：

```json
{"id":6,"command":"locals","level":0,"variable":2,"path":["inventory",1],"offset":0,"limit":32}
```

路径最多 16 层，支持字符串、布尔和有限数字键。普通数字绝对值不得超过
4503599627370495；更大的整数使用上面的 type/value 对象，支持完整 Lua 有符号
64 位范围。表结果含 type、items（key/value）、offset、next_offset；next_offset
为 null 表示结束。使用 Lua 原始遍历顺序，顺序只在同一次暂停内有效，不预先遍历
整表计算 total。每页最多 128 项，offset 最大 65535，每请求路径查找及分页累计
最多扫描 65536 项，超限明确失败。标量路径结果返回 value。ready.inspection 提供
local_tables、path_limit 和 entry_budget，供 Agent 发现能力。

循环表只显示类型摘要，需要显式路径继续读取；函数、userdata 和表类型的键不支持
路径定位。长字符串键可预览，但访问时须传完整原始键。所有读取不调用 __index、
__pairs、__len 或 __tostring；错误请求不改变暂停中的调用栈。

钩子内等待保留真实调用栈，恢复不重跑回调，也不重置指令预算；等待期间只轮询
命令、网络和原生设备，不重入 Lua。步过/步出依据 Lua 当前栈深度，尾调用遵循
Lua 的栈折叠语义；顶层步出停在下一次 Lua 回调。图形暂停呈现已有绘制队列。

state/watches 分页 offset 默认 0、limit 默认 64（1..128）。path 最多 16 层，使用字符串键
或零基数组索引。容器值只返回 type/size，继续读取需显式 path；长字符串和键
提供 512 字节以内、完整 UTF-8 前缀及原始字节数。检查不执行 Lua 表达式、对象
方法或元方法。实体 ID 只供当前运行期使用。

请求行最多 16 KiB，每次主循环读取最多 4 KiB；超长行报错并丢弃至换行，后续
请求仍可处理。EOF 发 disconnected 并保持暂停，绝不隐式继续。无窗口进程由
启动它的控制器关闭；图形模式可关闭窗口，或启用 --debug-keys 后按 P 本地恢复。
Windows 使用重定向的管道/文件输入；不创建额外控制台窗口或读线程。

目前 devtools=true 仅表示此协议已可用；ready.commands 是当前命令能力清单。
源码调试与指令预算共用同一个钩子入口。
Linux/macOS 输入适配已编写，尚未在对应平台验证。

`shiny.ui.new` 在 devtools 构建自动按根控件 ID 注册。也可用
`sc.debug.ui(name, ui)` 注册别名，`sc.debug.ui(name)` 注销；同名注册替换旧绑定。
每房间最多 64 个名字，名字 1..128 字节；弱引用不阻止树被垃圾回收，切房间随 VM
释放。未启用模块时没有该原生函数，标准 UI 模块直接跳过注册。

例如 `{"id":5,"command":"ui","tree":"inventory","offset":0,"limit":16}`。
返回作者声明顺序的节点及 total/offset，包含 ID、父 ID、焦点、捕获、隐藏/禁用、
模态、布局待更新、矩形、标量值、页签、列表条数、滚动和文字选择位置。隐藏或脏布局
不返回旧矩形；读取不触发重新布局、Lua 回调或元方法。标量字符串预览最多 256 字节；
value 截断附 value_bytes/value_truncated。单树检查上限 65536 节点，分页上限 128。
无窗口与原生面板复用这些检查数据；资源列表表示声明和 CPU 数据，不冒充 GPU 驻留信息。
列表的当前项为带 id 的记录时，节点还返回 selected_item_id。检查使用原始字段与
数组读取，不调用 items 或行记录的元方法；值的 UTF-8 预览上限仍为 256 字节。
scroll_max 为纵向上限，scroll_capture 为滑块捕获状态。tooltip_visible 为当前
提示框可见性，显示时另有 tooltip_rect、tooltip_truncated；隐藏或脏布局不返回
旧提示矩形。这些字段与 Lua UI.inspect 使用相同语义，读取不推进提示计时。

图形 devtools 构建加 `--debug-keys` 后，F4 依次切换指标、实体、UI、资源和关闭面板，
F6/F7 翻页，F8 切换 UI 树。面板默认为关闭，独立使用无需 stdio 控制器；Lua 断点
等待期间也能查看。显示间隔含帧率等待，不是 CPU/GPU 处理耗时；阶段分析继续使用
`--profile`。面板按窗口像素绘制，复用项目声明字体的字形缓存和后备顺序；中文
名称需要项目提供对应字体。未声明字体时使用默认位图字体，不额外分发系统字体。
UI 行显示焦点、隐藏/禁用、滚动量、滑块捕获、提示和选中行 ID。

Agent 无需发送实体按键，可用 `{"id":8,"command":"panel","section":"ui","tree":"背包","offset":12}`。
section 为 off/metrics/entities/ui/resources；切换 section 重置 offset 与指定树，
offset 为 0..65532，面板每页最多 12 行，较小窗口可能裁剪剩余行。tree 为 1..128
字节的注册名称，仅用于 ui；不存在的树在面板显示诊断。参数整批验证后生效；
section=off 关闭。不传字段返回当前 section/offset/tree，空 tree 表示按 F8 索引选择。
此命令需要已创建窗口的 --debug-stdio 会话，无需 --debug-keys；无窗口或首次
加载断点尚未创建窗口时明确拒绝。即使检查正停在候选 VM，面板仍显示旧活动房间。

2026-09-27：原生 UI 检查补充 scroll/tooltip 字段，禁止元方法、脏布局和暂停栈检查
在 Release 与无窗口 ASan/UBSan 通过。panel 无窗口拒绝及帧协议在两种构建通过。
`tests/native_ui_inspector.py` 为显式图形检查：隐藏、静音、每场景两帧，协议选择面板
和分页，验证无效参数不改变配置。实际查看 `build/ui-inspector-reviewed-v2/` 的中文
提示及滑块底部捕获图；字体修正后的面板首页/末页在 `build/ui-panel-font-reviewed/`，
中文树名、滚动量、焦点和 tooltip 标记可读。真实 F4/F6/F7/F8 按键与 DPI 设备仍未验收。

2026-09-26：Windows Release 图形构建通过，但验证仅使用 --headless 管道。
帧协议进程级测试通过，覆盖请求 ID、三帧单步及 stopped 事件、继续/暂停、实体
分页、嵌套状态路径、watch、未知命令、坏 JSON、超长行后恢复、日志 stderr、
正常退出及 EOF 保持暂停。同项无窗口 ASan/UBSan 通过。复现命令：
`python tests/debug_stdio_integration.py build/full/shiny.exe`。未做窗口、跨平台
或性能验收。

2026-09-27：隐藏原生材质恢复检查实际使用 ready/step/stopped/terminated 协议，
验证 GPU/脚本/尺寸三种候选失败后的帧数及 trace 连续性，并验证后续成功切换。
详见[原生恢复证据](../verification/systems/advanced-render.md)。这不包含原生面板交互或
Lua 源码断点的图形验收。

同日新增源码调试定向测试 `tests/debug_lines_integration.py`：嵌套调用断点、原始
局部变量、步入/过/出、顶层步出、恢复后不重复执行以及无限循环预算限制通过。
源码调试和原有帧协议均通过无窗口 ASan/UBSan。清除所有断点且未单步时，行钩子
跳过源码路径解析；预算检查保留。未扩展至全套回归、压力长测或原生窗口验证。

2026-09-27 局部表补充：`python tests/debug_locals_integration.py build/full/shiny.exe`
验证嵌套 Lua 路径、布尔/小数/64 位整数/含 NUL 字符串键、UTF-8 截断、300 项分页、
循环表、禁止元方法、扫描预算及错误后恢复。此项和原有 debug_lines 在 Windows
Release、全模块无窗口 ASan/UBSan 均通过；未扩大到全套测试或原生面板交互。

同日加载补充：`python tests/debug_load_integration.py build/full/shiny.exe` 覆盖
project.lua、require、init、首次 draw、候选初始化与提交后更新、两种加载阶段 quit
以及恢复后的加载指令预算。Release 和无窗口 ASan/UBSan 通过；原有帧协议和源码
单步 Release 检查通过。隐藏原生 candidate-debug 检查命中首次加载和候选 init、读取暂存状态
和局部变量后继续并注入失败，验证旧状态、着色器版本、像素及连续 trace 保留；
实际查看 `build/debug-load-native-reviewed-v2/candidate-debug/final.png`。原生面板按键
交互、F5 实体按键及首次加载后的可见窗口交互仍未验收。

UI 接入补充验证：`tests/debug_ui_integration.py` 覆盖原生分页、父级隐藏/禁用、
焦点、元方法隔离、注销和垃圾回收，以及资源/容量字段。Release 通过；源码断点
停留期间读取、步出后刷新和释放的同项无窗口 ASan/UBSan 通过。原有帧协议在两种
构建均通过。`build-dev` 开发工具关闭构建通过，验证未注册 sc.debug.ui、普通 UI
仍能创建布局，且 --debug-stdio 被明确拒绝。原生面板仅完成编译验证。
