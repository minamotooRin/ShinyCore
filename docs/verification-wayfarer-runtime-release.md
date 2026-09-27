# Wayfarer 精简发行构建 · 2026-09-28

为 Wayfarer 单独构建 Release 引擎：图形与流式地图开启，网络、高级渲染和开发工具关闭；
没有修改第三方依赖。`--api` 确认五项开关符合配置。复现命令：

```powershell
cmake -S . -B build/wayfarer-runtime -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSHINY_GRAPHICS=ON -DSHINY_STREAMING=ON -DSHINY_NETWORK=OFF -DSHINY_ADVANCED_RENDER=OFF -DSHINY_DEVTOOLS=OFF
cmake --build build/wayfarer-runtime --target shiny --parallel 2
python tools/package.py build/wayfarer-runtime/shiny.exe build/wayfarer-runtime-release-20260928 --project examples/wayfarer
```

发行文件夹和 ZIP 位于 `build/wayfarer-runtime-release-20260928/` 及同名 `.zip`。
报告中引擎二进制为 6,206,976 字节，资源 17,846,650 字节，标准 Lua 模块
176,145 字节，显式运行库 0 字节；相比本轮全功能引擎包的二进制减少
536,576 字节。资源仍包含完整中文输入字库，未为了缩小体积削减玩家可输入字符。

打包器校验源项目与包内项目。将包复制到仓库外临时目录、限制 PATH 为 Windows
系统目录后，包内引擎执行 3245 帧完整 Wayfarer 场景，五项玩法断言通过。
精简包的隐藏、静音原生对话截图在
`build/wayfarer-runtime-release-visual-20260928/wayfarer-dialogue.png`；
已目视检查文字、按钮、HUD 与地图。截图仅验证该模块组合的画面，不重复打开
已解决的字体问题。未做干净系统安装、人工游玩、真实音频或其他平台验收。
