# 新项目脚手架检查 · 2026-09-27

`tools/new_game.py` 现在生成使用 `sc.input` 的普通 Lua 控制器、版本 3 JSONL
输入回放，并在项目内用 `shiny --check-all .` 检查全部房间。README、AGENTS.md
和 LuaLS 配置只引用相对项目文件；引擎可位于 PATH 中任意位置，创建命令仍输出
当前检出可用的完整可执行路径供立即运行。未使用的旧动画示例不再复制。

必要验证：`tests/test_tools.py` 8 项通过；生成项目的 `--check-all` 与第 60 帧
跳跃回放通过。项目移动到 `build/scaffold-v3-moved/` 后，相同检查和回放结果
仍通过；用轻量引擎打包、从中性工作目录运行包内项目亦通过。隐藏静音原生截图
`build/scaffold-v3-native-review/jump.png` 已目视检查：角色、平台、说明文字可见，
无裁切或缺图。此项不代表实体手柄、跨平台或最终发行验收。
