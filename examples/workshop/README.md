# 星灯工坊 / Starlight Workshop

这是 ShinyCore 0.2 的小型功能示例。主房间包含 Tiled 地面与平台、Lua 动画角色、动态箱子、运动平台、斜坡及中文 UI；走到右端进入第二个房间。

```sh
./build/shiny --check-all examples/workshop
./build/shiny examples/workshop
./build/shiny --headless examples/workshop --frames 200 --replay examples/workshop/replays/smoke.txt --save-dir ./test-saves
```

A/D 移动，Space 跳跃，Down+Space 穿透单向平台，E 保存并播放音效，Up 读取检查点。第二个房间按 E 返回。检查点恢复显式数据并重建房间；它不会精确还原保存瞬间的位置和物理状态。

`game/controller.lua` 实现跳跃缓冲、coyote time、斜坡速度与移动平台支撑；`game/animation.lua` 实现帧时长、循环和标记；`game/locale.lua` 是普通翻译表。它们都是游戏代码，可以直接修改。

## 在 Tiled 中编辑

直接打开 `workshop.tmj`，保持有限地图、正交、正方形网格、right-down 绘制顺序。保存为 JSON `.tmj`，图块数据使用整数数组。图集可以内嵌，也可以使用项目内的 `.tsj`。

- 在 tileset 图块的自定义属性中设置字符串 `collision=solid` 或 `collision=one_way`；未设置时为空。需要斜坡时，为图块添加一个凸多边形碰撞对象，3..8 个有序顶点，旋转为 0。
- 图块层支持可见性、透明度、像素偏移与 H/V/对角翻转。图集 tile 尺寸必须等于网格，图片支持 margin/spacing。层按文件顺序编号，实体 layer 可在它们之间排序。
- 对象层只提供数据。使用 `sc.objects()` 读取 name、x/y、properties 等记录，再由 Lua 工厂调用 `sc.spawn()`；不会自动创建身体。
- 不支持无限地图、压缩/Base64、组层、图片层、视差、染色、模板、瓦片动画或六边形旋转。加载器会给出明确错误。

使用 `sc.map("terrain", x, y, gid)` 编辑图块，并在下次物理同步更新碰撞。旧 ASCII `sc.tile` 是独立的基础地图接口；不要用它读 Tiled GID。

## 状态版本与迁移

项目 ID 为 `shiny.workshop`，当前 data_version 为 2。`game/migrate.lua` 展示版本 1 的 `gold` 字段迁移为 `coins`，并保留其他字段。引擎传入数据副本，禁止迁移修改引擎世界，并再次校验返回值。存档失败或未来版本不会覆盖旧文件。

原生默认位置为系统用户数据目录下的 `ShinyCore/shiny.workshop`（Linux 目录名小写）。无窗口默认内存存档；使用 `--save-dir` 进行可重复的磁盘测试。

## 素材与许可

`keeper.png` 复用本仓库原创 Lantern 素材，MIT。`tiles.png` 与短音效 `chime.wav` 为本项目程序生成素材，MIT；`theme.ogg` 是相同音效的 Ogg Vorbis 演示版本。它们用于验证资源管线，不代表完整音乐作品。

`workshop.ttf` 是 [Google Fonts 的 Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc) 的字重 400 静态子集，保留 ASCII 与 project.lua 中列出的中文字符。字体使用 SIL Open Font License，完整文本随包保留在 `assets/OFL.txt`；字体不适用项目 MIT 许可。

若 UI 新增汉字，需要同时扩充字体文件子集和 resources.ui.characters。只添加 characters 不会凭空增加字体中缺失的字形。`tools/build_assets.py` 记录素材生成方式；运行游戏无需 Python 包或 FFmpeg。
