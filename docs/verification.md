# 验证与 C11 / C++23 对照

验证日期：2026-09-12。环境：macOS arm64、AppleClang 21、CMake 4.3.1。
这些是本机实测，不代表 Windows/Linux 已运行验收，也不是跨平台体积上限。

## 保存的版本

- `v0.1.0-c11`（`b8016f9`）：从本任务迁移前的源码和变更记录恢复并重新以 C11 编译的完整基线，含可选网络。不是 C++ 兼容模式。
- `release/c11`：同一 C11 源码，额外提交 `6164042` 只纠正两处宿主语言描述并记录修正。原始标签未移动。
- `main`、`feature/cpp23`、`v0.1.0-cpp23`：现代 C++23 交付版本。第三方库保留 C 编译；游戏 API 和场景格式继续使用 0.1.0。

C11 已在仓库旁的 `ShinyCore-C11` 独立 worktree 构建；当前仓库的
`build/shiny` 是 C++23。查看 `git worktree list` 确认实际目录。
生成的程序、ZIP、截图和日志不进入源码 Git，分别保留在 `build*/`、
`dist/` 和 `artifacts/`；可以按下文命令重新生成。

## 自动验证

| 配置 | 结果 |
| --- | --- |
| C11，Release，图形 ON、网络 ON | 8 / 8 CTest 组通过 |
| C++23，Release，图形 ON、网络 OFF | 5 / 5 CTest 组通过 |
| C++23，Release，图形 OFF、网络 ON | 8 / 8 CTest 组通过 |
| C++23，Debug + ASan/UBSan，图形 OFF、网络 ON | 8 / 8 CTest 组通过 |
| C++23，Release，图形 ON、网络 ON | 构建及原生客机与无窗口主机通信通过 |
| C++23，剥离符号后的联机交付包 | 4 / 4 双进程网络集成测试通过 |

现代核心测试执行 40,709 个显式检查，在 Release 的 `NDEBUG` 下仍然有效。
覆盖默认初始化、代际 ID、容量、输入边沿、实心/单向瓦片、高速扫掠、
粒子、相机、随机数和逻辑字段哈希。Lua 测试覆盖配置、补丁原子性、
失效句柄、非法数值、内存与指令预算、绘制保护和 VM 生命周期。

CLI 集成测试 9 项、脚手架/打包测试 5 项，另有真实时间节奏测试。
实际引擎验证了新项目创建、现有目录保护、移至其他目录后的包启动、
JSON 错误和 LANTERN 房间回放，没有使用跳过真实程序的替代结果。
网络测试使用真实 UDP 和两个进程；详细覆盖与限制见 [networking.md](networking.md)。

ShinyCore 自有 C++ 源码在启用 `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`
的最终构建中没有编译警告。第三方 raylib 的 CMake/OpenGL 弃用提示仍存在。
CI 已配置 Linux、Windows、macOS 及网络开关，但尚未在远端运行。

## 行为一致性

`tools/compare_engines.py` 用同一份 LANTERN 项目、seed 42、480 帧
`tour.txt` 分别运行两版，每个程序重复两次。结果：

- 两版各自可重复；22 个基础 Lua 函数相同。
- 全部输出游戏状态字段一致，`changed_state_fields` 为空。
- 最终状态 hash 均为 `65cc73c66b3ad78f`。
- 网络 OFF 的原生构建与网络 ON 的无窗口构建都分别完成了 C11/C++23 对照。
- 对比工具实测会拒绝不同种子造成的状态变化，并对不存在的程序给出明确失败。

现代原生与无窗口模式也运行了同一 430 帧路线，JSON 逐项一致：
场景为记忆室，111 个实体，keeper 位于 `(63, 166)` 且接地，
hash 为 `38e92d73367837a3`。

这验证了本机、该项目和回放覆盖的行为。hash 不包含任意 Lua 局部变量，
并非完整存档；浮点跨架构一致性和实时网络输入的可重复性不在承诺范围。

## 体积

相同工具链和 CPU，Release、静态依赖、图形 ON、网络 OFF：

| 测量对象 | C11 | C++23 | C++23 增量 |
| --- | ---: | ---: | ---: |
| 未剥离符号的程序 | 1,770,056 B | 1,816,120 B | 46,064 B |
| macOS `strip -x` 后程序 | 1,708,216 B | 1,748,856 B | 40,640 B |

后者增加约 2.38%。网络 ON 的 C++23 原生程序为 1,875,736 B，
同样剥离后为 1,805,144 B。打包同时包含游戏、文档和许可，ZIP 大小
还会随文档内容变化，不能当作可执行文件大小。

未测量运行速度，因此不宣称 C++23 比 C11 更快。迁移的直接收益是
固定容量标准容器、独占所有权、RAII 资源释放和 `std::expected` 错误传播。

## 原生与交付验收

- 检查了真实原生截图：洞穴、精灵图集、点光源/遮挡、粒子、HUD，以及收集后进入的记忆室。
- 直接启动并操作 C++23 的 macOS `.app`，验证快速 F1/P 按键；修复了原生事件轮询间短按可能丢失的问题。
- 在交付包副本中临时损坏 PNG 后按 F5，错误面板出现，旧世界和纹理保留；恢复原始字节后再次 F5 成功。交付素材已恢复并核对。
- C++23 联机版原生客机与无窗口主机完成握手、远程输入与主机位置回传，菜单和联网截图已检查。
- 保留 `dist/ShinyCore-C11`、`dist/ShinyCore-Cpp23`、`dist/ShinyCore-Cpp23-Network` 及各自 ZIP；后两者默认双击启动 LANTERN，网络包还包含 DUET。

只验证了音频设备初始化和合成声音调用路径，没有把主观听感当作自动测试结果。
目前是可用的 0.1 桌面内核；编辑器、完整存档、复杂物理、音频文件 API、
多语言字体及主机平台导出等尚未实现，见 [README](../README.md)。

## 重现命令

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSHINY_NETWORK=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DSHINY_GRAPHICS=OFF -DSHINY_NETWORK=ON -DSHINY_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure

# C11 worktree 必须先按 README 独立构建；两个程序使用相同功能开关。
python3 tools/compare_engines.py ../ShinyCore-C11/build/shiny build/shiny

./build/shiny examples/lantern --mute --frames 430 --seed 42 \
  --replay examples/lantern/replays/tour.txt --capture /tmp/shiny-archive.png
```
