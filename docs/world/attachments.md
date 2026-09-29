# 原生父子视觉附着

`sc.presentation.attach(child, parent, offset)` 建立房间内的生成号句柄关系。
实体直接保存父句柄和局部姿态，不引入独立对象池或 ECS；同步不分配内存。

```lua
local player = sc.spawn {x=100, y=80, w=20, h=12,
    body={type="dynamic", fixed_rotation=false}}
local lamp = sc.spawn {w=6, h=4, body=false, solid=false, color="#FFC98C"}
sc.presentation.attach(lamp, player, {x=22, y=4, angle=.2})
sc.presentation.interpolate(true) -- 可选，只在初始化首次启用。
local relation = sc.presentation.attachment(lamp) -- parent/x/y/angle 的独立副本。
sc.presentation.detach(lamp) -- 保持固定世界姿态，成为根对象。
```

## 坐标与更新

- offset 是仅含 x/y/angle 的普通表，缺省值均为 0；再次 attach 整体替换偏移。
  每个值必须有限且位于 ±1,000,000。x/y 相对父对象未旋转边界左上角，angle 为
  局部弧度；旋转围绕父中心作用于子中心。尺寸、翻转、颜色、图层和 solid 不继承。
- 子对象必须无刚体，vx/vy/angular_velocity 为零。附着后不可用实体 patch 改变
  世界 x/y/angle 或加入刚体/速度；违规批量 patch 整批失败。局部变化用 attach，
  独立运动前先 detach。物理连接用关节，纯装饰建议 solid=false。
- Lua set/set_many 提交后同步子树；固定步进在刚体和普通根运动后、弹体及相机
  之前同步。暂停时仍同步显式修改。sc.get/get_many 始终返回固定世界坐标，范围
  查询和弹体命中使用这些坐标；无刚体附件不会自动成为 Box2D 查询形状。
- attach 预检句柄、子对象条件、循环及整个子树的新深度，至多 32 条父边；失败
  保留原关系。直接子对象当前世界姿态越界也拒绝。后续运动及深层后代同步沿用
  原生运动边界：世界 x/y/angle 饱和到 ±1,000,000；边界外不保证几何关系。

## 插值与生命周期

显示先混合根姿态，再组合父链中混合后的局部姿态，旋转时附件沿圆弧跟随。
角度走最短弧，尺寸用当前值。改换父关系或 snap 祖先时，后代链使用当前固定
姿态；同父局部动画仍可插值。原生精灵、光源、法线和遮挡共用显示姿态。

detach 对已解除关系的活动对象无操作，失效句柄报错。销毁/卸载父对象先同步，
再解除直接子对象关系并保留世界位置；更深后代继续跟随仍活动的父对象，不隐式
级联销毁。房间销毁释放全部关系，槽位复用不继承关系；原生 spawn 复制实体也
不复制父关系。attach/detach 允许 load/init/update；attachment/pose 可只读检查。

attachment 返回 nil 表示根。父句柄不是持久引用；存档显式保存父子
[对象持久 ID](identity.md)和偏移，再原子重建关系。
[Lua prefab](prefab.md)已使用原生批量关系，无需逐帧调用 Lua 更新。
原生嵌入者用 sc_attach/sc_detach 修改关系，直接改实体后调用 sc_attachments_sync；
不得直接改 parent/local_pose，须遵守子对象契约。插值历史仍按需分配，stats().bytes
已包含父关系和局部历史字段。

## 原子创建与持久恢复

`sc.spawn_many(entities, parents?)` 的可选 parents 是与 entities 等长的普通连续
整数数组：0 表示根，1..N 指向同批实体，允许前向引用。省略或 nil 创建独立根。
附着项的 x/y/angle 是局部偏移，其余字段仍按普通实体解释。整批预检实体、ID、
容量、父索引、无刚体/速度条件、循环、深度与全部组合世界姿态，再创建并连接。
任一失败不占用实体或持久 ID；输入 Lua 表不变，返回句柄顺序与输入一致。
显式空 parents 只可配合空 entities；已有世界中的实体不通过此索引数组引用。

[examples/attachments](../../examples/attachments/README.md) 演示把父对象持久 ID 转换
为同批索引，从显式存档原子重建。模板/几何保留在项目数据，存档只保存关系、
局部/世界姿态和明确选择的速度字段，不序列化 VM、求解器缓存或运行时句柄。

## 定向证据

Release 的 test_attachment 检查真实刚体更新顺序、嵌套中心变换、半帧圆弧插值、
局部动画、祖先瞬移、父销毁/槽位复用、循环与子树深度预检。
attachment_integration.py 检查 Lua 参数、阶段、关系副本和批量 patch 原子性。
既有 test_presentation 与 presentation_integration.py 确认插值开关不改变固定回放/存档。

```powershell
build/full/test_attachment.exe
build/full/test_presentation.exe
python tests/attachment_integration.py build/full/shiny.exe
python tests/presentation_integration.py build/full/shiny.exe
```

60 帧隐藏、静音原生图 `build/attachment-reviewed/attachments.png` 已查看：动态根、
旋转附件与嵌套标记方向正确，标签无重叠。原生/无窗口 entities、watches、hash、rng、
tick 相同。截图为固定状态；分数 alpha 几何由原生单元测试验证，不冒充高刷新率
视觉验收。同四项定向检查已在全功能无窗口 ASan/UBSan 构建通过，设置遇错退出，
无检测器报告。API 生成一致性及携带注解的项目 SDK 审计通过，分发版本 dev.40。
未执行性能、长时间或硬件交互测试。

批量关系与 prefab 整合的后续检查在 Release 和全功能无窗口 ASan/UBSan 通过，
包括前向索引、失败不修改草稿/世界、Lua 严格数组与对象持久 ID 恢复。
`build/attachment-save-reviewed/restored.png` 的 40 帧隐藏原生图已查看，原生/无窗口
固定字段一致；保存、解除、重置房间和读档后的显式关系/姿态一致，句柄已更新。
API 与项目 SDK 注解同步为 dev.41。
