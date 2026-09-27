# Crossing 场景背景（2026-09-27）

新增一张本地背景图集，为三关提供渡槽、山谷水磨坊和信号塔废墟。使用内置图像
生成工具制作，无参考图片；简要生成要求和实际区域在样例 assets/README.md。
原有角色、声音及音乐资源保留。PNG 为 1672×941、2,438,040 字节；解码 RGBA
约 6 MiB。游戏运行时无需生成工具、Python 或网络。

通过已有 sc.image 源区域和 layer=-100 接入，每帧一个命令，不创建普通实体。
背景限制在两侧墙之间、地面上方，保留 ASCII 碰撞边界；低亮度染色将真实角色、
平台、开关、光点和 HUD 与装饰背景分开。项目资源声明和打包清单已同步。

必要检查：

- `--check-all` 验证全部声明房间及首次绘制资源；打包依赖闭包包含新 PNG。
- 三个场景通过实际键盘回放到达、F6 保存，再正常 LOAD 后十帧原生渲染。新增
  `crossing-aqueduct` 捕获项与磨坊/信号塔共用该短流程，原有渡台长回放项仍可使用。
- 已实际查看 `build/crossing-scenery-reviewed/` 三张截图，画面区分明显，角色、
  机关、HUD、地面边界可辨，没有图集跨行串色或新增文字遮挡。
- 磨坊与信号塔同一恢复输入下的 entities、state、state_hash、contacts、rng、
  tick、watches 与上一轮无背景截图记录一致。未修改模拟/存档行为。

```powershell
python tools/capture_samples.py build/full/shiny.exe --output build/crossing-scenery-new --case crossing-aqueduct --case crossing-mill --case crossing-signal
```

目录保留准备输入、正常存档、恢复输入、快照、日志、截图及引擎/图集哈希，
manifest 记录目视通过。原生运行全部隐藏、不聚焦、静音。本轮不涉及原生内核或
Lua/C 边界，未重建引擎、运行 sanitizer、完整通关回归或性能压力测试。

这完成背景接入，尚不代表全部前景美术、动画、音频、5–10 分钟人工体验或最终
发行验收。三个小样的其余品质与原始完整版要求仍待继续完成。
