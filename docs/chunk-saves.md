# 分块世界存档

全部槽位接口、返回记录及错误语义见[存档槽契约](saves.md)。

`sc.save.write_chunks(slot, changes)` 同时保存当前房间、`sc.state` 和指定块的
显式数据；未修改的块保留。它不保存实体句柄、Lua VM 或物理解算器。

```lua
-- update 中：记录对象持久 ID、对象属性和删除标记。
local ok, err = sc.save.write_chunks("slot1", {
    ["main/forest:-1:0"] = {
        objects = { ["chest.7"] = { opened = true } },
        deleted = { "door.2" },
    },
    ["main/forest:0:0"] = false, -- 删除此块的持久记录
})

-- init 或 update 中：一次只读需要的块。
local chunk, err = sc.save.read_chunk("slot1", "main/forest:-1:0")
if chunk then
    for _, persistent_id in ipairs(chunk.deleted) do
        sc.identity.remove(persistent_id)
    end
end
```

块 key 使用[对象持久 ID](identity.md)的字符规则；建议包含房间、地图稳定名
和块坐标。块内容必须是普通数据对象，结构由游戏定义。`false` 只在 changes
的第一层表示删除，块内部布尔值保持正常语义。缺块返回 `nil`；读取错误返回
`nil, error`。写入成功返回 `true`，预检或磁盘错误返回 `nil, error`。

普通 `sc.save.write` 保留已有块；`read` 返回检查点和 `chunk_count`，块引用索引留在原生端；`load` 仍以
重建房间恢复显式状态。`delete` 删除槽、备份及该槽管理的块记录。写入仅允许
update，块读取允许 load/init/update，检查模式禁用块读写。无窗口测试必须
传 `--save-dir`；普通小存档继续支持原有内存槽。

## 原子性与恢复

磁盘布局为 `slot.json`、`slot.json.bak` 和 `slot.json.chunks/*.json`。
索引格式为 **3**，旧格式明确拒绝。块文件不可变，以内容摘要和碰撞后缀命名；
相同内容可复用，发现不同内容绝不覆盖。摘要用于损坏检测，不是认证或加密。

先预检全部修改，再写块，保留上一份完整有效索引，最后原子替换当前索引。
只有最后一步是提交点。读档先逐块验证索引引用的长度、格式和摘要；任何一个
块损坏，都回退到完整的上一份检查点，不混合两个时刻的房间、共享状态或块。
两份均损坏时拒绝覆盖，游戏可明确删除该槽后创建新存档。

一个 VM 缓存最近读取槽的索引，不驻留所有块。后续 `read_chunk` 根据该索引
单独读取并验证一个块；已选快照中的块随后损坏会报错，不悄悄切换快照。
`read/load` 显式重新选取索引，写入和删除使缓存失效；切房间继承选定索引。
当前索引与上一份有效备份引用的块都保留，成功提交后回收其余引擎块文件。
回收失败不把已提交存档报告为失败，后续提交重试；目录中的其他文件不会被删。

## 容量与当前边界

| 项目 | 限制 |
| --- | --- |
| 共享 `sc.state`、单块序列化数据 | 各 256 KiB，深度 16 |
| 一次 Lua changes 参数 | 现有 Lua 数据转换预算 256 KiB，包含容器开销 |
| 槽内块数 | 16384 |
| 索引序列化大小 | 4 MiB |
| 索引引用的块总字节数 | 1 GiB |

`--api` 的 `save` 字段输出这些真实常量。初始化完整快照时逐块验证，不把所有
数据留在内存里；显式同步读写接口会等待磁盘，不应逐帧调用。后台读写接口见下。
加载边界、卸载导出及进入块的批量异步状态读取已接入 [stream_world](stream-world.md)。
声明在 `project.stream_indexes` 的地图索引现由内容线程预读；标题菜单加载仍同步，
本实现不证明流式地图帧耗时达标。
单槽并发写入和 POSIX 断电持久性尚未承诺。

## 原生后台读写组件

`include/shiny/save_io.h` 的 `ScSaveIo` 提供后台事务基础，宿主持有它并提供
异步 Lua 接口，显式同步工具接口仍保留。房间只提供独立数据快照；不能让房间
拥有服务，否则房间析构仍会等待磁盘。线程按需启动，不使用 Lua、世界指针或 GPU。

| 原生操作 | 语义 |
| --- | --- |
| `submit(request)` | 无磁盘 I/O 的数据预检，接收独立快照，返回 1..2^52−1 请求号 |
| `advance(id)` | 不等待磁盘；false 表示未完成，true 表示读取完成或写入已提交，expected 错误表示失败 |
| `retry(id)` | 仅重试已观察到的失败，保留请求号与原始快照 |
| `release(id)` | 仅释放已观察到的结果；不撤销已提交存档，旧请求号失效 |
| 析构 | 完成已接受的 IO 后结束线程；不会报告未观察到的失败，宿主须先处理结果 |

同时最多保留一笔未释放读/写事务，共用一条工作线程。写入负载使用 1 MiB 编码预算（记录、路径、项目名、
块值及每项键/容器余量）；这不是进程堆内存上限。块数、单块数据和磁盘世界容量
仍沿用上述限制。预检失败不会排队或写文件。待处理事务不能取消、替换或重复重试，
避免给出“已取消”却稍后实际落盘的结果；完成后需显式 release 才能提交下一笔。

写入仍复用同一份格式校验、备份、原子索引提交和块回收实现。调用方必须排除
该槽的其他同步/异步磁盘操作；组件不提供跨实例或跨进程锁。advance 只负责
取得结果，本身不提供模拟帧语义。宿主在下一个固定更新边界等待，维护 UI/设备，
并排除未释放事务期间的其他存档访问、切房间及热重载。释放成功读取时固定选定索引；
释放写入、失败或缺失结果时清除选定快照缓存。
流式世界模块已在保存成功后再发布地形/对象，不能将接受请求视为保存成功。

## Lua 异步事务

`write_async(slot)` 与 `write_chunks_async(slot,changes)` 仅允许 update，返回
请求号或 `nil,error`；需要磁盘目录，无窗口必须传 `--save-dir`。数据在调用时
复制，后续修改 Lua 表或 sc.state 不影响这笔保存。检查模式和候选初始化禁止写入。

```lua
local request
return {
    update=function()
        if sc.tick()==0 then
            request=assert(sc.save.write_chunks_async('slot1', {
                ['forest:0:0']={opened=true},
            }))
        elseif request then
            local status=sc.save.status(request)
            if status.status=='complete' then
                sc.save.release(request)
                request=nil
                -- 此时才卸载对象、发布地形或调用 sc.scene。
            end
        end
    end,
    ui_update=function()
        if request then
            local status=sc.save.status(request)
            if status.status=='failed' and sc.input.key_pressed('enter') then
                sc.save.retry(request)
            end
        end
    end,
}
```

`status(request)` 返回独立 `{request,operation,status,error?}`，operation 为 read/write，
状态为 pending/complete/failed。
它不会查询后台完成时机；提交当帧仍为 pending，宿主在下次固定边界发布结果。
当前 tick 的物理步骤正常完成，等待期间不消费输入/回放、不推进模拟或逐帧 trace；
图形端继续绘制、设备采样、音频和网络维护。预检/快照复制仍发生在主线程。

`retry` 与 `release` 允许活动房间的 update/ui_update，禁止 draw、初始化和检查模式。
失败时可重试原快照，或 release 失败结果后继续旧世界；不能释放 pending 请求。
成功后也需 release，之后可再保存、读档或切房间。所有其他存档操作在请求未释放时
报错，避免索引读取与块回收竞态。已释放、未知或非整数请求号报错。

无窗口失败会给 ui_update 一次处理机会；未重试或释放则带 save 诊断退出。
普通离线回放不会消费未来按键来解开等待，故障测试用外部文件修复与显式 UI 回调协调。
退出或 --frames 到限后完成已接受的写入；未处理失败会使进程报错，不能用提交成功
声称存档已落盘。应用退出等待不具备 UI 交互保证，也不承诺强杀进程后的提交成功。

`shiny.stream_world` 已封装请求、暂停和完成后发布，Wayfarer 使用该流程，
见 [流式世界](stream-world.md)。底层同步工具 API 仍保留，Crossing/Barrage
尚未转换。协调模块的存档读取/索引验证已在后台，对象、地形与导航准备/发布仍在主线程。
图形宿主的加载/保存提示使用底部短条，失败不会再显示热重载错误面板；
详细诊断保留在 stderr 和 status.error，游戏负责提供恢复控件。

## 批量异步读取

```lua
-- update：先提交；下一固定更新才观察完成，期间 UI 继续运行。
request = assert(sc.save.read_chunks_async("slot1", {"forest:0:0", "forest:1:0"}))
-- 后续 update，status 为 complete：
local restored = sc.save.result(request)
sc.save.release(request)
-- restored.record 是检查点摘要；restored.chunks 按 key 返回所请求的独立状态。
```

`read_chunks_async(slot,keys)` 仅允许活动房间 update，返回请求号或 nil,error。
需要磁盘目录；禁止候选、检查模式、已有事务或待切换房间。keys 为 0..1024 个
唯一对象持久 ID 语法键；空数组只读取摘要。提交不执行磁盘 IO，输入在提交时复制。
索引、完整性验证及块文件读取都在工作线程执行，沿用当前格式与完整备份回退。
已有固定快照时读取同一代索引，不因某块损坏而混入其他备份；没有快照时统一选择。

`result(request)` 只接受已完成的读取，返回 `{record?,chunks}` 副本。主文件与备份
都不存在时 record 缺失、chunks 为空；未知块键省略。损坏、权限或预算错误是失败，
不返回半批状态。record 与 read(slot) 摘要一致，不暴露内部文件名/索引。
修改结果不会影响后续 result、当前 sc.state 或磁盘；该操作不自动重建房间。

选中的索引上限 4 MiB，所选块结果合计 1 MiB（含键及容器余量），每块仍受
256 KiB 限制；这些是编码预算，不是堆内存承诺。result 可在只读阶段调用，但
pending/failed/write/失效请求均报错。读取成功 release 后固定其索引，后续块读取
保持一致；同槽 `sc.save.load` 也使用该索引，不再同步重读磁盘。当前应用的写入或
删除会清除固定索引。retry 重用原请求和已传入的索引，未预选索引时重新尝试
完整有效快照。
读写共用应用互斥和固定边界，故磁盘完成速度不能被 Lua 在帧内观察到。
