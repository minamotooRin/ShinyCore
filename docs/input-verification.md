# 输入扩展整合审查 — 2026-09-14

本次以已完成的 0.2 基线 `bcbaaa8` 为基础，整合输入扩展 `2694abc`，保留新版引擎、文档和测试。早期输入分支的 `dd16684` 只是开发中快照，没有将它覆盖回主分支。整合与验证在独立的 `ShinyCore-input-review` 工作区进行。

## 审查修正

- 修复同一采样中键盘与手柄交接同一个动作时的误触发。动作连续按住时不产生额外边沿；明确观测到同一控件释放后重按，仍保留两条边沿。新增 C++ 和 Lua 回放回归测试，先复现失败再验证修复。
- 修复原生键盘在两次采样之间快速释放再按下丢失边沿的问题。raylib/GLFW 的按下队列只含新按下，不含系统重复；结合前次采样可恢复被最终按住状态隐藏的释放。Windows 测试验证连续五次快速重按及系统重复抑制。
- 极小负轴值转成 float 后统一为正零，保证等价零值的状态哈希一致。断开手柄的非零轴值在转成 float 前即拒绝，避免下溢绕过校验。
- 文档冲突保留完整最新 0.2 内容，再加入输入接口说明。补充 AGENTS 与开发指南中的 `--debug-keys` 前提。

## 验证结果

环境：Windows x64，WinLibs GCC/MinGW 16.1.0，Release，独立 CMake/Ninja 构建目录。

| 检查 | 结果 |
| --- | --- |
| 图形 ON、网络 OFF：build-review-graphics | 完整 CTest 8/8 通过 |
| 图形 OFF、网络 ON：build-review-headless | 完整 CTest 11/11 通过，包括真实双进程 UDP |
| 输入集成测试 | 10 项通过，覆盖 105 个键名、17 个按钮、6 个轴、非法输入、边沿、场景切换、设备交接和零值哈希 |
| 0.2 features 测试 | 全部通过；早期快照中的 sc.measure 文档缺失已由最新基线修复 |
| Windows 原生窗口消息测试 | 默认键位、快速重按、失焦释放、F12 不截图、调试重载/暂停/单步/退出、回放隔离全部通过 |
| Input Lab 30 帧 | 图形/无窗口完整 JSON 相同，hash=e5b7e135fc23094c |
| Workshop 90 帧 | 图形/无窗口完整 JSON 相同，hash=e61f9f070a39754e |
| Lantern 430 帧旧回放 | 到达 rooms/archive.lua |
| 原生截图 | 已检查 Input Lab 的键盘/按钮/轴显示，以及 Workshop 中文 UI、角色、平台和斜坡 |
| 打包和脚手架 | 两套配置的工具测试均通过，包含新增输入说明和示例 |

原生测试仅向自身创建的子进程窗口发送消息，不注入全局键盘事件，不操作其他应用窗口。它验证原生回调、采样和宿主路径，不代替物理键盘或手柄测试。未检测到可用手柄，实机按钮、摇杆、扳机和拔插仍未验收。

本次输入审查时尚未执行 ASan/UBSan：当时 GCC/MinGW 缺少运行库，本机没有 WSL。后续已使用 LLVM-MinGW 完成本机检测，见[补充记录](sanitizer-verification.md)；既有 Linux sanitizer CI 保留并覆盖新增输入测试。未宣称跨平台或跨架构浮点一致性。输入审查的依赖使用本机已获取的固定版本源码的独立副本，通过 FETCHCONTENT_SOURCE_DIR_* 配置；没有修改依赖源码或复用其他任务的构建输出。

验证日志在各构建目录的 Testing/Temporary。截图与 JSON 证据保存在审查工作区的 artifacts/input-review/：input.png、workshop.png、input.json、workshop.json、lantern-430.json 和 native-windows.json。

## 复现

```sh
cmake -S . -B build-review-graphics -DSHINY_GRAPHICS=ON -DSHINY_NETWORK=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-review-graphics --parallel 2
ctest --test-dir build-review-graphics --output-on-failure
python tests/native_input_windows.py build-review-graphics/shiny.exe

cmake -S . -B build-review-headless -DSHINY_GRAPHICS=OFF -DSHINY_NETWORK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-review-headless --parallel 2
ctest --test-dir build-review-headless --output-on-failure
build-review-headless/shiny.exe --headless examples/lantern --frames 430 --replay examples/lantern/replays/tour.txt
```

Windows 执行 CTest 前需将所用 WinLibs 工具链的 bin 目录加入 PATH，供测试程序加载运行库。Windows 示例命令使用 .exe；其他平台按实际产物路径调整。更换编译器时创建独立构建目录。
