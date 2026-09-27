# 流式地图通行掩码烘焙

`tools/nav_bake.py` 用**同一个原生引擎**读取已构建的 format-3 地图块，
由 `shiny.stream_tiles` 准备静态地形，再调用 `sc.navigation.mask` 烘焙通行数据。
Python 不实现另一套 Tiled 碰撞栅格。此工具在开发期运行；游戏发行时不需要 Python。

```sh
python tools/nav_bake.py build/full/shiny.exe examples/wayfarer/maps/world build/wayfarer-nav.json
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

当前产物是**静态通行快照**，不包含存档中的地图修改；它尚未形成跨块连通图，
也没有接入 `World.route`。下一步是从这些掩码生成分量与边界入口，运行时按入口
规划已加载窗口外的路线，并对已加载的动态修改失效或重建相关分量。当前
`World.path/flow` 仍只查询已发布窗口；参见[流式世界](stream-world.md)。
