# ShinyCore 架构总览

![ShinyCore 架构总览：游戏项目由应用宿主加载，应用持有跨房间服务与设备；活动房间拥有独立 Lua VM 和 World；Lua 通过校验边界调用原生模拟。](architecture-overview.svg)

图中路径均相对于仓库根目录。详细生命周期与限制见[架构与契约](architecture.md)。
