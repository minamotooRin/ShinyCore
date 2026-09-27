# Crossing 动作音效检查 · 2026-09-27

新增 jump、land、switch、error、rescue 五个本地单声道 PCM 音效，共 49,172 字节。
`tools/build_crossing_audio.py` 仅用 Python 标准库生成，重建结果逐字节一致。
它们只在动作发生时提交 `sc.audio.play`；初次落地不播放 land，回营清空落地状态。
现有 chime 和持久 theme 保留。音效不进入存档或玩法随机数。

定向无窗口回放观察到跳跃帧只出现 jump，落地后出现 land，主动回营出现
rescue，渡槽拉杆成功时出现 switch；跳过首个信号后误操作返回 error。完整
2,876 帧三关回放的 entities、contacts、
state、watches、settings、projectiles 和 `state_hash=85fde75661bf3fcc` 与修改前一致；
结局仍完成。`--check-all` 通过。生成的开发发行目录包含全部五个 WAV，移位后的
项目 `--check-all` 通过。

使用 `tools/capture_samples.py` 隐藏、静音捕获并实际检查了
`build/crossing-audio-reviewed/crossing-aqueduct.png`：渡台、角色、机关提示、
HUD 和目标文字均可见，没有新遮挡。本轮未试听实体音频设备，也未评判最终混音、
完整游戏美术或人工游玩时长；该开发包不是最终发行验收。
