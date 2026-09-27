# IME 分句定向检查（2026-09-27）

Windows 组合文本现采集分句边界及六种转换属性，经固定容量转换器进入原生快照、
Lua、回放和诊断哈希。最多 128 段；超限保留文本、光标和首个目标范围，清空分段并
显式置位 truncated。UI 按字素显示属性，提交前不修改正文或撤销栈。不增加依赖。

已执行的必要检查：

- Release `test_ime_text`：六种属性、同属性不同分句、无效分句回退、代理对边界、
  非法 UTF-16 和容量溢出通过。
- `ime_segments_integration` 三项：分段改变引发布局缓存更新、组合字素整段高亮、
  超限回退、提交与撤销、trace 输入回放往返、非法回放拒绝、属性改变影响诊断哈希。
- `input_contracts`：原生字段/返回值元数据与快照一致；无窗口 ASan/UBSan 同时执行
  上述原生转换和三项集成检查，均通过，启用 halt_on_error。
- 原有光标/目标位置、预览提交/取消/撤销、多行和焦点隔离的三个定向用例通过。
- API 注解及参考文档生成一致性通过；14 份示例注解同步，8 个 SDK 清单审计通过。
  更新项目使用 SDK dev.29；旧 snapshot 示例模块版本未改变。

```powershell
ctest --test-dir build/full -R '^(ime_text|input_contracts|ime_segments_integration)$' --output-on-failure
python tests/native_ime_segments.py build/full/shiny.exe --output build/ime-segments-reviewed
```

原生单帧截图已实际查看：待转换文本有点状下划线，两个非连续目标片段均高亮，
已转换片段有细下划线，错误片段为红色，固定片段为弱化文字且无下划线；光标可见，
正文保持 Unchanged。使用隐藏、不聚焦、静音、隔离存档的实际原生渲染。命令、
回放、快照、日志、截图及捕获时的引擎哈希保存在 `build/ime-segments-reviewed/`。
捕获后仅补入诊断哈希字段和 API 描述，并将同色错误默认值集中到主题；未改变图像行为。

本项不涉及已解决的中文字体清晰度，也未重复旧图检查。未执行性能/压力测试、
全套回归或真实输入法操作。Windows 候选窗口、不同输入法与 DPI 的实机行为，
Linux/macOS 原生组合文本仍未验收；记录输入的截图不替代这些项目。
