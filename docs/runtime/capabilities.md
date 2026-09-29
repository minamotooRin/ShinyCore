# 构建能力与项目需求

`shiny --api` 的 modules 是当前可执行文件的功能清单；project.lua 的 modules
通过同一原生表校验，打包报告保存该构建的清单。未知名称和关闭的能力都拒绝加载。

```lua
return {modules={"advanced_render"}} -- 整组高级渲染内容
-- 仅依赖材质时可以声明 modules={"materials"}
```

| 构建 | graphics | advanced_render 与四个子能力 |
|---|---|---|
| lightweight | true | false |
| full | true | true |
| headless | false | true |

advanced_render 汇总 materials、postprocessing、geometry_shadows、normal_maps，
均跟随 SHINY_ADVANCED_RENDER。无窗口构建保留数据校验与逻辑 API；实际 GPU 绘制
还需要 graphics。原生实现可用、截图验证、平台实机和性能验收是不同事项，后几项
记录在验收文档，不能通过 capabilities 布尔值推断。

2026-09-27 修复早期固定为 false 的汇总标志。修改前真实完整构建拒绝声明
advanced_render 的项目；修改后完整/轻量的需求校验、实际打包及报告一致性检查
通过。全功能无窗口 ASan/UBSan 的需求校验通过，确认 graphics 仍为 false。
API 注解/参考文档生成一致性检查通过，未改变函数契约或 SDK dev.46。

证据：build/capabilities-reviewed/，包含原始失败、三构建 API 和检查清单。
没有渲染路径变动，不重复原生截图、性能测试或样例包移动检查。此前样例 ZIP
仍保留其生成时的可执行文件与报告；不会把旧包描述为已包含这次元数据修复。
