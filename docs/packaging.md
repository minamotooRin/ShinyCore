# 发行依赖

```powershell
python tools/package.py build/full/shiny.exe build/packages/Wayfarer --project examples/wayfarer
# Windows：直接双击游戏专用 EXE 启动，不生成 run-game.bat：
python tools/package.py build/full/shiny.exe build/packages/WayfarerExe --project examples/wayfarer --launcher-exe Wayfarer
# 使用动态链接工具链时，按需重复指定运行库及其再分发许可：
python tools/package.py build/shiny.exe build/packages/Game --project my-game --runtime path/runtime.dll path/LICENSE
```

`--launcher-exe` 仅用于 Windows 自定义游戏包，可传文件名主体或 `.exe` 文件名，
也支持中文与空格。打包器将已构建的引擎复制为该名称；启动时自动读取同目录的
`game/project.lua` 与 `game/main.lua`，因此从任意工作目录双击或移动整个包后
仍能运行。显式传入项目路径时，以命令行路径为准；`--api` 等开发命令仍可使用。
这是游戏专用启动程序，不是把脚本、资源和运行库压进单个 EXE：分发时须保留
整个输出目录或 ZIP。未使用此选项的包维持 `shiny.exe` 与批处理启动器。

输出目录必须不存在。游戏先通过 `--check-all` 校验所需模块和内容； `advanced_render`
表示高级渲染整组已编译，细分需求可使用 materials/postprocessing/geometry_shadows/normal_maps；
无窗口构建可检查这些内容，实际 GPU 运行还需要 graphics。构建能力不代表平台验收状态。打包器
复制游戏、许可、文档及启动器，生成 ZIP 和 `package-report.json`。
报告列出文件 SHA-256、引擎能力、依赖图和各类字节数。运行时不需要 Python。
项目有 `package.json` 时只复制其依赖闭包，未列出的文件不进入游戏包；没有清单
时保留保守复制。新建项目和三款完整样例已提供清单。源项目与包内项目分别通过
`--check-all`，后者失败时移除本次未完成的输出，不留下看似成功的包。

当前 Windows 开发包为 `build/sample-packages-scenarios-20260927/`：Crossing/Barrage
使用轻量引擎，Wayfarer 使用完整引擎。本轮重新打包纳入三份
完整流程场景清单，源项目和包内 `--check-all`、运行库审计通过；从中性工作目录
运行包内 180 帧场景断言，三款通过。报告与 ZIP 均含完整流程清单。旧版移动目录的
Unicode/原生截图证据见[可移动包记录与大小](verification-portable-packages.md)，
不自动证明本轮包已在源码外移动或执行完整流程；这些仍待验收。
这不是全平台/干净系统验收。

Wayfarer 道路状态提交修正后的单独新包在
`build/sample-packages-route-20260927/wayfarer/`，ZIP 同目录；它替代上段三包中
Wayfarer 的游戏脚本。包内 `--check-all` 与中性工作目录的 180 帧场景检查通过，
未重复完整任务、移动安装或原生截图。Crossing/Barrage 包保持上段版本。

Wayfarer 结局存档提交修正后的精简运行时包现位于
`build/wayfarer-runtime-release-20260928/`，仅启用图形和流式地图。
包已复制到仓库外执行完整场景，原生对话画面已检查；详见
[精简发行构建记录](verification-wayfarer-runtime-release.md)。旧 Wayfarer 包不含本次修正。

当前 Wayfarer 包更新为 `build/wayfarer-navigation-release-20260928/` 和同名 ZIP，
包含世界坐标导航入口及项目本地 dev.74 SDK；复制到仓库外的完整场景通过。
精简引擎开关与二进制保持上段配置，见[流式导航验证](verification-stream-world-navigation.md)。

## 内容清单

```json
{
  "format": 1,
  "scripts": ["main.lua", "rooms/second.lua"],
  "files": ["assets/player.png", "assets/LICENSE.txt", "smoke.jsonl", "docs/api.lua"],
  "stream_maps": ["maps/world/index.json"],
  "dynamic_modules": {"main.lua": ["enemies.scout", "enemies.guardian"]}
}
```

- `project.lua` 和清单自身自动保留。`scripts` 列出入口及所有房间，包括动态切换的
  房间。工具按运行时路径规则递归收集直接字面量 `require`，包括 update 分支中的调用；
  跳过注释和字符串内容，不执行 Lua。`shiny.*` 从项目自己的 `lib/shiny/` 复制。
- `files` 是精确的项目相对文件路径，包含运行资源、许可、回放和开发文档。它们作为
  数据复制，不扫描其中的 Lua 注解。动态路径、原始 Tiled 外部文件、着色器及其他
  非流式数据依赖也须在这里列全；不根据观察到的一次游玩推断哪些资源永远不用。
- `stream_maps` 接受工具生成的 format 3、32×32 块索引。收集全部索引块及地图图像，
  校验块字节数，并收集地图、组、图层、图集、图块及块对象中生效的 `file` 属性依赖；
  离线源地图、TSX 图集和构建缓存无需随运行包携带。
- 计算名称、别名调用或转义字面量 `require` 需在 `dynamic_modules` 按所属脚本列出
  全部候选模块。无法静态判断的 require 且没有声明时失败。其他间接查表方式也须
  显式声明，工具不是完整 Lua 程序分析器。这里及 `scripts` 中的模块仍会递归收集依赖。

所有列出的文件必须存在且位于项目内；不支持通配符或外部绝对路径。
报告的 `project_content` 保留每个文件的纳入原因和省略文件列表，便于 Agent 审查。
项目作者负责清单完整性；包内校验不代替后续房间、动态玩法分支的回放检查。

## 本地 Lua SDK

携带 `lib/shiny/*.lua` 的项目必须有 `shiny-sdk.json`，由脚手架生成。当前七个
标准模块示例已补齐记录和本地 MIT 许可。清单包含 SDK 版本、精确要求的
`--api` 引擎版本及 contract_version，以及模块、API 注解和许可的文本 SHA-256。
文本换行统一为 LF 后计算，避免跨平台检出造成误报；发行报告仍另外记录原始字节哈希。

打包分别检查源项目与副本：版本不符、文件被未记录地修改、新增未记录模块或缺少
SDK 许可均失败。内容裁剪自动保留 SDK 清单和许可；清单可作为全部模块的目录，
允许只携带其中一部分，`lua_sdk.files` 只报告实际随包的文件。没有使用标准模块的
游戏无需添加 SDK 清单。此检查属于开发工具，不增加游戏运行时依赖。

模块仍是可读、可修改的普通 Lua。修改标准模块或 API 注解后，审查兼容性，再显式
记录新的本地版本；工具只记录本地内容，不下载或覆盖模块：

```powershell
python tools/sdk.py my-game --version my-game-sdk.2 --engine-version 1.0.0-dev --contract-version 1
```

同一清单的文件或版本要求发生变化时，不允许重用旧 SDK 版本名。版本标识和哈希
用于一致性审计，不是签名或自动兼容性证明；游戏回放仍须验证其实际调用行为。
`lua/shiny/version.json` 是脚手架复制标准库时采用的版本要求。

## 原生运行库

`tools/runtime_deps.py` 不加载库，检查 PE 普通/延迟导入、ELF NEEDED 和
Mach-O load commands。所有显式库均作为根检查间接依赖，包括循环引用；
缺少库、许可、架构不匹配或重复文件名均失败。运行库放在引擎旁，许可放入
`licenses/runtime/`。不从开发机 PATH 自动收集 DLL，也无法发现任意代码中
动态计算的 `LoadLibrary` / `dlopen` 名称；此类库必须显式列出。

- Windows 使用内置系统 DLL/API Set 清单；VC++、MinGW 动态运行库不视为系统库。
  MSVC 默认使用统一静态 CRT，可通过 CMake 配置覆盖；MinGW Release 已使用静态链接。
- Linux 需要 `readelf`。系统依赖清单包含 libc、编译器运行库及常见图形/音频库；
  它们是目标机器的安装前提，审计不保证任意发行版或 glibc/GLIBCXX 版本兼容。
  携带额外库或清理已有 RPATH 时需要 `patchelf`，将副本搜索路径设为 `$ORIGIN`。
- macOS 需要 `otool` 和 `lipo`。系统路径之外的依赖须显式携带；
  `install_name_tool` 将副本依赖改为 `@loader_path` 并清除旧搜索路径，改动后进行 ad-hoc 签名。
  按架构记录 LC_RPATH；通用二进制的路径不同时，分别改写临时切片后重新合并。
  原始输入不变，合并失败保留尚未替换的副本，临时文件自动释放。
  不提供 Developer ID 签名、公证或完整应用签名保证。

复制与改写后重新读取所有运行库，拒绝残留的开发机搜索路径、未改写依赖和缺失的
包内搜索路径；macOS 逐架构检查，显式动态根也必须可在包内查找。
`native_dependencies.relocation_verified` 仅表示静态依赖与路径检查通过，
不表示干净系统、任意动态库加载、签名公证或真实平台运行已验证。
打包先静态审计源文件，再复制运行库并修正包内搜索路径；读取 `--api` 与检查
源项目、包内项目都使用已备齐运行库的包内引擎。因此显式 `--runtime` 可以来自
不在开发机 PATH 中的目录，打包失败仍删除未完成输出。
本机 Windows 定向测试编译了位于源引擎目录外的 DLL 和导入它的可执行文件，
用 `--runtime` 打包自定义项目后，包内 API、项目检查、运行库清单及体积报告均通过；
`tests/test_tools.py` 共 9 项通过。该证据不替代 Linux/macOS 实机搬移测试。
缺少改写工具、Mach-O load commands 空间不足或重新审计失败时，打包失败并撤销输出。
改写语义依据 Apple 的 [install_name_tool](https://github.com/apple-oss-distributions/cctools/blob/main/misc/install_name_tool.c)
和 [lipo](https://github.com/apple-oss-distributions/cctools/blob/main/man/lipo.1)；不修改源二进制来补足空间。

2026-09-27：11 项依赖工具检查和一次 Windows 实际脚手架/打包/搬移检查通过。
新增覆盖残留路径、未改写依赖、显式动态根、通用二进制架构差异与合并失败清理。
ELF/Mach-O 仍为模拟工具输出测试，平台实机验证未执行。

## 本地证据（2026-09-26）

通过 7 项依赖解析/图检查及 2 项既有打包/移动检查。ELF/Mach-O 工具调用
仅有模拟输出测试，本机未进行 Linux/macOS 原生发行验证。MSVC 静态 CRT 配置
尚未在该工具链验证。Windows GCC Release 的 20 项导入均为系统依赖，无额外 DLL。

已生成 Crossing、Wayfarer、Barrage 三个 Windows 完整模块测试包，位于
`build/sample-packages-20260926/`，各含 ZIP、启动器和大小/依赖报告。
三包分别解压到源码目录外的临时目录，PATH 仅保留 Windows 系统目录，
`--check-all` 与有界隐藏窗口回放均通过。实际查看截图：渡台第 435 帧、
中文日志第 36 帧、第一波第 180 帧。PNG、日志及记录在
`build/package-visuals-20260926/`，无缺失资源或文字裁切。

随后补齐显式内容闭包并通过 5 项依赖检查，以及 3 项实际打包/脚手架检查，
包括发现包内缺失声明房间后撤销输出、保留 update 才加载的模块。
中文 UI 修正后的最新包在 `build/sample-packages-native-ui-20260926/`。
三款均包含清单和 ZIP，在源码目录外解压、仅系统 PATH 下运行隐藏短回放成功，
实际查看渡台、中文对话和第一波截图；记录在 `build/package-visuals-native-ui-20260926/`。
Wayfarer 的中文已清晰，旧包的中文质量判断已撤销，见 [视觉记录](verification-sample-visuals.md)。
这次只裁剪未用 Lua 模块及 Wayfarer 的离线地图源/构建清单，不删减运行负载。

截图工具也可检查移动后的项目，并记录引擎 SHA-256：

```powershell
python tools/capture_samples.py path/to/wayfarer/shiny.exe --project path/to/wayfarer/game --case wayfarer-dialogue --output build/moved-dialogue
```

`--project` 要求显式选择同一个样例的 case；默认仍按源码目录中的样例运行。
每次输出目录必须不存在，保存 PNG、快照和日志。截图生成后的 review 默认 pending。

SDK 变更另通过 4 项版本/修改/换行/裁剪检查和 3 项既有脚手架/打包检查。
实际打包 CLI 拒绝版本不符且没有创建输出；Wayfarer 的源码和裁剪副本校验通过，
样本目录为 `build/wayfarer-sdk-package-20260926/`。此次未修改渲染，未重复图形测试。

这些是开发测试包，不是完整版验收发行。未进行干净系统安装验证、实体音频/
输入设备检查或最终美术验收；其他平台和默认轻量包仍待补齐。

对象模板的最终 file 属性、图集/图块/图层的 file 属性及图片集合图块图片现在随 stream_maps 闭包携带，
生效路径必须在项目内且存在；源模板和源地图不自动进入运行包。两帧原生运行
已验证仅复制闭包后的模板对象数据，详见[模板依赖](tiled-templates.md)。
