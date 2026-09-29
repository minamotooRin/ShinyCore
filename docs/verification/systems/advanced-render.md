# 高级渲染原生视觉检查（2026-09-27）

使用 Windows 完整 Release 构建、真实图形上下文，每项固定 3 帧，隐藏、
不获取焦点、静音、隔离存档。未运行负载、性能优化或长时间资源循环。

视觉检查发现并修正：

- 法线光照在切换三角形图元前绑定纹理，raylib 的模式切换随后将其重置为
  默认白纹理。现改为切换图元后绑定法线目标，不修改依赖代码。
- 后处理示例把完整动画图集压缩显示；现取 12×18 的单帧，等比显示角色。
- 阴影示例的近黑背景无法清楚表现投影；调整地面底色后可观察硬/软边缘。

`tests/native_advanced_render.py` 是显式运行的原生检查，不加入默认 CTest：

```powershell
python tests/native_advanced_render.py build/full/shiny.exe --output build/render-check
# 只复查受影响的分组；输出目录必须不存在。
python tests/native_advanced_render.py build/full/shiny.exe --output build/normal-check --case normals
```

法线探针使用相同白色表面、固定光源和常量方向法线。修复前改变方向没有效果；
修复后 +X 朝右灯、+Y 朝下灯、实体旋转 90° 朝下灯的中心亮度均为 233，
反向法线及水平翻转后为 25。检查门槛分别为 >170 和 <40，不依赖逐像素黄金图。
同时验证 screen UI 不受照明、原生状态 ready 和法线目标的实际逻辑分配量。
六个探针通过；示例球面、图集倒角及变换后的截图已实际查看。

后处理关闭及四 pass Bloom＋调色均已截图并查看。GPU 状态 ready，四 pass
使用两个颜色目标；关闭后目标字节数为零。世界像素产生变化，操作说明的
不透明 UI 字形保持一致。仅凭诊断计数不宣称驱动资源已完成泄漏验收。

硬阴影和四样本软阴影均已查看，地面投影及半影层次可辨，世界区域像素有变化。
材质示例已查看原图/显式覆盖/精灵默认材质、弹体、粒子、几何与图块，程序
状态 ready。示例和探针不代替全部变换、透明排序及驱动行为的验证。

本地证据：`build/advanced-render-reviewed-v2/` 保存法线探针和示例，
`build/advanced-effects-reviewed-v2/` 保存后处理、阴影和材质。每份记录包含
引擎 SHA-256、命令、输入、PNG、日志及状态；截图初始标记 pending，实际查看
后才记录 reviewed。法线目录的最初整组执行在后处理断言格式处停止，后处理及
其余分组已修正检查并在第二个目录单独通过，没有将中断执行记作整组通过。

仍未验收：透明法线混合、辅助
材质采样器、单独扭曲/调色效果、相机变换组合、跨房间 GPU 生命周期、性能门槛、
Linux/macOS。此次仅修改 GPU 提交顺序和示例，不改变原生所有权或 Lua 边界；
无窗口 sanitizer 不能覆盖该 GL 状态问题，因此本轮使用定向原生像素回归。

## 着色器与房间错误恢复

`tests/native_material_recovery.py` 用 stdio 单步控制实际 GPU 场景，在初始程序
编译成功后改写独立测试目录内的着色器。每项 2–4 帧，管道响应和进程退出均有
10 秒超时；隐藏、静音，输入回放隔离真实设备。支持 `--case` 只复查相关分支：

```powershell
python tests/native_material_recovery.py build/full/shiny.exe --output build/material-recovery
```

- 注入 GLSL 语法错误和未声明的活跃 uniform 后，状态 failed，compiled_revision
  保持 1，红色表面像素保留为 (204,51,25)。修正并 reload 后，revision 3
  编译成功，像素变为 (25,204,51)。三种结果均已查看。
- 候选房间 GPU 编译、脚本初始化、尺寸校验失败均保留旧房间、程序及共享状态。
  检查发现原失败分支会跳过当帧 trace 和 debugger.completed；现在统一完成
  已模拟帧的收尾。四个定向用例验证连续 trace 帧和正确的 stopped/terminated。
- 后续成功切房间清除旧错误提示，实际查看恢复后的绿色画面。窄窗口诊断按
  实际字宽和 UTF-8 字符边界换行；320×180 与 64×64 逻辑画布均通过提示可见、
  不越界的像素检查并已查看。小画布仅显示有限行，完整错误仍写入 stderr。

证据分别位于 `build/material-recovery-reviewed/`（前三个着色器用例）、
`build/material-candidates-reviewed-v2/`（四个房间用例）和
`build/material-diagnostics-reviewed/`（两种尺寸）。manifest 记录各自引擎散列，
不是把不同构建或中断批次合并宣称一次全套通过。
相关八帧隐藏音频事务复查通过；宿主重建后的无窗口 ASan/UBSan 材质契约检查通过。
这不覆盖 GPU 分配失败、驱动崩溃、长时间资源循环或全部设备组合。
