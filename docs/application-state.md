# 应用、设置与共享状态

`sc.app`、`sc.settings`、`sc.state`、`sc.debug.watch` 和 `require` 的参数及阶段
由原生注册表生成到 `--api`、[LuaLS 注解](api.lua) 和 [字段参考](api-reference.md)。
应用/设置绑定位于 `src/script/script_application.cpp`；存储与窗口适配仍归现有宿主服务。

| 接口 | 契约 |
| --- | --- |
| app.pause(boolean) | load/init/update/ui_update；暂停原生实体运动、物理、弹体和粒子，初始 false |
| app.paused() | 所有脚本阶段可读；当前房间的暂停标记 |
| app.quit() | load/init/update/ui_update；请求有序退出，候选房间的标记在提交前不影响活动房间 |
| settings.get() | 独立深拷贝；修改返回表不会应用设置 |
| settings.apply(patch,persist?) | 仅普通 update，检查模式禁止；nil/省略 persist 为 true；成功返回 true，服务失败返回 nil,error |
| settings.error() | 最近一次设置加载/服务应用错误，成功应用后清空；Lua 调用参数错误不替换它 |
| state.get(key) | 所有脚本阶段可读；独立深拷贝，缺失返回 nil |
| state.set(key,value) | load/init/update；替换一个键，显式 nil 删除；无效输入保留全部旧状态 |
| debug.watch(name,value) | load/init/update；复制观察值，最多 64 个名称；nil 保留 null 观察值并占槽，不是删除 |
| require(module) | 首次加载仅 load/init/update；缓存读取也允许 draw/ui_update；缓存保留返回值身份，nil 返回转为 true |

暂停不停止 Lua update、ui_update、tick、相机或应用服务；游戏应自行按暂停状态
控制玩法规则。暂停和退出标记归房间所有。共享状态在候选房间内修改其副本，
只有候选提交时发布；存档和设置仍通过各自服务处理。

设置补丁省略的字段保持当前值。width/height 为整数，范围分别为 320..7680 和
180..4320；初始引擎默认 1152×648、windowed、integer、vsync=true。
项目 display 和已保存设置可覆盖这些默认值。volume 支持 master/music/sfx/ui，
每项有限值 0..1，初始均为 1；只合并指定总线。bindings 为最多 16 KiB 的普通
字符串键对象，补丁会整体替换，键位语义由 Lua 输入模块解释。

设置字段/设备/磁盘失败保持旧的逻辑设置；窗口回退若也失败会附加诊断。
非法参数个数、persist 类型、不可转换的数据或阶段错误直接抛 Lua 错误。
未配置 project.id 或存储根时，persist=true 也仅保留内存设置。

状态键为 1..128 字节、无 NUL 的字符串；整个状态为最多 256 KiB、16 层的
显式 UTF-8 数据。观察名称同样要求 1..128 UTF-8 字节，无 NUL；每个观察值受
256 KiB/16 层限制。两者拒绝元表、稀疏/混合键表和非有限数字，不执行元方法。
观察数据校验失败保留旧值。所有上述接口拒绝多余参数，名称不接受数字隐式转换。

必要验证：Release 与全功能无窗口 ASan/UBSan 通过 application_contracts、
settings_integration，以及既有设置持久化/候选限制、跨房间状态、非法状态原子性
三项检查。覆盖 NUL/无效 UTF-8、观察槽上限、复制隔离、阶段与参数限制、模块失败
重试、设置补丁回退和绑定整体替换。生成文档检查及 16 项目 SDK 审计通过，dev.44。
记录在 `build/application-contracts-reviewed/`；没有图形改动，不重复截图或设备验收。
