# 对象持久 ID 验证 · 2026-09-21

完成 W1 的房间内对象身份基础。字段统一为 `persistent_id`，Lua 接口为
`sc.identity`；原生元数据、LuaLS、四个示例 SDK、诊断快照与离线地图输出
使用相同术语。资源工具版本递增，使旧缓存不复用已改名的输出。

固定容量索引保存活动、未加载和已删除状态；重复活动 ID、容量不足及非法
名称显式失败。对象持久 ID 创建后不可变，批量补丁失败不会部分修改其他
对象。引用保存房间路径和对象持久 ID，不保存运行时句柄。契约与尚未接入的
地图/分块状态边界见 [identity.md](identity.md)。

Windows 本机实际执行：

| 配置 | 结果 |
| --- | --- |
| GNU 16.1 Release，轻量图形 `build-dev` | CTest 17/17 |
| GNU 16.1 Release，完整配置 `build/full` | CTest 20/20 |
| GNU 16.1 Release，无窗口 `build-headless-v02` | CTest 20/20 |
| LLVM-MinGW 22.1.8，无窗口 ASan/UBSan | CTest 20/20，遇错退出开启 |
| 完整及轻量二进制生成契约一致性 | `tools/api_docs.py --check` 通过 |

新增原生检查覆盖固定存储、容量、重复 ID、卸载/删除/重建、过期句柄、无
部分登记，以及状态哈希。三个脚本集成测试实际执行了跨房间存档恢复和新
进程读取保存的引用；同时检查禁用容量、非法引用、不可变字段及 draw 阶段
限制。离线构建检查对象输出字段与可重复构建。

新增索引状态参与诊断哈希，更新了默认世界及固定种子的黄金值；默认构造
与显式重置仍应一致。哈希不作为持久格式或完整模拟状态使用。

以上不代表 W1 全部完成，也不代表五类性能负载、实体设备、Linux/macOS
或原生画面验收。视觉附着、地图对象完整整合和分块状态恢复仍待实现。

## Batch creation · 2026-09-25

`sc.spawn_many` now validates a complete batch before committing any entity or
persistent identity. Native tests verify unchanged registry states, drafts and
handle generations on failure, exhausted generation slots, pool-alias rejection,
and successful reconstruction of unloaded/deleted IDs. Lua tests cover field and
capacity errors, duplicate IDs, plain dense arrays, input order and forbidden
phases. The Lua result array is allocated before native commit.

Windows GNU Release full and lightweight builds each passed the five headless
suites `core`, `script`, `identity_integration`, `contracts`, `complete_integration`.
LLVM-MinGW ASan/UBSan passed the same 5/5 with halt-on-error. API generation and
full/light contract consistency checks passed; example annotations were synced.
No native window or performance test was run. This validates atomic entity batch
creation, not complete streamed terrain/render/object lifecycle integration.
