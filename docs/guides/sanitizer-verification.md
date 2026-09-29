# Windows ASan/UBSan 补充验收

日期：2026-09-14。源码基础为输入整合后的 `6cb4b3c`，加上下述修正。
环境：Windows x64、LLVM-MinGW 22.1.8（20260616 UCRT 发行版）、Clang/libc++、CMake 4.4.3、Ninja。
独立构建目录 `build-llvm-sanitizers`，RelWithDebInfo、图形 OFF、网络 ON、ASan/UBSan ON。

## 结果与修正

- ASan/UBSan 的编译、链接及真实运行均通过。独立故障探针分别触发堆越界和有符号整数溢出，得到源文件行号和调用栈，退出码均为 1；这些故意失败的探针不属于引擎测试。
- 首次完整测试为 7/11 通过：核心测试栈溢出；ENet 的空指针成员取址触发 UBSan，导致三个网络测试组失败。
- 核心测试中的大型 `ScWorld` 改用独占堆分配，保留原检查内容及默认初始化测试，避免优化内联和 ASan 栈开销耗尽 Windows 默认栈。
- ENet 1.3.18 的三个字段偏移表达式改为标准 `offsetof`。CMake 生成独立编译文件，保留下载源码不变；网络协议和检测范围不变，未添加 sanitizer 忽略项。
- 修复后 **11/11 CTest 组全部通过，37.59 秒**，包括输入、核心、Lua、23 项功能测试、工具、回放和真实双进程 UDP。开启遇错退出，测试日志没有 ASan/UBSan 运行时错误。
- GCC 16.1 Release 无窗口/网络 ON 回归 **11/11 通过，32.14 秒**，确认修正兼容原工具链；日志为 `.cache/gcc-sanitizer-fix-build.log` 和 `.cache/gcc-sanitizer-fix-tests.log`。
- GCC 16.1 Release 图形 ON/网络 OFF 回归 **8/8 通过，6.34 秒**；日志为 `.cache/gcc-sanitizer-fix-graphics-build.log` 和 `.cache/gcc-sanitizer-fix-graphics-tests.log`。
- Linux sanitizer CI 同样显式设置遇错退出，避免 UBSan 默认恢复执行时产生错误却返回成功。

本次 Clang 首次构建另报告 22 条普通编译警告，主要是有符号索引转换、浮点数转布尔和局部名称遮蔽；不能将本次构建称为零警告。它们与上述运行时检测结果分别记录。

日志：`.cache/llvm-sanitizer-configure.log`、`.cache/llvm-sanitizer-build.log`、`.cache/llvm-sanitizer-tests.log`（修复前）、`.cache/llvm-sanitizer-tests-fixed.log`（修复后）、`.cache/llvm-asan-probe.log`、`.cache/llvm-ubsan-probe.log`。CTest 详细输出位于 `build-llvm-sanitizers/Testing/Temporary/LastTest.log`。

## 复现（PowerShell）

将 `$llvmBin` 设置为实际 LLVM-MinGW 安装目录的 `bin`。它应包含 `clang.exe`、`clang++.exe`、`llvm-symbolizer.exe` 和 `libclang_rt.asan_dynamic-x86_64.dll`。旧终端不一定继承刚更新的系统 PATH；显式前置目录即可，无须修改全局环境。

```powershell
$llvmBin = 'C:/path/to/llvm-mingw/bin'
$env:Path = "$llvmBin;" + $env:Path
& "$llvmBin/clang++.exe" --version
cmake -S . -B build-llvm-sanitizers -G Ninja `
  "-DCMAKE_C_COMPILER=$llvmBin/clang.exe" `
  "-DCMAKE_CXX_COMPILER=$llvmBin/clang++.exe" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DSHINY_GRAPHICS=OFF -DSHINY_NETWORK=ON -DSHINY_SANITIZERS=ON
cmake --build build-llvm-sanitizers --parallel 8
$env:ASAN_OPTIONS = 'halt_on_error=1'
$env:UBSAN_OPTIONS = 'halt_on_error=1:print_stacktrace=1'
ctest --test-dir build-llvm-sanitizers --output-on-failure
```

保留 LLVM-MinGW `bin` 在测试进程的 PATH 中，以加载 sanitizer DLL 并解析调用栈。本构建用于开发检测，不替换已有 GCC 发行包；`-static` 不代表 sanitizer 动态库被静态打包。

本次验证不需要 WSL。Linux sanitizer CI 尚未远端执行，macOS/Linux 原生验收状态不变；未验证 LeakSanitizer、图形后端的 sanitizer 运行或跨架构一致性。
