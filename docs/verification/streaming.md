# 流式提交验证 · 2026-09-22

## 对象分块补充 · 2026-09-25

格式 3 将对象移入归属块。Windows GNU Release 本轮仅运行无窗口的
`stream`、`content`、`stream_integration`、`contracts`、`asset_tools`、
`complete_integration`，6/6 通过；LLVM-MinGW ASan/UBSan 对前三项复验，
3/3 通过。资源工具覆盖负坐标、组/层偏移、跨块对象单一归属、对象独占块、
重复 ID、非法坐标及可重复构建。原生测试覆盖对象块拒绝与旧可见数据保留；
集成测试实际构建资源，再由无窗口宿主读取对象。`diff --check` 通过。

本轮未启动图形窗口或性能测试；以下原生画面记录属于早期实现，不能作为
格式 3 对象绘制或完整流式世界的验收。对象自动实例化及持久状态恢复尚未完成。

本轮替换阻塞式块读取，建立计划帧发布和宿主模拟边界。契约见
[streaming.md](../streaming.md)。目前发布的是块数据，尚非完整地图运行时。

## 实际验证

Windows GNU 16.1 Release：完整和无窗口配置 CTest 各 **24/24**，轻量配置
**19/19**。LLVM-MinGW 22.1.8，无窗口 ASan/UBSan、网络/流式开启、遇错退出：
**24/24**。最终补齐契约元数据和参数校验后，流式开启的三个配置各重跑相关
测试 **5/5**；轻量内容/存档检查通过，禁用模块参考文档校验修正后通过。
原生图形集成测试最终 **5/5**，生成注解和参考文档检查通过。

原生受控读取测试在工作线程中阻止读取完成，并在主线程重复执行 100 次
`advance/get`；它们立即报告未就绪，不等待读取。后请求已缓存的数据不能
越过前序慢请求；同一到期批次全部准备好后一起可见。另覆盖计划帧排序、
负坐标稀疏空块、引用平衡、取消收尾、预算预留、反复淘汰/重访、读取失败后
保留原可见集合，以及取消失败请求后继续提交。

进程集成测试覆盖阶段与参数类型、路径 NUL 拒绝、原生元数据、准确的回放/
录制帧计数、逐帧 trace 一致性。文件缺失在 LLVM-MinGW 下暴露了系统错误
字符串不是 UTF-8 的问题，现改用稳定诊断、系统错误码与 UTF-8 路径。

## 实际等待与原生画面

额外运行 15 个块、每块 64 层、每层 1024 个 GID（共 983,040 个值）的读取
批次。在 update 的第 2 帧请求，第 3 帧统一发布，运行至 8 个模拟步：

| 模式 | 宿主轮次/显示帧 | 没有模拟步的等待轮次/帧 | 模拟步 |
| --- | ---: | ---: | ---: |
| 无窗口 | 25 | 17 | 8 |
| 原生图形 | 10 | 2 | 8 |

使用相同固定回放，实体、显式状态、watch、世界诊断 hash 和状态 hash 相同。
实际原生 PNG 已检查，显示预期的移动方块；此画面验证宿主仍能正常完成渲染，
并不代表地图块已经自动绘制。完整本机输出位于
`build/verification-streaming-2026-09-22/`：`heavy-comparison.json`、两份
profile/trace 和原生截图。等待轮次取决于本次磁盘/调度速度，不作为性能门槛。

```sh
ctest --test-dir build/full --output-on-failure
python tests/stream_integration.py build/full/shiny.exe --native
python tools/api_docs.py build/full/shiny.exe build-dev/shiny.exe --reference docs/api-reference.md --check
```

## 未验收部分

未在本轮执行 Linux/macOS、真实手柄/IME/音频听感或长时间硬件测试。
后续实现的整块提交/边界/状态恢复见[流式世界](../stream-world.md)，独立 UI 与
命名动作消费见[UI 生命周期](../ui-lifecycle.md)。GPU 驻留、异步存档仍待完成；
应用级读取线程与房间销毁的补充证据见下。
本次数据读取测试不能替代规定的 1024 个地图块、33.3 ms 应用处理帧验收。

## 读取失败恢复（2026-09-27）

`failure/retry` 提供 UI 阶段的非阻塞重试，失败记录只在计划边界发布。
受控读取器检查保留序号/提交帧/引用/缓存预留、拒绝重复/错误重试，以及延迟
重读时旧块保持可见、同批块成功后一起发布。Release 与无窗口 ASan/UBSan 通过。

真实进程从缺失块开始：固定模拟停在第 1 帧，测试收到错误后原子放回块文件，
UI 重试后继续完成 3 帧；检查字段、阶段、非法参数、旧实体及批次顺序。
无窗口未处理错误仍及时失败；上述 Release/ASan 检查通过。没有运行全量套件。
同一故障流程还完成隐藏、静音、有界原生验证，实际查看
`build/stream-recovery-reviewed/recovered.png`：3 次固定更新、3 个活动块，恢复后
没有残留错误覆盖层。记录含实际引擎哈希，不代表性能或真实输入设备验收。

```powershell
./build/full/test_stream.exe --retry
python tests/stream_recovery.py build/full/shiny.exe
python tests/stream_recovery.py build/full/shiny.exe --native --output build/stream-recovery-check
```

## 应用级读取生命周期（2026-09-27）

宿主现在持有一个按需启动的 `ScChunkLoader`，活动/候选房间仅拥有缓存。
Windows GNU Release 与全功能无窗口 LLVM ASan/UBSan 均通过以下定向检查：

- `test_stream`：受控读取未完成时销毁房间及候选，析构先于释放读取闸门结束；
  排队候选不执行读取，旧任务完成后丢弃，新房间同坐标返回自己的 GID；
  两次实际读取来自同一个后台线程。原有顺序、缓存、对象校验及重试检查也通过。
- 宿主 `test_room_loader_isolation`：旧房间带未来请求切换后，新房间数据、
  引用和请求序号独立。`test_scheduled_publication_and_recorded_frames` 仍通过。
- `stream_recovery.py`：真实缺失文件修复后按原帧恢复，未处理错误仍退出。

默认轻量构建也完成重新编译；`--api` 确认流式模块关闭，单帧无窗口运行确认
`sc.stream.available=false`、未注册 open 方法且宿主正常退出。初次冒烟检查
误将禁用命名空间预期为 nil，修正为现有 available 契约后通过；未修改引擎行为。
未运行整套测试或压力负载。

这验证所有权、取消和短流程功能，不是磁盘延迟或帧时间性能验收。
没有启动图形窗口。索引仍同步打开，应用退出仍等待已开始的读取。
原生独立流拥有私有加载器时仍在析构中等待，见[生命周期契约](../streaming.md)。

## 异步存档后的世界发布 · 2026-09-27

stream_world 与 Wayfarer 已改为先准备独立快照、后台保存、再发布世界。
以下 `tests/stream_integration.py` 定向用例在 Release 与全功能无窗口
ASan/UBSan 通过；本次宿主提示修正后又用 Release 检查这五项，通过：

- test_stream_world_coordinator：加载、等待、切换、重返和空区域。
- test_stream_world_map_edits：负坐标图块修改/删除、碰撞与导航、持久恢复。
- test_streamed_object_transition：同步工具仍保持原子发布与失败保留。
- test_world_async_cancel_and_publication_failure：取消不撤销成功磁盘提交，
  保存成功但实体容量不足时旧世界保持，释放容量后重试发布，恢复原暂停值。
- test_world_failed_save_recovery：真实临时路径故障、修复后重试原快照、
  取消等待中的写入后发生失败仍能释放等待并保留旧存档。

Wayfarer 的原有完整任务回放/续玩检查已通过；本次提示修正仅重新运行
`--check-all` 及 45 帧保存界面回放，没有重复完整任务。七个项目 SDK 审计通过，
Wayfarer 为 dev.20，其他带注解项目仍为 dev.19。

`tests/native_wayfarer_save.py` 用真实目录故障触发保存失败；仅加失败后退出钩子，
布局和绘制来自实际样例。首次查看 `build/wayfarer-async-save-review/save-error.png`
发现宿主误称热重载失败、中央面板遮挡恢复按钮，该图不通过。宿主现将 IO 状态
单独绘为底部短条，详细错误留在日志/API。最终实际查看
`build/wayfarer-save-final-review/save-error.png` 和 `save-menu.png`：中文说明、
重试/返回按钮及焦点边框清晰且无覆盖，正常保存菜单没有残留错误提示。
manifest 记录引擎/样例哈希和退出钩子；所有原生运行隐藏、静音、有界且隔离存档。
完整与轻量图形构建通过。本次未验收等待瞬间动画、实体设备或性能。

块状态读取及索引验证仍同步；GPU 驻留、跨进程崩溃恢复和大型负载尚未验收。
已通过保存/发布失败恢复不等于整个流式功能完成验收。

## 进入块异步恢复 · 2026-09-27

stream_world 与 Wayfarer 已使用 read/prepare/write/publish 四阶段事务。
原有四项协调/地图修改/取消发布/保存故障测试按新增读取阶段更新帧序，
新增两项检查，六项在 Release 与全功能无窗口 ASan/UBSan 均通过：

- `test_world_read_preparation_and_cancel` 禁用同步 read_chunk，验证首次读取期间
  不发布对象/地形、取消读取保留活动对象及边界、准备回调失败可重试、读取位置
  正确恢复、保存成功后联合发布。没有用减少断言绕过新增阶段。
- `test_world_failed_read_recovery` 注入真实损坏检查点，验证 UI 观察到 read 失败、
  修复文件后在原固定帧恢复、预先取消的读取失败能解除等待、旧对象和原暂停值保留，
  取消不改磁盘。最初夹具带无图集 GID，改为本测试所需的空块后通过，未放宽导入校验。

Wayfarer 完整流程检查通过。旧回放在新增读取暂停后修路时距离不够；仅延长该段
向右输入三帧并顺延后续事件，现为 3243 帧。采齐 24 株、16 个地图块状态、修路、
结局/续玩及新旅程隔离断言保留。新旅程检查增加一帧等待首次读取发布。
无窗口初次失败诊断保留在 `build/wayfarer-read-route/`，不是通过证据。

`native_wayfarer_save.py` 新增损坏读取源夹具，不替换游戏的绘制或恢复控件。
实际查看 `build/wayfarer-read-world-reviewed/` 的读取失败、写入失败、保存菜单三图：
中文标题区分阶段，说明/按钮/焦点框完整，底部宿主提示未遮挡控件，正常菜单无错误残留。
全部运行隐藏、静音、有界、隔离存档；manifest 记录真实引擎及样例哈希。
Wayfarer SDK 为 dev.22；本轮仅改 Lua、回放、工具与文档，未重建原生引擎或执行压力测试。

仍未覆盖 GPU 资源动态驻留、完整候选房间异步准备、真实设备与最终性能验收。
地图索引打开及标题菜单选档/读档仍同步，不将进入块恢复的完成扩大为所有 IO 已异步。

## 共享内容线程与 PNG 解码基础 · 2026-09-27

加载器已从 stream.cpp 分离为 `ScContentLoader`，地图校验仍在同一后台线程完成。
新增图片任务持有路径、预期尺寸和 RGBA 结果，固定 128 MiB 排队像素预算，
PNG 编码输入限 32 MiB，解析分配另限 256 MiB。默认轻量关闭独立解码器；
无窗口流式构建使用同一固定版本 stb_image，不要求 raylib 或 GPU 上下文。

必要检查通过：

- `test_content_loader`（Release、ASan/UBSan）：真实 RGBA/调色板透明 PNG、
  Unicode 文件路径、尺寸改变、截断/非 PNG/超量/缺失输入、修复后再读；
  排队预留、取消期间额度保留、过期票据、异常转换和像素独立于加载器存活。
- `test_stream`（Release、ASan/UBSan）：既有顺序/预算/失败/对象校验；
  生命周期检查现将地图与图片放在同一受控慢线程上，验证取消旧房间/候选请求、
  队列顺序以及存活图片和新房间互不污染。
- 宿主两个定向回放（Release、ASan/UBSan）：
  `test_room_loader_isolation`、`test_scheduled_publication_and_recorded_frames`。
- 完整图形、全功能无窗口 sanitizer、轻量图形构建；轻量构建命令不包含新增
  content_loader/png 源文件或独立 stb_image 包含路径，单帧无窗口运行确认
  `sc.stream.available=false`，没有注册 open。

没有改变 Lua 接口、样例或绘制路径，未重新运行样例全流程、截图或性能负载。
GPU 图片缓存及地图事务接入仍待实现；上述检查不证明运行时已按需上传纹理。

## 原生图片驻留与纹理适配器 · 2026-09-27

新增 `ScImageCache` 与 `ScGpuImages`，原生契约及未接入范围见
[图片驻留](../image-residency.md)。只运行以下相关检查：

- `test_image_cache` 在 Release 与全功能无窗口 ASan/UBSan 通过：重复引用不
  重解码、声明尺寸不一致、容量失败、无引用缓存复用、LRU 淘汰及旧 ID 失效、
  显式错误重试、统计，以及慢读取中销毁缓存不等待、取消队列不污染后续请求。
- `test_gpu_images` 使用真实 raylib 声明和模拟 GPU 资源，在 Release 通过；
  同一源码另用 LLVM sanitizer 编译并链接已检测的原生库，通过。检查每次上传
  次数、主线程约束、CPU 像素借用、共享引用、淘汰、上传失败保留活动纹理、
  重试以及每个成功创建的资源恰好释放一次。该测试不开窗口。
- `check_native_images` 使用真实隐藏/不获取焦点的图形上下文和同一适配器，
  上传后台生成的 RGBA 测试色块，实际绘制到 128×64 GPU 目标并读回。像素断言
  通过；实际查看 `build/image-cache-native-reviewed/colors.png`，左红右绿、边界
  清晰、无缺失纹理。manifest 保存测试程序哈希和查看结果。外层限时 15 秒，
  没有初始化音频设备，也未操作桌面输入。
- 完整与轻量引擎构建通过，轻量构建没有编译新增缓存/纹理适配器。

复现普通定向检查：

```powershell
cmake --build build/full --target test_image_cache test_gpu_images --parallel 2
ctest --test-dir build/full -R '^(image_cache|gpu_images)$' --output-on-failure
cmake --build build-llvm-sanitizers --target test_image_cache --parallel 2
ctest --test-dir build-llvm-sanitizers -R '^image_cache$' --output-on-failure
```

Sanitizer 按既有说明设置编译器 bin 的 PATH、ASAN_OPTIONS/UBSAN_OPTIONS。
GPU 模拟测试的无窗口 sanitizer 版本直接编译 `tests/test_gpu_images.cpp` 和
`src/render/image_cache.cpp`，使用 `include` 及固定 raylib 5.5 源码中的头文件；
链接 sanitizer 构建的 shiny_script、shiny_core、Box2D 和 yyjson 静态库，
输出 `build-llvm-sanitizers/test_gpu_images_manual.exe`。未链接 raylib 或打开 GPU。

原生捕获只证明新适配器的 GPU 上传/绘制；颜色由测试读取回调生成，不是 PNG
读取或游戏集成证据。宿主/Lua/世界事务尚未使用这些组件；未运行整套、压力测试、
样例重放、实体设备或跨平台检查，不能据此宣称流式图片功能已整体完成。

## 房间图片事务接入 · 2026-09-27

宿主、Lua 和渲染现已通过 `sc.images` 使用原生缓存；前两节“尚未接入”描述的是
当时状态。世界模块与样例的图片依赖收集仍待完成，见 [当前契约](../image-residency.md)。

必要检查结果：

- Release：`image_cache`、`gpu_images`、`images_integration` 三个 CTest 项通过。
  图片集成含七个用例：集合替换/清空、固定帧等待、解码失败取消、修复文件后同帧
  重试、房间生命周期/参数拒绝、完整内容检查、契约及未提交图片使用检查；其中
  法线依赖独立检查颜色/法线共同提交成功，漏提交法线在无窗口也报错。
- 全功能无窗口 ASan/UBSan：`image_cache`、七个图片集成用例通过。使用实际 PNG、
  临时项目和隔离存档；没有窗口。缓存新增受控取消测试：旧工作占满解码预留额度，
  新引用成功预留，poll 保持 pending，旧工作结束后同一 ID 成功读取。
- 完整与轻量构建通过；轻量单帧检查 `sc.images.available=false`、无 prepare，
  `stream=true` 明确失败。构建命令不含独立 PNG 解码或 CPU 图片缓存。
  `SHINY_STREAMING=ON / SHINY_ADVANCED_RENDER=OFF` 另外编译新绑定翻译单元通过，
  不将此单文件检查表述为该组合的完整构建验收。
- 两张实际隐藏、静音原生捕获在 `build/images-runtime-native-reviewed/`，已查看：
  成功提交后红/绿图片均出现；失败取消后旧红图保留。与对应 3/4 帧无窗口回放的
  watches 和帧数一致；manifest 保留捕获时的引擎哈希。之后的法线依赖检查只在
  无窗口执行，未将旧截图冒充该新增行为的图形证据，也没有重复捕获。
- 完整/轻量的生成文档及声明保留定向检查通过，13 个样例 API 副本一致，七个
  SDK 清单通过审计；含注解的六个 SDK 更新为 dev.24，snapshot 保留原版本。

期间修正的问题：早期回放夹具缺少必需 gamepad 字段；新类型元数据缺 constraints；
生成器测试将参数注解子串当成完整行重复，并对关闭模块比较其不存在的类型章节。
均已修正对应夹具/元数据/测试；测试启动现在保留 unittest 用例选择参数，允许只跑
相关项。没有为通过测试放宽图片失败、容量或提交规则。

未运行整套、压力/长时、硬件设备或跨平台测试；未声称候选房间异步加载、地图
资源事务、显式缓存热重载或最终性能门槛已完成。

## 流式世界图片依赖与样例接入 · 2026-09-27

`stream_world` 的 residency 选项已接入 Tiles 图片依赖、对象精灵和显式 retain_images，
在图片准备成功后保存离开块，再联合发布世界和纹理。地图修改需要新集合时进入
patch 事务；同集合修改仍立即提交。`sc.images.prepare` 现在接受声明名称/路径、
跳过已验证的常驻图片并补齐绑定法线图。旧 API 的严格 stream=true 输入限制已替换。

必要检查：

- Release：八个 `images_integration` 用例、五个 `stream_images_integration` 用例通过。
  世界检查覆盖解码失败保留/取消、修复同请求后继续、回访恢复对象状态、地图修改
  先准备图片后替换碰撞、清空世界释放引用、图像层/动画所有帧依赖，以及图片已就绪
  但实体容量不足时保留旧世界/纹理；另检查 patch 取消不误取消不存在的区域请求。
- 全功能无窗口 ASan/UBSan：上述图片 API 和世界资源用例通过。仅为本次原生边界
  改动重建引擎，未重复缓存基础/全套/压力测试。流式开启、高级渲染关闭的绑定
  翻译单元也完成编译；此项不是该组合完整发行验收。
- 现有默认预载路径的三个世界用例通过：coordinator、map_edits、async_cancel_and_publication_failure。
- Wayfarer 内容 `--check-all` 通过。首轮任务回放揭示名称/路径混用导致重复图片阶段；
  修正对象资源名称对应后，原样 3243 帧回放完成 24 株药草、清路和结局。保留实际
  输出在 `build/wayfarer-image-route/`；复用该存档验证 16 个块、24 个删除记录、道路
  修改及新进程结局恢复，未再重复完整路线。原回放没有缩减负载或改写输入。
- 两次隐藏、静音原生捕获：`build/stream-images-world-reviewed/retained.png`
  显示失败取消后仍在的红色地形/绿色对象；`build/wayfarer-image-world-reviewed/wayfarer-world.png`
  显示森林、药草和人物资源，已实际查看，无缺图。各自帧数 10/180；原生与相应
  无窗口 watches 一致。manifest 保留捕获程序哈希。这是图片驻留检查，没有重新
  修复或验收已解决的 UI 字体问题；未重复对话/日志截图。
- API 文档和 13 份本地注解一致，七个 SDK 清单审计通过；有变更的六个项目固定为
  dev.25，snapshot 保留原版本。Wayfarer 的两个标准模块副本与源码一致。

测试夹具初次射线跨过加载边界，无法区分移除地形与边界碰撞；已改为完全位于
已加载区域内的射线，再验证地形移除。保留范围/命中断言，未放宽引擎行为。
独立字体检查、完整图形硬件、音频、跨平台、性能和长时测试均未执行。候选房间的
异步初始化、成功缓存内容失效及最终完整版交付仍待完成。

## 显式图片重载 · 2026-09-27

`sc.images.reload()` 和 `World.reload_images` 已实现。缓存将未发布的新版本与
活动版本分开；提交后改变后续查找首选，旧引用独立存活。取消/失败不会把新内容
暴露给后续 prepare。重载不重建世界、不写存档；相同尺寸、新旧共同预算的限制
见 [图片重载契约](../image-residency.md)。当前不自动监听文件或绑定 F5。

本次必要验证：

- Release 五项通过：image_cache、gpu_images、images_integration、
  stream_images_integration、image_reload_integration。新增原生缓存检查同时持有
  新旧像素、私有版本查找隔离、提交预检、别名共享、其他房间旧引用、容量不足，
  以及第二张重载图片预留失败时回退第一张，完整保留原批次。
- GPU 模拟用真实 API 声明检查重载上传失败保留旧纹理、成功产生不同句柄、旧图
  在最后一个引用后才销毁，以及所有创建恰好释放一次；未打开设备。
- 全功能无窗口 ASan/UBSan 的 image_cache、stream_images_integration 和
  image_reload_integration 通过。没有重跑压力/长时或完整样例任务。
- 六个文件变更用例通过：提交、准备后取消、损坏文件、尺寸变化、修复重试、
  流式世界重载。通过 JSON Lines 调试协议，在旧图已提交的暂停帧替换实际 PNG，
  避免用睡眠猜测读取时机。重试保持失败所在固定帧；流式世界 reloaded 事件后
  chunks 表保持同一对象，未执行地形或对象重建。
- 两个实际隐藏、静音、六帧原生用例在 `build/image-reload-native-reviewed/`。
  已查看提交后的绿色图和损坏后保留的红色图，中心像素与预期 RGB 精确相同；
  每个用例还在重载处理后再次 prepare，确认后续缓存查找选用正确版本。
  manifest 保存引擎哈希与视觉结果，protocol.json 记录固定帧控制，stderr.log
  保留真实错误。没有使用样例或字体旧截图替代本次证据。
- 本地 API 注解与七个 SDK 清单已同步审计；包含注解的六个 SDK 为 dev.26，
  snapshot 保留原版本。关闭模块的独立发行构建未在本次重跑。

未执行真实 GPU 的故障注入（上传失败用模拟 API 检查）、跨平台/实体设备、
性能门槛或最终发行验收。候选房间异步准备仍待接入。

## 2026-09-27：候选房间初始图片准备

`scene.preload_images` 使用既有图片依赖解析与房间引用；init 后准备、首次 draw 前
自动提交。宿主保留候选 RuntimeOwner，跨展示帧推进 CPU/GPU 准备；活动房间继续
UI/绘制，固定输入与 trace 等待最终结果。失败释放候选并保留原房间和应用状态。
初次启动使用无额外世界的原生加载提示，F5 与普通切换共用候选路径。

本轮实际执行：

- Windows Release `room_preload_integration` 五个用例通过：初始化/切换、下一帧
  输入边沿与旧句柄、帧数边界、声明校验/未提交 UI 图片、check-all；另含损坏 PNG
  和首次 draw 错误的失败退出/释放子用例。
- 既有八个 `images_integration` 用例通过，覆盖本次复用的名称/法线依赖解析。
  `debug_load` 通过，涵盖候选加载断点、提交、预算及退出；发现并修复候选断点
  退出后再次调用旧房间 draw 的问题。
- 全功能无窗口 LLVM-MinGW ASan/UBSan：`room_preload_integration`、`debug_load`
  通过，启用 halt_on_error；没有运行全套或压力长测。
- 轻量构建成功；缺少 streaming 时显式拒绝 preload_images，普通房间仍可启动。
  API 注解生成检查通过，13 份示例注解同步，六个更新 SDK 使用 dev.27；七个现有
  SDK 清单审计通过。
- 隐藏、不聚焦、静音、隔离存档的两帧原生运行：success/decode/draw 三种情况
  均通过逻辑状态及像素断言，并逐张查看真实截图。证据在
  `build/room-preload-native-confirmed/{success,decode,draw}/`，各自包含项目、回放、
  trace、stderr、最终状态和 PNG，根目录 manifest 保存捕获时引擎哈希和目视记录。
  多图片每帧有界上传使成功/首次 draw 失败场景必经等待，日志确认旧 UI 与绘制
  仍执行；不是仅以解码库能力推断宿主响应性。

边界：本轮没有硬件交互、跨平台、整房间异步或性能验收。Lua/init、项目/PNG 头
检查、地图索引、常驻资源、音频与 shader 编译仍在主线程。候选失败在无窗口模式
直接返回诊断，图形模式保留当前房间。中文字体问题未重新打开或重复验证。
