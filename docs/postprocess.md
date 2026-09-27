# 四 pass 后处理链

`SHINY_ADVANCED_RENDER=ON` 提供 `postprocessing` 能力；默认关闭。
使用带 `postprocess=true` 的材质组成一条最多四 pass 的顺序链：

```lua
local grade=sc.material.create{shader="grade",postprocess=true,
    uniforms={gain={type="vec3",value={1.08,.96,.88}},gamma={type="float",value=.9}}}
sc.material.postprocess({grade}) -- load/init/update
local pipeline=sc.material.pipeline() -- 可在 draw/ui_update 查询
sc.material.postprocess({}) -- 禁用；下一次原生提交释放目标
sc.material.destroy(grade)
```

传入数组可重复引用同一材质。非密集数组、失效句柄、图像材质或超过四项均报错，
失败不改变原链、预算和版本。材质仍被链引用时不能销毁。图像绘制拒绝后处理材质；
其参数仍用普通 material.set 更新。切房间重建链，不保存 GPU 资源或运行期句柄。

第二参数为颜色目标预算字节数，省略保留当前预算，初始 64 MiB，上限 1 GiB。
零预算只允许空链。目标使用场景 width/height 的逻辑分辨率，不使用窗口输出尺寸；
每目标为一张 RGBA8 纹理，无深度或 mipmap。一个 pass 需要一个目标，二到四个 pass
交替使用两个目标；所需颜色字节数为 width×height×4×目标数。设置时即检查预算。
候选房间可以与活动房间暂时各持有一套目标，提交后旧资源释放。

pipeline() 返回 passes、revision、limit=4、budget_bytes、required_color_bytes、
allocated_color_bytes、target_count、status、error。颜色字节数是已分配纹理的逻辑
存储量，不是驱动 VRAM 实测值，也不包含引擎基础场景/光照/最终目标。无窗口仅
校验所需预算，实际分配为零，非空链状态 pending；原生成功绘制后为 ready。

每 pass 的 texture0 是上一 pass 的输出。后处理专用内置 uniform（均可省略）：

| 名称 | 类型 | 含义 |
| --- | --- | --- |
| sc_scene | sampler2D | 此链之前、尚未包含屏幕 UI 的原始世界画面 |
| sc_resolution | vec2 | 原始画面的逻辑宽高，单位像素 |

内置名不得放入 uniforms 表；原生链接时检查类型。每个后处理材质最多绑定三个
额外 image 采样器，另一个槽留给 sc_scene；图像材质仍可绑定四个。读写目标始终
分离，提交使用覆盖复制，避免再次叠乘已合成像素的 alpha。目标采样为 point/clamp；
滤镜自行实现卷积。链作用于地图、世界图像、实体、光照、弹体和粒子之后；屏幕 UI、
消息、调试边框与检查面板在其后绘制。

候选房间编译和目标准备失败保留活动房间。运行期 GPU 程序或目标不可用时跳过整条
链并报告 failed，显示完整原画面，不应用半条链；逻辑配置仍保留。已有材质重载
失败继续使用上一份有效程序。目标分配失败每个链版本只尝试一次，重新设置链可
重试；shader 修复后 reload 不需要重建已有目标。缩短或清空链释放多余目标。

可复制的 [Bloom、调色、扭曲示例](../examples/postprocess/README.md) 提供可读 GLSL
及全部模式的十帧回放。当前目标为 RGBA8 LDR，不提供 HDR 或通用渲染图。

## 验证

2026-09-26：Release 图形构建完成，无窗口测试覆盖 pass 上限、顺序、预算、原子
拒绝、引用生命周期、调用阶段及示例模式切换。原生像素、驱动编译与目标创建/释放
循环尚未验收；本轮未开窗口。无窗口不能证明 Bloom、调色或扭曲的实际显示效果。
同项无窗口 ASan/UBSan 与材质边界回归通过；关闭高级模块的构建及能力拒绝检查
通过。API 参考和项目内注解已重新生成并检查一致性。未执行全套回归或压力测试。

2026-09-27：修正示例的图集单帧选取，实际查看关闭与四 pass Bloom＋调色。
检查世界像素变化、UI 字形不变和目标数量；单独调色、扭曲及资源循环仍未覆盖。
详见[视觉记录](verification-advanced-render.md)。
