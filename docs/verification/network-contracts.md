# 网络 API 契约验证

本轮补齐 5 个网络函数与 10 个会话方法的结构化参数/默认值/范围/返回值/阶段，
以及 ScNetEvent、ScNetStats 字段。使用统一 ScLuaApi 注册描述，删除单独的网络
JSON 格式化路径，保留上下文与副作用检查闭包。会话名拒绝数字隐式转换。
`mutation.when=present` 明确 state(nil) 是写入；原有 non_nil 语义不变。

已执行的必要验证：

- Release、全功能无窗口 ASan/UBSan：network_contracts.py 检查元数据、预算快照、
  名称类型/NUL、显式 nil 写入限制、候选初始化、失效绑定与跨房间状态。
- 两种构建均运行 test_net_lua：真实二进制消息回环、参数/容量/副作用边界、
  显式关闭/GC/VM 释放、命名会话、状态副本及 Lua 分配失败清理。
- Release 的既有双进程 test_named_session_survives_room_and_paused_simulation：
  实际 localhost UDP 交换 hello/ack/done；暂停、切房间后端口和 peer 保留，双方完成退出。
- 网络关闭的 lightweight 构建成功；无窗口运行确认 sc.net.available=false 且
  host/bind 缺失；--api 不含网络函数/记录。Ninja 的引擎链接与脚本静态库目标
  不含 ENet / net_lua 输入。
- 完整/轻量 --api 联合生成文档检查通过；16 个项目注解与 SDK dev.45 审计通过。

证据索引：`build/network-contracts-reviewed/manifest.json`，包含构建/检查范围、
源文件与二进制哈希。网络副作用测试仅访问本机回环地址，没有可见窗口或音频。
本轮不重复四人弱网、性能、长时间运行或硬件验收；不能据此声明这些项目已达标。
全功能 API 仍有 27/181 项缺少结构化契约，完整引擎交付目标仍未完成。
