# 图集弹体验证 — 2026-09-24

本记录覆盖图集区域登记、弹体绘制和相关边界，不是完整版验收。

- Windows GNU Release `build/full`、轻量 `build-dev`：content、systems、script、
  contracts、complete_integration、features 六项 CTest 均通过。
- Windows LLVM-MinGW ASan/UBSan 无窗口 `build-llvm-sanitizers`：同六项通过；
  加入无效 PNG 头部用例后重新运行 features，通过。
- 新用例覆盖越界区域、错误资源、显示尺寸上限、未登记模板的整批原子失败、
  init 阶段限制、64 模板容量、密集删除后的样式关联和绘制次序、预留排序缓冲区。
- `examples/projectile_atlas --check-all` 通过。使用 smoke.txt 的相同 8 帧回放，
  原生和无窗口最终 JSON 完全相同：12 活动弹体、0 命中。
- 原生截图 `build/full/projectile-atlas.png` 已实际查看：八个原图区域、三倍
  显示尺寸、纯色方块、透明着色精灵。首次截图发现 texture=0 未恢复白纹理；
  改用显式默认纹理后重新构建、截图并确认修复。
- API 注解、生成参考和项目本地注解已同步；差异空白检查通过。

复现原生检查：

```powershell
.\build\full\shiny.exe examples/projectile_atlas --frames 8 --replay examples/projectile_atlas/smoke.txt --capture D:/GameDevelopment/ShinyCore/build/full/projectile-atlas.png
```

没有执行指定基准硬件的 20,000 弹体性能验收、Linux/macOS 图形检查或长时间
运行；不据此声称这些项目通过。PNG 头部读取也不等价于无窗口完整图片解码。

## 同日补充：项目容量约束

`project.limits.projectiles` 现支持 0..65536、默认 32768；配置为零禁用，
其他值仅指定预算，调用 `configure(capacity?)` 时才分配，可选择较小容量。
`stats()` 分别报告项目预算和实际池容量，超限生成报告 used/requested/capacity。
图集示例明确声明 64 槽预算。

完整、轻量、ASan/UBSan 三种构建均通过 content、systems、script、contracts、
profile、complete_integration、features 七项检查。新增行为检查覆盖禁用、未分配
状态、可选默认容量、65536 上界实际分配、非法配置、重复配置，以及失败批次
不消耗对象或 ID。生成元数据与完整/轻量构建一致，所有示例注解同步。
