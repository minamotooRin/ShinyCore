# ShinyCore 架构总览

```mermaid
flowchart TB
    subgraph AUTHOR["① 编写与验证 · 可移植文本项目"]
        direction LR
        GAME["🎮 游戏项目<br/>project.lua · 房间 Lua · 资源<br/>examples/"]
        LIB["🧩 Lua 标准模块<br/>lua/shiny/ → 项目 lib/shiny/"]
        TOOL["🛠️ Agent 工具链<br/>tools/ · tests/ · benchmarks/"]
    end

    subgraph APP["② 应用级 · 跨房间存活"]
        direction LR
        HOST["🧭 主循环与生命周期<br/>src/runtime/main.cpp<br/>输入快照 → 固定更新 → 显示"]
        SERVICE["📦 共享服务<br/>src/content/ · src/audio/ · src/net/ 可选<br/>资源 / 存档 / 设置 / 常驻音乐 / 会话"]
        DEVICE["🖥️ 设备适配<br/>src/platform/ · src/render/ · src/audio/device.cpp<br/>窗口 / GPU / 声音；可无窗口运行"]
        DEV["🔎 交互调试可选<br/>src/dev/"]
    end

    subgraph ROOM["③ 房间级 · 各有独立 Lua VM 与 World"]
        direction LR
        DRAFT["⏳ 候选房间<br/>独立准备资源与初始绘制"]
        ACTIVE["🏠 活动房间<br/>玩法 Lua / UI / 实体 / 临时状态"]
    end

    subgraph ENGINE["④ 原生执行 · C++23 / RAII / 显式容量 / 无通用 ECS"]
        direction LR
        BIND["🔐 sc.* 脚本边界<br/>src/script/<br/>类型、阶段、容量校验"]
        CORE["⚙️ 60 Hz 确定顺序模拟<br/>src/core/ · src/physics/<br/>实体 / 弹体 / 导航 / Box2D<br/>不依赖 Lua、窗口、GPU、网络"]
        API["📐 公共原生契约<br/>include/shiny/"]
    end

    GAME --> HOST
    LIB --> ACTIVE
    TOOL -.->|构建、回放、检查| GAME
    TOOL -.->|诊断| HOST
    HOST -->|拥有| SERVICE
    HOST -->|驱动| DEVICE
    HOST -.->|构建时启用| DEV
    HOST -->|准备| DRAFT
    HOST -->|逐帧推进| ACTIVE
    DRAFT -->|全部成功才替换| ACTIVE
    ACTIVE -->|update / draw| BIND
    BIND -->|已校验命令| CORE
    API -.- BIND
    API -.- CORE

    classDef authored fill:#eaf2ff,stroke:#91afe2,color:#183454
    classDef host fill:#e9f7f4,stroke:#83bfb3,color:#173c37
    classDef room fill:#fff3e3,stroke:#dbb47c,color:#553819
    classDef native fill:#f0ecfa,stroke:#b7a3da,color:#382a55
    class GAME,LIB,TOOL authored
    class HOST,SERVICE,DEVICE,DEV host
    class DRAFT,ACTIVE room
    class BIND,CORE,API native
```

实线表示拥有、驱动或运行调用；虚线表示离线工具、可选调试或契约关联。房间切换只在候选准备成功后提交；模拟内核不依赖 Lua、窗口、GPU 或网络。核心设计约束是 RAII 与独占所有权、启动时确定容量、固定顺序模拟，不引入通用 ECS。构建裁剪见 [CMakeLists.txt](../CMakeLists.txt)，详细语义见 [架构与契约](architecture.md)。
