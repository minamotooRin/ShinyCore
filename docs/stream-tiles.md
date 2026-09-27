# 流式图块绘制

`sc.stream.metadata()` 在 load/init/update 返回索引头的独立副本：format、
chunk_size、tilewidth、tileheight、layers、tilesets 和 parallaxoriginx/y，
不复制全部块目录或对象。
每个房间初始化时读取一次，传给 `shiny.stream_tiles`：

```lua
local Tiles = require("shiny.stream_tiles")
local view, prepared
-- init
sc.stream.open("built/map-world/index.json")
view = Tiles.new(sc.stream.metadata(), {["assets/tiles.png"]="tiles"})
-- 块已发布后，在 update 中准备；赋值前的错误保留上一份 prepared。
prepared = Tiles.prepare(view, {sc.stream.get(0, 0), sc.stream.get(1, 0)})
Tiles.update(view, dt)
-- draw：读取原生相机的当前位置和旋转/缩放后的可见范围。
Tiles.draw(view, prepared)
```

第二个构造参数将图集路径映射为项目声明的 image 资源名；缺项时以路径本身
作为资源名。模块不绕过资源声明或运行时下载图片。

prepare 处理已加载块，不创建普通实体。绘制按图层索引、全局图块行列排序，
不依赖传入块顺序或 Lua 表遍历顺序；支持负块坐标、层偏移、透明度与可见性、
图集 margin/spacing、tileoffset、GID 翻转和对角变换。图块底边与地图单元底边
对齐，允许图集图块尺寸不同于地图单元尺寸。重复块、未知 GID 或错误层索引
会使准备失败；新 prepared 不自动替换当前画面。

图像层沿图层顺序绘制，继承组的偏移、透明度、可见性和视差倍率。离线工具
从 PNG 头读取 imagewidth/imageheight，保留 repeatx/repeaty；运行时按视口
裁剪和重复，超过单条绘制命令 4096 像素上限时分段。图像层使用同一资源名
映射，不需要创建对象或非空图块；即使 prepare 输入空块数组也可绘制背景。
图块层和图像层均使用地图的 parallaxoriginx/y 计算视差。构建工具版本已提升，
需要重新构建旧资源缓存以写入图像尺寸；不添加兼容猜测。

图层着色：离线转换接受 Tiled 的 `#RRGGBB` / `#AARRGGBB` tintcolor，沿组路径
逐分量相乘，最后写入扁平层的有效 tintcolor；透明度仍独立相乘。运行时组合 tint
alpha 与 opacity，再转换为 sc.image 的 `#RRGGBBAA`。图块层/图像层使用同一
颜色路径；未声明着色时保持白色。对象层的 GID 图块对象自动使用同一颜色路径；
普通对象的实体外观仍由游戏工厂决定。

构建器版本 9 验证源层及继承结果的 opacity、visible、offset、parallax、tintcolor；
非法输入指向源地图和层路径。非 normal 混合模式和 transparentcolor 颜色键当前
明确拒绝，后者请在 PNG 中使用 alpha，不再默默忽略。块格式仍为 3，新增的
有效着色不要求改变原生接口。

update 使用固定步长推进动画（秒，0..0.25）；draw 只读取动画时间和 `sc.camera.read()`，
按相机显示锚点 anchor_x/y 计算视差、按 visible 世界矩形裁剪，提交世界坐标 `sc.image` 命令。
也可显式传第三参数 `{x,y,visible={x,y,w,h}}` 复用同一份相机快照。源区域仍由原生
PNG 尺寸检查。可见图块计入 project.limits.draws，超出容量明确报错。

视差只偏移世界位置，缩放、旋转、震动由原生相机统一处理。图层零基索引作为
`sc.image` 的 layer，与原生实体和地图层混合排序。同层图块先于实体；将实体 layer
放在背景与前景层号之间即可获得遮挡。分层地图参与场景光照，不进入 UI 裁剪栈。
支持图集式及图像集合式 tileset、tilelayer、imagelayer 与 objectgroup 中的
GID 图块对象。图块对象按层的 `draworder`（topdown/index）、原始对象次序、
`objectalignment`、tileoffset、旋转、缩放、GID 翻转与可见性绘制；删除标记
立即隐藏图片。对象层的其他对象仍由 `stream_objects` 或游戏脚本处理。图块
对象碰撞不会由图片自动生成；静态碰撞需显式的对象几何，移动障碍需实体物理。
GPU 资源准备与发布由 shiny.stream_world 协调；本模块负责准备绘制数据和资源名称。
导航窗口见下文，相机旋转/缩放由原生相机处理。

图像集合使用 `columns=0`，每个 tile.image 相对于其 tileset 文件解析，离线
工具写入项目相对路径和真实 PNG 尺寸，并把所有图片纳入缓存依赖。单张
图块图片尺寸范围 1..4096。图块 ID 可以不连续，引用缺失 ID 或无图片动画帧
会明确失败。动画可切换不同尺寸图片，裁剪使用当前帧尺寸，底边始终保持
地图单元对齐；跨块绘制顺序仍由全局行列决定。资源名映射同样适用于这些图片。

2026-09-26：一项定向无窗口联调通过，真实读取块并提交原生图片命令，检查
元数据副本、准备失败保留旧列表、动画、翻转、透明度、视差、隐藏层与裁剪。
无窗口检查不证明原生像素正确；本轮未启动图形窗口或性能测试。

同日新增图像层定向测试通过：实际构建 PNG 层索引、读取元数据并提交图片
命令，覆盖重复边缘、分数相机坐标、非零视差原点、隐藏层、层顺序、5000
像素宽图像拆分，以及非 PNG 输入诊断。仅执行这一新增测试，未扩大回归。

图像集合补充：外部 tileset、稀疏 ID、跨图片动画、尺寸变化、负坐标跨块
顺序和图片内容缓存失效已通过一项定向联调；另复验共用的图集命令路径。
两项均通过，没有重跑全套测试或启动图形窗口。

统一图层补充：排序实现同时由图形后端和无窗口原生测试使用。定向验证了
跨层、同层种类顺序、图片提交顺序、排序容量失败和 UI 裁剪误用；流式命令
联调确认实际提交 layer。相关脚本测试及 ASan/UBSan 复验通过。图形构建通过，
原生像素效果仍未验收。

## 流式地形

在 update 中准备并提交碰撞，再替换绘制列表：

```lua
local next_tiles = Tiles.prepare(view, loaded_chunks)
sc.stream.terrain(Tiles.terrain(view, next_tiles, loaded_chunks), Tiles.navigation(view, loaded_chunks))
prepared = next_tiles
```

`Tiles.terrain` 读取图块的 `collision` 属性：默认 empty，支持 solid 和
one_way。碰撞使用原图块尺寸及层偏移，不跟随动画帧改变。单向地形不允许
垂直或对角翻转。图块对象组优先于整块碰撞属性，支持矩形、旋转矩形、
凹多边形和以 16 边形近似的椭圆；离线分解为三角形并烘焙对象旋转和偏移，
运行时同步 GID 水平、垂直和对角变换。对象组不支持 one_way、点、折线、
文字、图块对象或模板，遇到这些输入明确报错。碰撞不随图层视差移动。

第三参数传入已加载块时，`Tiles.terrain` 也加入对象层中标记
`collision=solid` 或 `collision=one_way` 的静态形状。离线构建器烘焙矩形、
旋转矩形及凹多边形；one_way 只允许未旋转矩形。跨块形状只归锚点块所有，
`stream_world` 根据稀疏覆盖索引保留该块。修改图块后重建地形时仍保留对象
形状；卸载锚点块后形状消失。缺少烘焙数据的旧块会明确报错，需重新构建资源。
静态碰撞独立于对象实体和删除标记；可开关/移动的障碍应由游戏创建实体碰撞，
不要给它设置静态 `collision` 属性。

`sc.stream.terrain(shapes, navigation?, entities?)` 在 load/init/update 原子替换全部导入地形；空数组
清除导入地形，原 ASCII 图块仍保留。形状为世界像素矩形
`{x=0,y=0,w,h,one_way=false}`，可附加 3..8 个局部凸多边形顶点的平铺
`vertices={x1,y1,...}`。尺寸至少 0.16，坐标和端点限制在 ±1,000,000。
最多 16,384 个形状，Lua 数据转换另有 256 KiB 限额。

新物理块创建成功后才替换旧块；失败保留旧地形，未变化块复用。成功后解除
有限房间的外围碰撞和相机范围限制；相机仍有 ±1,000,000 数值边界。游戏应
配合 `shiny.stream_regions` 的 boundary 限制进入未加载区域。可通过 entities 参数联合提交进入对象；加载边界、对象持久状态和 GPU 资源由
shiny.stream_world 协调。已选择的局部导航格网障碍随地形一起更新。

2026-09-26：定向核心及流式地形联调通过，覆盖负坐标、射线、单向属性、
替换失败保留、空批次清除、局部物理块替换和解除有限边界；同组 ASan/UBSan
验证通过。未启动原生窗口、性能测试或扩大回归范围。

图块对象组补充：资源构建器版本 7 输出 `collision_shapes`，请重新构建资源；
运行时不猜测旧数据。2026-09-26 的单项端到端测试通过，覆盖真实离线构建、
负坐标凹多边形、水平翻转后的原生射线命中、旋转烘焙，以及退化/自交几何
错误定位。本次只修改 Python/Lua，未增加原生资源所有权，无需重建引擎。

## 局部导航网格

```lua
local grid = Tiles.navigation(view, loaded_chunks)
sc.navigation.region(grid.x, grid.y, grid.rows, grid.cell_size)
local path = sc.navigation.path(0, 0, 5, 5) -- 当前网格的局部单元坐标
local field = sc.navigation.flow(5, 5)
local dx, dy = sc.navigation.direction(field, player_x, player_y) -- 世界像素
```

`Tiles.navigation` 用已加载块的包围矩形生成覆盖行；块之间的未加载空洞为 `#`，
已加载单元为 `.`。网格最多 16,384 单元，可显式指定 1..256 的正整数单元尺寸，
要求整除块像素宽高；默认取地图图块宽高的最大公约数。超限应缩小活动导航区域，
不静默截断。网格原点可以为负，不改变物理地图或房间显示配置。

`sc.navigation.region(x,y,rows,cell_size=8)` 原子选择网格，同时根据已提交地形
栅格化障碍。行必须等宽且非空，只接受 `.` 和 `#`；原点及终点在 ±1,000,000。
失败保留原网格及流场。成功替换会释放旧流场，需要重新调用 flow；无参数恢复
默认房间导航。此接口在 load/init/update 可调用，draw 不可调用。

path 的输入与输出、flow 目标使用局部单元坐标；direction 和批量 steer 使用世界
像素。单元中心为 `(grid.x+(cx+.5)*cell_size, grid.y+(cy+.5)*cell_size)`。
后续 `sc.stream.terrain` 未传新网格时，成功提交会同步更新导航障碍；变化时流场返回 stale，
用 refresh 按节点预算重建。相同障碍不重启流场，失败提交保留原通行数据。
网格边界外不可寻路，不提供跨整个稀疏世界的全局路径或身体半径膨胀。

2026-09-26：单项无窗口联调通过，覆盖负原点、未加载空洞、地形通行数据
更新、流场失效/分步恢复、坏输入保留以及恢复默认地图。ASan/UBSan 同项复验
通过；没有启动图形窗口、性能测试或全套回归。

地形与导航联合提交：将 `Tiles.navigation` 的结果直接作为 terrain 的第二参数，
新网格和新地形全部预检成功后一起替换；坏行数据、未知字段或无效几何均保留
旧物理、网格和流场。传 nil 等同省略，保留原网格布局，仅刷新障碍。指定新网格
时释放旧流场，需要重新构建目标；默认房间网格也同步刷新导入地形的通行掩码，
因此以后恢复默认导航不会使用过期障碍。两个参数合计使用 256 KiB 转换预算。

2026-09-26：相关核心测试与单项联合提交联调通过，覆盖默认网格更新、三类失败
保留、成功替换及省略网格参数的障碍刷新；ASan/UBSan 同组通过。对象、加载
边界和 GPU 资源仍不属于这一事务，完整世界提交尚未完成。

## 同批创建进入区域的实体

`local ok, ids = sc.stream.terrain(shapes, grid, entities)` 可同时创建实体数组，
字段与 `sc.spawn_many` 一致。返回的 ids 与输入顺序对应；省略实体参数时只返回
true。返回数组在原生提交前分配，实体几何、对象持久 ID 和容量全部预检后，
才发布地形、导航和新实体；失败不消耗实体槽、生成号或持久 ID。

实体物理身体遵循普通 spawn 的同步时机，在下一次物理同步/查询创建；本接口
不把延迟的设备或求解器内存分配失败包装成可恢复的完整世界事务。实体参数
占用项目实体容量，地形与导航的 256 KiB 转换预算不包含实体批次。

2026-09-26：核心测试及单项流式联调通过，检查无效实体、容量拒绝、无效地形
均保留原世界，以及成功提交返回有序实体句柄。ASan/UBSan 同组通过。只执行
相关检查，未运行图形窗口或性能测试。

## 组层着色检查（2026-09-27）

工具定向用例覆盖多级色乘积、独立透明度、隐藏继承、偏移、视差，以及非法源值/
继承溢出诊断；原有图块命令和图像层两项定向检查通过。根模块与 Wayfarer 的本地
stream_tiles 同步为 SDK dev.38，哈希审计通过；其他项目未使用该模块，不改版本。

`python tests/native_stream_tint.py build/full/shiny.exe --output build/stream-tint-reviewed`
生成编译地图，经真实流式读取后分别运行无窗口/隐藏原生两帧。两个模式观察值
一致，图块/图像命令颜色正确，截图三处像素符合透明度混合（容差 2）。
`layers.png` 已实际查看，着色、说明和间距通过；命令、哈希和采样值在 manifest。
未播放声音、运行全套/压力或重建原生；没有重新处理历史字体问题。
