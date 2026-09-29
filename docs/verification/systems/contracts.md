# 实体契约验证 · 2026-09-21

本轮落实 W0 的实体契约部分，并修复它暴露的读取、边界和批量处理问题。
整体完整版目标继续保持未完成；本记录不代表其他系统或性能验收通过。

- `ScEntityPatch` 的 22 个字段、`ScEntity` 快照和 `ScEntityEdit` 批量项由
  原生元数据生成 LuaLS 定义与参考文档。数值/布尔字段的校验、初始值、读取
  共用类型安全的成员表；复杂图像/刚体约束仍是显式代码。
- 九个实体函数包含参数、返回值、模块、阶段及适用的容量信息。未转换的
  函数没有结构化契约或返回 null，不能把字段覆盖理解为整个 API 已完成。
- `sc.get/get_many` 现在返回 `flip_x/flip_y`；宽高 `0.001` 不再被单精度
  下限误拒绝。能确定字段名的标量错误会包含该名称。
- 批量重复 ID 检查使用固定 8 KiB 位图；批量暂存区按项目实体容量预留，
  更新中复用。启动回调加入异常保护，分配异常不穿过 Lua 的 C 回调边界。

## 实际验证

Windows 本机 GNU 16.1 Release：最小图形配置 CTest **15/15**，完整配置
CTest **18/18**。LLVM-MinGW 22.1.8、网络/流式开启、无窗口 ASan/UBSan
CTest **18/18**，遇错退出开启。完整构建的原生 profile 回归 **7/7**，
同一回放在采样开/关时截图相同，图形/无窗口玩法字段一致。

新增六组契约测试实际检查：

- 元数据字段集合、默认值、只读字段及四个示例的本地 SDK 一致性。
- 数值上下边界、非整数、错误类型、NaN/Inf 和过长文本。
- 配对图集尺寸、颜色、非法路径、刚体多边形与 body/dynamic 优先级。
- 失效句柄、重复 ID、整批预检失败时其他项不修改。
- 函数阶段与 draw 中的只读/修改限制、字段错误定位。
- 原生数据与生成文档一致；最小构建只检查其实际可用接口，完整构建检查
  全部生成输出，避免把关闭模块误认为文档缺失。

C++ 测试另外执行 2000 个实体的批量修改、尾部重复 ID 失败、位图重置及
再次修改，并验证暂存区地址/容量不变。这是容量和正确性检查，不是规定的
2000 单位持续性能负载，也没有据此宣称达到 16.67 ms 门槛。

```sh
python tools/api_docs.py build/full/shiny.exe build-dev/shiny.exe --reference docs/api-reference.md --check
python tests/test_contracts.py build/full/shiny.exe
ctest --test-dir build/full --output-on-failure
```

完整的嵌套类型、可选参数默认值、错误代码、其他函数契约、五类压力负载与
后续 W1–W8 仍待实现或验收；Linux/macOS 未在本轮本机执行。

## 音频契约补充（2026-09-26）

`sc.audio.play/set/stop/bus` 新增结构化参数、返回值、容量和阶段；`bus` 单独声明
带非 nil options 时的修改阶段。三种音频选项/结果类型包含默认值、范围和声音
类型决定的默认总线。优先级先以 double 检查整数，拒绝数字字符串句柄及带 NUL
后缀的资源/总线名称；失败保持原状态。

实际只执行了本次需要的检查：Release 与重建的完整无窗口 ASan/UBSan 运行
`tests/audio_contracts.py`；sanitizer 另运行既有的两个音频行为用例，均通过。
两个生成器检查验证文档同步和类型后相邻声明不被误删除。包含注解的六个项目
SDK 更新到 `1.0.0-dev.3`，七个项目当前 SDK 清单审计均通过。
这些不是前文历史全量 CTest 的重新执行，也不代表其余 API 已全部结构化。

```powershell
python tests/audio_contracts.py build/full/shiny.exe
python tools/api_docs.py build/full/shiny.exe --reference docs/api-reference.md --check
```

## 物理契约补充（2026-09-27）

11 个 `sc.physics` 函数已提供结构化参数、范围、默认值、返回类型、容量和阶段。
`ScContact`、`ScRayHit`、`ScJointControl` 与 `ScJointControlPatch` 由原生字段
表生成；补丁字段可省略，完整结果字段必有，关节种类相关的范围与初始值明确说明。
接口现在拒绝多余参数、字符串句柄、带 NUL 的关节种类以及带元表的查询点列。

本次只运行相关检查：Release 与完整无窗口 ASan/UBSan 的
`tests/physics_contracts.py` 通过，覆盖字段/阶段一致性、参数数量、句柄类型、
几何输入、独立结果副本和失败补丁原子性；sanitizer 另运行既有的精确形状查询
及关节电机/生命周期两个用例，通过。没有运行图形、性能或全量测试。
同步过程发现生成器误删紧邻函数的类型字段/别名，现已修正，并逐类型检查
注解字段与原生数据一致；52 位整数边界也不再按 15 位有效数字舍入显示。
相关生成器检查通过。六个含注解的项目 SDK 升至 `1.0.0-dev.16`；其余 API
未全部结构化。

```powershell
python tests/physics_contracts.py build/full/shiny.exe
```
