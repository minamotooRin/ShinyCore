# 材质与片元着色器

`SHINY_ADVANCED_RENDER=ON` 编译 CPU 材质与可用时的 OpenGL 3.3 后端，默认关闭。
`--api` 的 `advanced_render` 表示整组功能已编译，`materials`、`postprocessing`、
`geometry_shadows`、`normal_maps` 分别用于声明细分需求；它们跟随同一构建开关。
图形后端另由 `graphics` 表示，见[构建能力契约](../runtime/capabilities.md)。已有[定向原生视觉检查](../verification/systems/advanced-render.md)，
完整平台/硬件与性能验收仍独立记录，不由这些构建能力布尔值表示。
无窗口预设保留材质内容检查，不创建 GPU。

```lua
-- project.lua
return {modules={"materials"},resources={
    hero={type="image",path="hero.png"},
    tint={type="shader",path="tint.frag"}}}
```

```lua
-- init
local id=sc.material.create{shader="tint",uniforms={
    strength={type="float",value=.5},
    tint={type="vec3",value={.3,.9,.7}}}}
-- update: 全部预检成功才提交
sc.material.set(id,{strength=.8,tint={1,.5,.2}})
sc.material.bind_image("hero",id) -- 同图像的精灵、图块、弹体与粒子继承
sc.material.bind_entity(hero,false) -- 指定实体使用内置着色器
-- draw
sc.image("hero",x,y,32,32,{material=id})
```

材质归房间持有，最多 64 个，存储首次创建时分配。句柄包含房间 epoch、生成号
和槽号，可由 Lua 整数与 JSON 数字无损表示。销毁或切房间后旧句柄失效，生成号
耗尽的槽退休；不能将其存档为持久引用。capacity() 返回使用量及上限。

| API | 契约 |
| --- | --- |
| create(spec) | shader 是声明的 shader 资源名，uniforms 可省略；postprocess 默认 false，创建后不可变；未知字段报错 |
| set(id, values) | 修改已声明参数；未知名、类型或范围错误时整批不变 |
| reload(id) | 读取源码并递增请求版本；读取失败保留旧源码和版本 |
| info(id) | 返回 shader 路径、postprocess、revision、compiled_revision、status、error、uniforms |
| bind_image(image, id) | 声明图像的默认材质；0 清除。同一路径的资源别名共享绑定 |
| bind_entity(entity, id) | 活动实体覆盖；0 清除并继承图像默认值，false 强制内置着色器 |
| image_material(image) / entity_material(entity) | 读取默认/有效材质句柄，0 表示内置着色器；目标仍需有效 |
| destroy(id) | 先解除图像、活动实体和后处理链引用；释放 CPU 源码，后端在下次提交时释放程序 |
| capacity() | used、capacity=64、uniforms_per_material=32、textures_per_material=4、postprocess_textures_per_material=3、postprocess_passes=4；另有 image_bindings、image_binding_capacity=64、entity_bindings、entity_binding_capacity |

修改只允许 load/init/update；查询也可在 draw/ui_update 使用。普通参数与读取
错误抛 Lua 错误，可用 pcall 捕获。set 不改变参数名或类型。
`--api` 现在列出 `ScMaterialSpec`、`ScMaterialUniformSpec`、`ScMaterialInfo`、
`ScMaterialCapacity` 和 `ScPostprocessPipeline` 的字段、必填项、默认值与只读属性；
LuaLS 和 API 参考文档从这份原生元数据生成。材质函数拒绝多余实参，避免
调用成功却忽略 Agent 误传的数据。类型相关的 uniform 值约束仍以本节及
原生校验为准。

绑定前先创建材质。每个房间最多绑定 64 个图像路径，实体覆盖存储随材质池按
项目实体容量一次分配。销毁实体后其覆盖不再保留材质，新生成号不继承旧覆盖；
切房间清空绑定。绑定属于表现资源，不进入实体玩法字段或存档。

图像默认值覆盖 sc.image、实体精灵、有限/流式地图、图集弹体及纹理粒子。
sc.image 显式 material 句柄优先，material=false 使用内置着色器，省略时继承；
实体覆盖也可用于无纹理几何。绑定不能使用 postprocess 材质。更换图像或精灵
后按新路径查找默认值。提交保持透明顺序；批量绘制刷新缓冲后重新绑定辅助纹理。

最多 32 个 uniform；名字为 1..63 字节 GLSL 标识符，不接受 gl_ 前缀及 mvp、
texture0、colDiffuse、sc_scene、sc_resolution。类型为 float、vec2/vec3/vec4、int、bool、texture。
浮点分量须有限且在 ±1000000 内；向量必须是精确长度的数组，int 为 int32，
bool 为布尔值。texture 引用声明的 image 名，图像材质最多四个额外采样器，后处理材质三个；info 返回其
项目相对路径。浮点参数按原生 float 精度存储。

源文件 1..65536 字节，不含 NUL。片元着色器使用 GLSL 330，固定顶点阶段提供
fragTexCoord、fragColor 和 mvp；texture0 为当前图像，colDiffuse 为原生色调。
声明 uniform 必须在链接后活跃且实际类型匹配；未声明的自定义活跃 uniform、
数组、被优化掉的声明均报错。当前不支持自定义顶点阶段。
参见[示例](../../examples/materials/README.md)及[片元源码](../../examples/materials/tint.frag)。

每个版本只尝试编译一次。候选房间在替换活动资源前完成全部编译，失败保留旧
房间和 GPU 资源。活动 reload 失败保留旧程序及 compiled_revision，status 变为
failed，error 包含文件和驱动诊断；修复后显式再次 reload。运行期新材质没有有效
程序时不绘制对应图像，并向 stderr 报错，不使用默认着色器冒充成功。参数仍可
作用于保留下来的程序。材质作用于颜色阶段；法线阶段仍使用原图 alpha 和绑定的
法线图，自定义 discard/透明度变化不会自动同步到法线缓冲。几何实体的纹理坐标
遵循原生形状绘制，不保证是局部 0..1 坐标。

GPU 状态在原生提交后更新。无窗口只检查声明、源码和参数，status 保持 pending、
compiled_revision=0，不把读取成功当成 GLSL 编译。GPU 状态是诊断数据，不应决定
跨平台玩法或存档。

## 验证记录

2026-09-26：Release 图形构建完成，无窗口材质契约测试与示例三帧运行通过。
覆盖类型、参数原子修改、失效/跨房间句柄、容量、调用阶段与源文件 NUL 检查。
GPU 编译、驱动 uniform 检查、保留旧程序、透明像素及 GPU 资源释放循环尚未
实机验收。本轮未启动窗口。同项无窗口 ASan/UBSan 通过，并通过管道调试注入
损坏源码，确认 reload 读取失败不增加版本、修复后恢复更新。关闭模块的 build-dev
构建及能力拒绝检查通过；API 生成文档一致性检查通过。没有运行全套回归或压力测试。

图像/实体绑定补充检查已通过 Release 与无窗口 ASan/UBSan，覆盖别名、覆盖优先级、
原子失败、引用保留、实体槽复用和房间隔离；后处理引用检查通过。扩展示例的三帧
回放可切换默认绑定。上述检查不验证实际着色器像素、透明排序或辅助纹理采样结果。

2026-09-27：完整示例已隐藏运行并查看原生截图，着色程序 ready，图像/精灵、
弹体、粒子、几何及图块着色可见。进一步注入 GLSL 编译错误、活跃 uniform 不匹配，
原生像素证明旧程序保留，修正重载后颜色更新；候选编译失败保留旧房间。
透明排序、辅助纹理采样和长期 GPU 生命周期仍待检查；详见[视觉记录](../verification/systems/advanced-render.md)。

2026-09-27：补全材质字段契约与返回类型，相关元数据/调用数量断言加入
`tests/materials_integration.py`。完整与轻量构建、生成文档检查、项目 SDK 审计及
定向无窗口材质测试通过；项目注解固定为 SDK dev.55。本次没有改动渲染像素，
未将无窗口结果当作 GPU 或平台验收。
