# ShinyCore 0.2 验证记录

以下记录为输入扩展合入前的 0.2 基线验收。输入整合后的完整测试、审查修正与原生验证见 [输入扩展整合审查](input-verification.md)。

日期：2026-09-13 至 2026-09-14。环境：Windows x64、WinLibs GCC 16.1（UCRT/POSIX）、CMake 4.4.3、Ninja、Release。以下为本次本机结果；[0.1 迁移历史](verification-v0.1.md) 独立保留，不能代替本次验收。

## 构建与测试

| 配置 | 结果 |
| --- | --- |
| 图形 ON、网络 OFF，build-dev | 6/6 CTest 组通过 |
| 图形 OFF、网络 ON，build-headless-v02 | 9/9 CTest 组通过，包括真实双进程 UDP |
| 自有 C++ 编译警告 | 两套构建日志未出现 warning/error；第三方 CMake 弃用提示另计 |
| 图形与无窗口状态对照 | Workshop 90 帧 JSON 相同，hash=9bc159ce561aabde |
| ASan/UBSan | 基线验收时 GCC/MinGW 缺少运行库；后续 LLVM-MinGW 本机验证已完成，见[补充记录](sanitizer-verification.md) |
| macOS/Linux 原生运行 | 本轮未执行；CI 保留平台矩阵及 Linux sanitizer 任务，尚未触发远端运行 |

核心测试 40,271 个显式检查。新增 features.py 共 23 个行为测试，覆盖模块缓存/循环、绘制限制、原子状态更新、跨房间数据、磁盘重启恢复、最大状态深度、迁移错误/内存限制、存档写入失败、身体推挤/复合形状/非法补丁、斜坡上行接地、移动平台、穿透平台、传感器、查询与关节、音频句柄/暂停/淡出/持久音乐、UTF-8 测量、Tiled 拒绝不支持配置、Lua 动画及 API 注解同步。

原有 9 项 CLI 集成与 6 项工具测试通过；保留 Lantern 原始素材与 430/480 帧房间往返路线。Box2D 接触位置有小间隙，原落地断言调整为 0.05 像素容差；没有把旧碰撞轨迹视为 0.2 数值契约。

网络开启构建通过 core/script/net/net_lua/features/integration/tools/realtime/network_integration。网络测试实际创建套接字和两个进程，验证握手、消息、资源释放与错误路径。

## 原生画面、资源和重载

- 已查看 Workshop 原生截图：中文标题/提示、图块地面、单向平台、移动平台、角色、箱子和斜坡。截图发现三角形顶点绕序导致剔除，修正后重新截图确认。
- 在测试项目副本中损坏 keeper.png，按 F5 后显示资源错误面板，旧世界与已上传纹理继续显示；恢复文件，再次 F5 成功。
- 重载结束的快照中 visits=2：失败候选未提交状态，成功候选继承旧状态并重新 init。
- 图形预检执行 WAV/OGG 解码，原生运行完成设备初始化和播放调用；音频句柄、暂停与淡出由无窗口测试断言。不以主观听感或样本级同步作为已验证结论。
- 产物位于 artifacts/workshop-v02.png、artifacts/packaged-workshop.png；回放和重载日志在 .cache/。

## 发行包

Windows MinGW Release 静态链接 GCC 运行库并剥离符号。图形程序约 4.31 MB，无窗口网络程序约 2.62 MB；Workshop ZIP 约 1.89 MB。这是当前平台与配置的实测，不是跨平台体积上限，也不是与动态运行库的 0.1 基线直接性能比较。

自定义包带独立 run-game.bat、Lua 模块、两个房间、图集、WAV/OGG、字体、API/Agent 文档及完整许可，不附带 Lantern。已复制到独立目录、从项目外启动，并把 PATH 限制为 Windows 系统目录：--check-all、200 帧回放/磁盘存档和 90 帧原生截图均通过。PE 导入表只列出 Windows/UCRT 系统库，没有 libgcc/libstdc++/libwinpthread DLL 依赖。

## 复现

```sh
cmake -S . -B build-dev -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build-dev --parallel
ctest --test-dir build-dev --output-on-failure
cmake -S . -B build-headless-v02 -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DSHINY_GRAPHICS=OFF -DSHINY_NETWORK=ON
cmake --build build-headless-v02 --parallel
ctest --test-dir build-headless-v02 --output-on-failure
./build-dev/shiny examples/workshop --frames 90 --capture /absolute/path/workshop.png
python tools/package.py build-dev/shiny 新的发行目录 --project examples/workshop
```

Windows 程序后缀为 .exe；使用 Visual Studio 时改用独立构建目录及 --config Release。在具备运行库的 Linux 环境使用 SHINY_SANITIZERS=ON；该选项也覆盖 Lua、Box2D、yyjson 和字体实现。配置会明确拒绝缺少 ASan/UBSan 的工具链。

范围限制：检查点仅恢复显式数据并重建房间；hash 不涵盖任意 Lua 局部变量、关节或求解器缓存。跨架构浮点一致性、复杂文字塑形、主机平台、并发写同一存档槽和 POSIX 掉电持久性均不作保证。图形发行流程不含商店签名或公证。
