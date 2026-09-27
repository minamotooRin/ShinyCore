# 三款样例主题音乐 · 2026-09-27

三款样例不再循环原 Workshop 的 0.3 秒占位音。主题分别为 Wayfarer 20 秒、
Crossing 16 秒、Barrage 15 秒；均是本项目编写音符/和弦并离线合成的原创
22,050 Hz 立体声 Ogg Vorbis，分别为 71,632/61,023/59,141 字节。音乐保持原资源名，继续由
`sc.audio.music` 取得应用级声部，不改游戏规则、随机数或存档。

`tools/build_sample_music.py` 用普通表描述三段旋律、速度与音色，共用一个有界
PCM 合成/编码流程。离线重建需要 Python 标准库和 ffmpeg；`-fflags +bitexact`
使同一编码环境的三份 Ogg 逐字节重建一致。发行游戏不使用 Python 或 ffmpeg，
不同编码器版本不承诺字节一致。重建命令：

```powershell
python tools/build_sample_music.py wayfarer
python tools/build_sample_music.py crossing
python tools/build_sample_music.py barrage
```

已执行 `python tests/sample_music.py build/full/shiny.exe`：三份素材重建字节相同，
真实无窗口宿主在各自 20/16/15 秒循环后保留一条活动音乐声部，位置进入新循环。
ffmpeg 解码的 Crossing/Barrage 首尾单采样幅度差分别为 9/36（16 位 PCM），
无显著接缝跳变。两项目 `--check-all` 通过；轻量引擎开发包包含与源码相同的
Ogg，包内无窗口跨循环运行通过。Crossing 渡台与 Barrage 第一波的隐藏、静音
原生截图已目视检查；Crossing 捕获的实体、状态、接触、RNG 和 watch 与旧素材
版本相同。真实扬声器听感、主观混音质量和最终样例验收仍未完成。
