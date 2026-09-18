# 完整版开发验证记录

日期：2026-09-18。此记录仅证明当前开发树中实际执行的检查，**不代表完整版完成**。
旧版报告不代替本次验收，工作包状态见 [implementation-status.md](implementation-status.md)。

## 本机自动检查

环境为 Windows x64，GNU 16.1 Release 与 LLVM-MinGW 22.1.8 ASan/UBSan。

| 配置或检查 | 实际结果 |
| --- | --- |
| 默认图形构建，网络/流式关闭 | 13/13 CTest 组通过 |
| 无窗口，网络和流式开启 | 16/16 CTest 组通过 |
| LLVM 无窗口 ASan/UBSan，网络和流式开启 | 16/16 CTest 组通过，48.68 秒；后续菜单/工具改动对应的 4 组再次通过 |
| LLVM 原生图形 ASan/UBSan | 设置菜单回放、分辨率切换、文件失败回退与两次无边框往返通过 |
| 默认字体度量修正 | 内容、功能、菜单与小样集成的 4 组回归通过；原生截图人工检查 |
| 新建 lightweight 预设 | 配置/构建及 13/13 CTest 组通过，测试 12.07 秒 |
| 新建 full 预设 | 配置/构建及 16/16 CTest 组通过，测试 43.11 秒；高级渲染和调试器仍不可用 |
| 新建 headless 预设 | 配置/构建及 16/16 CTest 组通过，测试 38.12 秒 |

ASan/UBSan 环境为 `ASAN_OPTIONS=halt_on_error=1` 和
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`，LLVM-MinGW 的 `bin`
位于 PATH。编译仍存在若干浮点转布尔、符号转换警告，未声称无警告。
详细本地输出位于 `.cache/settings-sanitizer-tests.log` 和
`.cache/settings-graphics-sanitizer-build.log`。

## 设置与 UI

- 原生菜单使用规范化键盘回放，验证修改草稿不会立即应用、返回放弃草稿、
  应用分辨率/缩放/VSync/音量、保存动作重绑定。截图来自真实窗口帧缓冲，
  尺寸为 1280×720，路径 `.cache/settings-native.png`。
- `tests/native_settings.py` 注入原子写入临时路径冲突：先实际改变窗口，再保存失败；
  最终截图恢复为 960×540，磁盘原记录仍为 960。不是仅检查内存中的偏好字段。
- 同一检查执行两次窗口/无边框往返，最终 1000×700 窗口中的 384×216 画面
  精确缩放为 768×432，边界位于 (116,134)。逐像素验证四周黑边和内容边界。
- 无窗口行为测试另外验证设置损坏回退、跨房间保留、候选初始化不能写设置、
  隐藏和禁用控件不会收到 Enter，以及默认字体度量与自动换行。
- 这些检查使用回放和自动窗口操作，不等于真实中文输入法、实体手柄或听音验收。

复现命令（原生项需要可用桌面）：

```sh
python tests/settings_integration.py build-dev/shiny.exe .
python tests/settings_integration.py build-dev/shiny.exe . --native --capture settings.png
python tests/native_settings.py build-dev/shiny.exe native-settings-output
python tests/test_tools.py build-dev/shiny.exe .
```

## Windows 开发包移动验证

`dist/development-2026-09-18/` 包含三款**原型**的轻量配置包，以及 Crossing 的
完整配置对照包。每个 ZIP 均解压到 `.cache/package-relocation-2026-09-18/`，
从该独立目录使用包内引擎、脚本、标准库和回放运行 180 帧，全部通过。
两个 Crossing 包还执行原生图形回放，逐帧诊断与各自的无窗口运行比较一致。
该比较覆盖 trace 的显式字段；不代表 VM 或求解器全部未来状态一致。

| 实测内容 | 字节 |
| --- | ---: |
| lightweight 原生引擎 | 4,897,792 |
| full 原生引擎（当前已实现模块） | 5,029,376 |
| 包内 Lua 标准库 | 31,804 |
| Crossing lightweight ZIP | 2,083,443 |
| Crossing full ZIP | 2,145,002 |

分类明细和文件哈希位于各包的 `package-report.json`；移动运行结果汇总位于
`dist/development-2026-09-18/verification.json`。当前标准库按整套复制，项目资源
尚未做依赖裁剪；没有把这些包当作完成版游戏发行或体积指标最终验收。
构建复用已有校验过的依赖源码目录，未修改下载缓存中的依赖文件。
已检查实际 Ninja 构建图：lightweight 不包含 ENet 或流式加载编译目标，headless
不包含 raylib；没有把关闭模块仍被链接的构建当成轻量配置。

## 仍未验收

三款小样仍是功能原型，未达到各 5–10 分钟完整游戏流程。高级渲染、交互调试器、
完整流式地图提交/边界/持久状态、四人重连协议与弱网验收等仍在开发范围内。
五项指定压力负载、p99 CPU/GPU 门槛、128 MiB 缓存长期稳定性、两小时循环游玩、
实体双手柄/IME/DPI/听音，以及本次 Linux/macOS 真正执行结果均未验收。
CI 已增加相关构建和原生菜单测试命令，但尚未触发并取得远端结果。

## 提交前复核（2026-09-19）

复核修正了原生寻路接口的地图尺寸乘法溢出风险，以及零图块尺寸和非有限流场
坐标的转换风险，并增加对应回归测试。流式地图父目录统一显式使用 UTF-8，
其缓存、卸载与错误恢复测试改在中文目录中执行。同步修正了架构文档中的存档
格式和输入回放描述。

修正后重新构建并验证 lightweight 13/13、full 16/16、headless 16/16、
LLVM ASan/UBSan 16/16；完整配置再次通过原生菜单回放、无边框往返、
文件失败窗口回退和黑边像素检查。日志位于 `.cache/precommit-*-tests.log`，
原生输出位于 `.cache/precommit-native/` 与 `.cache/precommit-settings.png`。
这些本机结果不代替远端 CI 或未执行的平台、硬件和性能验收。
