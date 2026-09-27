# 弹体地图碰撞验证（2026-09-24）

原生 systems 测试使用实际批量弹体推进，验证高速穿越薄矩形的命中距离、地图
版本变化后更新缓存、凸多边形边及圆角顶点、初始重叠、外框空白角、穿透弹体在墙
处终止、墙后目标不命中、terrain=false 和单向平台一像素几何。
额外注入空间索引容量耗尽，检查在弹体推进前报错及后续重建可恢复。

Lua features 端到端导入 Tiled 三角形：第一发命中；删除图块后同一路径通过；
恢复水平翻转图块后反向发射再次命中。比较 target=0 和实际 fraction，
而不只是检查进程正常结束。圆角相交计算改用 double 中间值，减少高速时的消减误差。

Windows Release 完整构建、轻量构建和 LLVM-MinGW 22.1.8 无窗口 ASan/UBSan
构建的上述相关测试分别 4/4 通过。Sanitizer 使用遇错退出设置，未报告错误。
此记录只覆盖这些测试，不替代当前完整测试套件。

测试命令：

```powershell
ctest --test-dir build/full -R '^(systems|script|features|complete_integration)$' --output-on-failure
```

此项未执行完整性能门槛、两小时运行、Linux/macOS 或图形设备验收。
实现与剩余边界见[弹体地图碰撞](projectile-terrain.md)。
