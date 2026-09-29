# 样例视觉检查 — 2026-09-26

样例开发的固定规则已写入 `examples/AGENTS.md`：画面/UI 改动选择相关状态，
隐藏、静音、有界捕获后实际查看；检查文字、裁剪、遮挡、焦点、资源与相机，
发现问题后只复查受影响状态，避免重复全量测试。生成截图不自动算作检查通过。

使用 Windows 完整 Release 引擎、真实原生渲染、1152×648 输出和整数缩放，
静音、固定按键回放、隔离存档目录。`--capture-hidden` 在创建时设置隐藏和
不获取焦点，且要求有界 `--capture`；拒绝无边框设置。Windows 窗口枚举观察到
该进程的窗口，采样中无可见窗口。此模式需要图形驱动和显示环境。

已实际查看：Wayfarer 标题、中文对话、日志/装备与森林；Crossing 渡台；
Barrage 第一波。最初只调整字号和日志底色，仍把中文栅格化到 384×216 场景再放大；
用户指出中文模糊后，确认此前可读性判断不合格，旧图不作为文字质量通过证据。
这些样例仍使用简约原始资源，不将基础画面检查作为最终美术品质验收。

现已将非分层 screen/UI 命令移至窗口分辨率合成，TTF/OTF 字形按实际显示尺寸
按需生成，布局仍使用共享逻辑度量。场景继续按原有像素分辨率绘制；UI 顺序、
嵌套裁剪与黑边映射保留。字体页按码点页和栅格字号缓存，缩放变化清除旧尺寸，
避免反复调整窗口积累版本；每字形栅格上限 512 像素，每字体总预算仍为 128 页。
内置位图字体保留像素风格。原生世界坐标文字仍随场景分辨率绘制。

实际查看的新图在 `build/wayfarer-native-ui-reviewed/`：同一对话第 25 帧、日志
第 36 帧和标题。中文字笔画已清晰，未见文本或焦点边框裁切。
`tests/native_ui_integration.py` 的原生检查覆盖字形细节不再重复为 3×3 像素块、
嵌套裁剪/恢复、整数缩放、黑边和平滑缩放及运行中调整窗口；通过。
最终帧捕获/隐藏状态检查通过。另使用现有无窗口 ASan/UBSan 构建运行 45 帧日志
回放通过；它不覆盖 GPU 字形页，图形缓存的 sanitizer 验证仍未执行。

截图还发现缓冲区交换后的读取会拿到上一帧。两帧原生红/绿场景修复前输出红色，
修复后输出最终绿色；现在在交换前捕获，包含窗口缩放、黑边与原生面板。
PNG 读取/编码耗时计入 profile 的 diagnostic_ms，单列于正常 CPU 处理时间之外。
`tests/capture_integration.py` 检查最终像素、隐藏状态、参数约束和诊断归属，通过。

复现命令（仅选与改动相关的场景，输出目录不能已存在）：

```powershell
python tools/capture_samples.py build/full/shiny.exe --output build/captures --case wayfarer-dialogue --case wayfarer-journal
python tests/capture_integration.py build/full/shiny.exe
python tests/native_ui_integration.py build/full/shiny.exe --output build/native-ui-check
```

工具保留 PNG、运行日志、最终快照及 manifest；`visual_review` 初值是 pending，
生成成功不等于人工查看通过。只在实际查看后更新该记录。
早期截图在 `build/sample-visuals-reviewed/` 与 `build/wayfarer-text-reviewed/`；
后者仍是低分辨率文字，不能作为修复后的质量证据。2026-09-27 使用当前
`build/full/shiny.exe` 隐藏、静音重跑同一对话 25 帧，实际查看
`build/wayfarer-font-review-20260927/wayfarer-dialogue.png`：中文笔画清晰，
正文、HUD 与继续按钮无裁切。对应 manifest 记录引擎哈希；这些是本地证据，
未纳入发行包。

同日针对用户提供的旧模糊截图，用当前完整构建再次捕获对话第 25 帧及日志第 36 帧，
实际查看两张图均字形清晰、无裁切。新证据在 `build/wayfarer-font-current-reviewed/`，
manifest 含本次引擎哈希与查看结果；仅执行这两次隐藏、静音、有界回放。

三款独立测试包还在源码目录外解压，清除开发工具 PATH 后完成内容检查和
隐藏原生回放。实际查看渡台、中文日志和第一波截图，未见资源缺失或文字裁切；
记录位于 `build/package-visuals-20260926/`，详情见 [发行依赖](../../guides/packaging.md)。

中文修正及内容裁剪后的最新包也完成了相同移动检查，实际查看渡台第 435 帧、
中文对话第 25 帧、第一波第 180 帧。新记录在 `build/package-visuals-native-ui-20260926/`，
包含对应引擎 SHA-256；它替代旧发行包的中文质量结论。

结局检查补充：`build/sample-endings-reviewed/` 实际查看了 Crossing 通关、
Wayfarer 任务完成、Barrage 胜利和失败四张原生图。完成统计、中文和焦点边框
清楚，未见裁切/重叠；弹幕失败不再误写 COMPLETE，重复的上次成绩提示已移除。
三款回放、恢复和重新开始检查通过，Barrage 另检查 `last_result` 跨进程恢复。

结局捕获是显式选择项，例如：

```powershell
python tools/capture_samples.py build/full/shiny.exe --output build/endings --case crossing-ending --case wayfarer-ending --case barrage-ending --case barrage-defeat
```

工具先用原样回放生成真实存档，再用正常菜单操作恢复并捕获 4–10 帧原生输出。
它保留准备阶段摘要、恢复输入、隔离存档和 PNG，检查结局状态/胜负后仍将视觉
记录置为 pending，实际查看后才标为通过。布局复查可复用保留的存档，不必重跑
完整流程。这验证恢复后的结算画面，不等于看过整个游戏的所有动画或转场。

本次未验证音频设备、实体手柄、IME 候选窗、窗口模式切换、
高级光影或 Linux/macOS。隐藏窗口不能代替这些实机验收，也未开展压力测试。
