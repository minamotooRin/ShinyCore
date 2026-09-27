"""Absolute Unicode CLI paths must survive Windows argv and filesystem conversion."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="shiny-paths-") as directory:
    root=Path(directory).resolve()/"项目 空格 🌙"
    root.mkdir();project=root/"游戏";project.mkdir()
    (project/"project.lua").write_text('return {id="path_probe",entry="main.lua"}',encoding="utf-8")
    (project/"main.lua").write_text('''return {update=function()
 if sc.tick()==0 then
  sc.state.set("value",{text="路径",input=sc.input.key_pressed("space")})
  assert(sc.save.write("checkpoint"))
  assert(sc.save.read("checkpoint").state.value.text=="路径")
  assert(sc.settings.apply({volume={master=.25}}))
 end
end}
''',encoding="utf-8")
    replay=root/"输入.jsonl"
    replay.write_text('{"version":3}\n{"frame":0,"keys":["space"],"gamepad":{"connected":false}}\n',encoding="utf-8")
    def run(*args):
        result=subprocess.run([str(binary),str(project),"--headless","--frames","2",*map(str,args)],
                              cwd=directory,capture_output=True,text=True,encoding="utf-8",timeout=12)
        assert result.returncode==0,result.stderr
        return json.loads(result.stdout)
    snapshot=root/"快照.json";trace=root/"轨迹.jsonl";profile=root/"采样.jsonl"
    state=run("--replay",replay,"--save-dir",root/"存档","--snapshot",snapshot,
              "--trace",trace,"--profile",profile)
    assert state["state"]["value"]=={"text":"路径","input":True},state["state"]
    assert json.loads(snapshot.read_text(encoding="utf-8"))["state"]==state["state"]
    for path in [trace,profile]:
        lines=path.read_text(encoding="utf-8").splitlines();assert lines
        for line in lines:json.loads(line)
    assert list((root/"存档").rglob("*.json")),"save/settings files missing"
    recording=root/"录制.jsonl"
    run("--record",recording,"--save-dir",root/"另一个存档")
    assert json.loads(recording.read_text(encoding="utf-8").splitlines()[0])["version"]==3
print("Unicode project/replay/save/settings/snapshot/trace/profile/record paths passed")
