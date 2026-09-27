# 流式地图通行掩码烘焙

`tools/nav_bake.py` 用**同一个原生引擎**读取已构建的 format-3 地图块，
由 `shiny.stream_tiles` 准备静态地形，再调用 `sc.navigation.mask` 烘焙通行数据。
Python 不实现另一套 Tiled 碰撞栅格。此工具在开发期运行；游戏发行时不需要 Python。

```sh
python tools/nav_bake.py build/full/shiny.exe examples/wayfarer/maps/world build/wayfarer-nav.json
python tools/nav_graph.py build/wayfarer-nav.json my-game/maps/forest_route.lua
# 可指定与游戏一致的格子尺寸、身体半径和包含空块的矩形范围：
python tools/nav_bake.py build/full/shiny.exe examples/wayfarer/maps/world build/area-nav.json \
  --cell-size 8 --radius 0 --bounds 0 0 3 3
```

未指定 `--bounds` 时覆盖索引中非空块的最小矩形，也包含其中没有文件的空块。
每块周围至少读取一圈邻块，避免边缘裁断；较大图块偏移会扩大静态地形来源范围，
对象层使用索引中的跨块覆盖锚点。导航区域仍是 3×3 块，原生身体余量在中心块提取。
结果每块提供 `x/y` 和等宽的 `.` 可通行、`#` 阻挡行，按 y、x 升序排列。
输出还记录原始索引和块文件的 SHA-256，以及包含工具版本、参数、引擎二进制和
图块模块内容的烘焙指纹。输出使用同目录临时文件原子替换，可删除后重建。

格子尺寸须整除块的像素宽高；半径不超过较短的块边。3×3 导航区域最多
16,384 格，单次地形来源最多 256 块，总扫描范围最多 16,384 块；超限明确失败。
每次最多在一个临时无窗口项目中扫描 256 块，避免把全地图放入单个 Lua VM。
原生地形/流式缓存的容量错误也会直接报告，绝不输出部分有效文件。

`nav_graph.py` 把每块的四邻域连通分量和相邻边界上的首个入口编译成普通 Lua 表。
完整开放块只存一个分量号；有阻挡但仍连通的块保留掩码行，分裂的块保留格子分量号。
生成模块与项目一起发行，不需要 Python；`require` 可由打包器追踪。图中每对分量
只保留一个确定的入口，路线保证静态连通，不承诺最短像素路程。

```lua
local Route = require('shiny.stream_route')
local graph = Route.new(require('maps.forest_route'))
local result = Route.route(graph, player_x, player_y, goal_x, goal_y, 16384)
-- result.points = 起点、每次跨块的入口两侧格子中心、终点。
```

`Route.route` 查询世界像素坐标，返回 `status, visited, points, revision` 字段；状态为
`ok`、`unreachable`、`unloaded` 或 `budget_exhausted`。它遍历块分量图，
适合按目标建立一条长程路线，不应为 2000 个单位每帧各跑一次。游戏需要按下一入口
预取块，再用 `World.path` 或共享流场验证和执行当前已加载路段；未准备块仍受加载边界
保护。图的 `radius` 应与局部寻路半径一致，`cell_size` 应与 World 设置一致。

烘焙产物是静态基线。存档恢复或 `World.patch` 提交后，可对**已经发布**的块调用
`World.refresh_route(world, graph)`：它读取当前原生 `sc.navigation.mask(graph.data.radius)`，
仅替换通行掩码发生变化的块，重建相关分量和跨块入口。返回变化的块数；整批验证和
重建成功后才发布新图，`revision` 增加一次。`World.update` 返回 `published` 或
`patched` 后调用；同步 `World.patch` 成功返回时也可调用。等待、失败和取消期间不调用。
直接使用时，`Route.refresh(graph, mask, {{x=0,y=0}, ...})` 的块列表必须是完整、
已确认加载的块；掩码需覆盖每块全部单元，尺寸和半径须与烘焙图相同。半径大于
半个格子时，原生清障计算会把窗口边缘视为阻挡；此时刷新还要求块周围有足够的
已加载掩码边缘。`World.refresh_route` 自动跳过尚无这圈数据的边缘块，待邻块发布后
再刷新，避免把窗口截断误记成地图阻挡。

已卸载块保留本房间中上次确认的动态摘要；未曾加载的远处块仍使用静态基线，
因此尚未读入的存档修改无法提前影响粗路线。游戏仍须预取入口，用 `World.path`
验证已加载路段，并遵守加载边界；粗路线不是最终移动许可。
参见[流式世界](stream-world.md)。
