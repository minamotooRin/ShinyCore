# 粒子列存储验证 — 2026-09-24

覆盖范围：专用粒子列、活动区间、稳定压紧、容量失败和基础批量绘制。

- Windows GNU Release 完整和轻量构建，以及 LLVM-MinGW ASan/UBSan 无窗口构建，
  均通过 core、systems、content、script、contracts、profile、features、
  complete_integration 八项 CTest。
- 原生测试覆盖整批容量预检、失败不推进视觉随机数、删除后的列关联和透明
  顺序、存储地址复用、活动粒子禁止重配、过期回收、零容量释放存储。
- Lua 测试覆盖 used/requested/capacity 错误、失败批次没有部分写入和零容量。
  原有确定性模拟、无效浮点输入及玩法随机数隔离测试继续通过。
- `examples/particles` 使用 smoke.txt 执行 90 帧，原生与无窗口最终 JSON 完全
  相同。原生 profile 共 90 帧，容量始终 2048，峰值活动 749，最后活动 704。
- 已查看实际原生截图 `build/full/particle-columns.png`，三组不同颜色透明
  粒子显示正常。输出来自新批量绘制路径。
- API 描述、注解及示例内副本已同步；生成契约校验和差异空白检查通过。

复现命令见 `examples/particles/README.md`。这不是 20,000 粒子与敌人、弹体
同时运行的性能验收；没有执行指定硬件基准、Linux/macOS 图形或两小时耐久。
上述初次列存储验证尚未覆盖可配置发射器；后续实现见下节和[当前契约](particles.md)。

## 同日补充：发射器与曲线

- `sc.particles.define/burst/stats` 已接入；模板最多 64 个，曲线 2..8 个关键点。
  可配置速度、寿命、方向范围及重力倍率。Lua 标准模块按固定更新积累小数
  发射量，不逐粒子回调；暂停或关闭发射器时停止累计。
- 原生测试验证半寿命尺寸/RGBA 插值、重力覆盖、模板容量、无效曲线不占槽、
  压紧后模板关联、容量失败不推进 RNG。Lua 验证初始化阶段、跨房间旧句柄、
  draw 禁止发射、连续发射余数和容量失败原子性。
- 模板字段、默认值、范围、曲线键和函数阶段已进入 `--api` 并生成注解；
  契约测试从实际元数据逐字段测试非法类型、上下界与默认值。
- 完整、轻量、ASan/UBSan 构建的同八项 CTest 全部通过。
- 示例改为三个每秒 120 粒子的持续发射器。90 帧固定回放原生/无窗口 JSON
  完全一致，最终 watch 为 354 活动粒子、2048 容量、3 模板。已查看实际截图
  `build/full/particle-emitters.png`，尺寸、变色和透明淡出显示正常。

当时纹理与混合模式尚未实现，后续见下节。以上小场景不能证明完整版组合性能负载达标。

## 同日补充：纹理与混合模式

- 发射器新增声明式 PNG 区域和 alpha/additive 混合模式；裁剪保持宽高比，
  复用房间缓存，绘制只合并连续相同状态，不改变粒子创建顺序。
- 原生与 Lua 测试新增非法资源、裁剪范围/类型、模板复制和混合模式检查。
  完整、轻量、ASan/UBSan 的同八项 CTest 均通过。
- 新增 `examples/particle_materials` 与可执行原生像素检查脚本。
  完整和轻量 Windows 图形构建均通过 **438 个像素断言**：普通透明、叠加、
  先叠加后透明、图集透明/不透明像素、宽高比、白纹理恢复、场景透明矩形及
  最早粒子过期后的覆盖顺序。8 帧原生和无窗口最终状态一致，9 活动粒子。
- 检查发现已合成场景与最终目标使用普通透明复制，会再次乘 alpha 导致变暗。
  两处目标复制改为颜色直接覆盖后，像素符合一次混合的计算值。已查看真实
  截图 `build/full/particle-materials.png`。
- Linux/macOS/Windows 图形 CI 已加入相同脚本。**本地只执行了 Windows**，
  尚无本次远端 Linux/macOS 运行证据，不将工作流配置算作平台验收。
- 正片叠底未公开：后端现有公式不能正确保留该纹理路径的透明区域。

复现：`python tests/particle_materials.py build/full/shiny.exe --output build/full`。
脚本需 Pillow；发行游戏不需要 Python。未执行组合性能负载或新增硬件验收。
