# 法线贴图

启用 SHINY_ADVANCED_RENDER 后运行 `shiny examples/normal_maps`。
两盏彩色光源照亮共享图集，展示图像区域、旋转/翻转精灵和 Tiled GID 变换。
H 切换主灯高度。原生画面与定向法线探针已检查，范围见
[视觉记录](../../docs/verification/systems/advanced-render.md)。

```sh
shiny --headless examples/normal_maps --frames 4 --replay examples/normal_maps/smoke.jsonl
```

PNG 为 make_assets.py 生成的原创几何材质，项目已包含产物，运行不需要 Python。
法线 RGB 对应 +X 向右、+Y 向下、+Z 朝观察者，范围从 [-1,1] 编码到 [0,1]。
接口及限制见 [法线契约](../../docs/presentation/normal-maps.md)。
