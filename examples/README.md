# 游戏与功能示例

三款较完整的样例是 [`crossing`](crossing/)（动作解谜）、[`wayfarer`](wayfarer/)（中文 RPG）和 [`barrage`](barrage/)（弹幕生存）。它们各有项目配置、资源、回放与独立 README。四人联机玩法见 [`constellation`](constellation/)；较短的入门项目见 [`workshop`](workshop/) 和 [`lantern`](lantern/)。

其他目录是单项功能演示，如 `ui_scroll`、`text_input`、`materials`、`normal_maps`、`particles`、`tsx_import`。从具体示例的 README 入手；游戏项目自己的 `lib/shiny/` 和 `docs/api.lua` 供离线开发，不与引擎源码共享运行时路径。

```powershell
.\build\lightweight\shiny.exe examples/crossing --check-all
.\build\lightweight\shiny.exe examples/crossing
```

原生画面改动须查看实际截图；无窗口检查只验证规则与数据。测试及截图入口见 [`../tools/README.md`](../tools/README.md)。
