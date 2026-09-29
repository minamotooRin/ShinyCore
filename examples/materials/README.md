# 材质示例

使用 `SHINY_ADVANCED_RENDER=ON` 构建后运行 `shiny examples/materials`。
上排依次为原图、显式材质图像、继承图像材质的精灵和强制内置着色器的精灵。
下排为图集弹体、纹理粒子、实体几何材质及有限地图图块。使用 GLSL 330
片元着色器和动态 float/vec3 参数。B 切换图像默认绑定，显式覆盖保持有效。
R 重新读取 `tint.frag`；编译失败时旧程序继续绘制，错误由 `sc.material.info`
及 stderr 提供。初次载入失败没有旧程序，房间准备会失败。

无窗口：`shiny examples/materials --headless --frames 3 --replay examples/materials/smoke.jsonl`。
回放切换两次默认绑定，只验证内容、参数和绘制
命令，GPU 状态保持 pending。另已查看隐藏原生截图和 ready 状态；检查范围见
[视觉记录](../../docs/verification/advanced-render.md)。
PNG 沿用引擎已有原创 keeper 资产，无新增下载或运行时依赖。
