# 有界后处理示例

以 `SHINY_ADVANCED_RENDER=ON` 构建，再运行 `shiny examples/postprocess`。
逻辑画布为 480×270，窗口初始尺寸相同。PNG 沿用原有原创 keeper 资产；所有
片元源码保存在 shaders/，没有外部下载、脚本库或编辑器依赖。

- B：三 pass Bloom（亮部提取/横向模糊、纵向模糊、合成原场景）。
- G：单 pass 调色；W：单 pass 正弦扭曲。
- C：Bloom＋调色，共四 pass；O：关闭并释放后处理目标。

后处理仅作用于世界；标题和操作说明由 screen=true 命令绘制，保持清晰。
这里是 RGBA8 的 LDR 效果示例，不宣称 HDR 或色彩管理。

```sh
shiny --headless examples/postprocess --frames 10 --replay examples/postprocess/smoke.jsonl
```

该回放依次切换全部模式。无窗口只验证逻辑、资源和参数，GPU 状态保持 pending，
目标分配量为零。另已原生检查关闭与 Bloom＋调色，角色使用图集单帧等比绘制。
其他模式及 GPU 生命周期仍待检查，见[视觉记录](../../docs/verification/systems/advanced-render.md)。
