# Windows 可移动样例包与 Unicode 路径

2026-09-27 的移动检查发现：原构建可从中文目录运行，但绝对回放路径中的字符在
进入 main 的窄字符串参数前已丢失。Crossing 的原始失败日志保留在
build/package-review-20260927/Crossing-native.log，初始包仅作为修复前证据。

Windows 应用现内嵌 UTF-8 进程清单，保留 longPathAware/asInvoker 声明。
清单位于 src/platform，CMake 对 RC 文件记录显式清单依赖；GNU/MinGW 链接片段
仅舍弃工具链的默认 manifest 资源，避免已安装工具链的两个非中性清单冲突；
LLVM-MinGW 不使用该片段，MSVC 关闭自动清单以使用同一资源。没有改动/升级第三方。
UTF-8 进程选项的系统目标为 Windows 10 1903 或更新版本，依据
[Microsoft 的进程 UTF-8 说明](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)。
当前机器验证通过不代表已在所有最低版本或工具链上验证。

必要检查：

- GCC 轻量与完整 Release、LLVM-MinGW 全功能无窗口 ASan/UBSan 的 windows_paths
  通过：中文/Emoji/空格项目与绝对回放路径，存档/设置，以及 snapshot/trace/
  profile/record 输出；只运行这个定向用例。
- 三款重新按清单闭包打包，源项目和副本分别 --check-all，运行库静态审计通过；
  都不需要额外 DLL。SDK 保持 dev.46。关闭的模块在轻量包的能力清单中为 false。
- 解压至源码外 `D:/GameDevelopment/ShinyCore 移动发行 UTF8 20260927/`，核对报告中
  所有文件的大小与 SHA-256，从中性工作目录调用各自 run-game.bat，PATH 只含
  Windows 系统目录。包内校验及带绝对中文回放路径的隐藏静音原生运行通过。
- 实际查看 Crossing 22 帧、Barrage 180 帧、Wayfarer 22 帧原生图，角色/HUD/新旧
  素材可见，没有资源缺失或 HUD 裁切。未重复完整剧情回放、性能或耐久测试。

当前开发包在 `build/sample-packages-portable-20260927/`，每款含文件夹、ZIP、
run-game.bat、README 和 package-report.json；Windows 版本要求写入包内 README。
以下均为 MiB，按包内报告统计，资源包含随包回放与数据：

| 样例 | 引擎配置 | 引擎 | 资源 | Lua 标准库 | ZIP |
|---|---|---:|---:|---:|---:|
| Crossing | 轻量 | 5.61 | 2.36 | 0.09 | 4.78 |
| Barrage | 轻量 | 5.61 | 1.75 | 0.09 | 4.08 |
| Wayfarer | 完整 | 6.36 | 16.95 | 0.14 | 16.99 |

三包独立运行库和独立调试符号均为 0；游戏代码、文档及许可另外列在各自报告中。
证据 `build/package-review-20260927/fixed/manifest.json` 记录 ZIP 哈希、构建能力、
文件数、回放帧和截图检查；初次测试脚本 cmd 引号错误已记录，修正的是检查脚本。
`sample-packages-20260927` 是修复前包；`sample-packages-utf8-20260927` 是未补
Windows 版本说明的中间包，两处均有 STATUS.txt 指向当前包，不当作最终证据。

这仍是开发包，不是完整产品发行验收。尚未进行 MSVC、Linux/macOS、最低 Windows
版本、干净系统、实体输入/音频、人工游玩时长或最终品质验收。longPathAware 声明
不等于本次验收了超长路径。当前用户要求下不开展性能优化或压力/耐久检查。
