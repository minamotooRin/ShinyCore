"""Normal-map resource and Lua contracts; does not claim GPU or pixel verification."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
with tempfile.TemporaryDirectory(prefix='shiny-normals-') as directory:
    project=Path(directory)
    shutil.copytree(root/'examples/normal_maps',project,dirs_exist_ok=True)
    def run(frames=1,replay=False):
        args=[str(binary),str(project),'--headless','--frames',str(frames)]
        if replay: args+=['--replay',str(project/'smoke.jsonl'),'--trace',str(project/'trace.jsonl')]
        return subprocess.run(args,capture_output=True,text=True,encoding='utf-8',timeout=15)
    if not api['modules']['normal_maps']:
        result=run();assert result.returncode and 'required module unavailable: normal_maps' in result.stderr
        assert not any(f['name']=='sc.lighting.normal' for f in api['functions'])
        print('normal maps: disabled-module capability and API omission passed');raise SystemExit(0)
    result=run(4,True);assert result.returncode==0,result.stderr
    trace=[json.loads(line)['watches'] for line in (project/'trace.jsonl').read_text(encoding='utf-8').splitlines()]
    assert [row['height'] for row in trace]==[128,128,48,48]
    assert all(row['normal_maps']==1 for row in trace)
    normal_api=next(f for f in api['functions'] if f['name']=='sc.lighting.normal')
    assert normal_api['contract']['phases']==['load','init']
    fields={f['name']:f for f in api['types']['ScPointLight']['fields']}
    assert fields['height']['default']==32 and fields['height']['minimum']==.001 and fields['height']['maximum']==4096
    # Reuse a checked-in differently sized PNG; never modify source assets.
    shutil.copyfile(root/'examples/materials/assets/keeper.png',project/'small.png')
    (project/'project.lua').write_text('''return {id="test.normals",modules={"normal_maps"},resources={
atlas={type="image",path="atlas.png"},normal={type="image",path="normals.png"},
alias={type="image",path="atlas.png"},small={type="image",path="small.png"}}}
''',encoding='utf-8')
    (project/'main.lua').write_text('''local function fail(fn) assert(not pcall(fn)) end
return {init=function()
assert(sc.lighting.stats().normal_maps==0)
sc.lighting.normal("atlas","normal")
local s=sc.lighting.stats();assert(s.normal_maps==1 and s.normal_target_bytes==0 and s.normal_capacity==64 and s.normal_budget_bytes==67108864)
fail(function() sc.lighting.normal("atlas","small") end)
fail(function() sc.lighting.normal("atlas","absent") end)
fail(function() sc.lighting.normal(3,"normal") end)
fail(function() sc.lighting.normal("atlas",nil) end)
assert(sc.lighting.stats().normal_maps==1)
sc.lighting.normal("alias","atlas");assert(sc.lighting.stats().normal_maps==1)
sc.lighting.normal("atlas","normal")
end,update=function()
fail(function() sc.lighting.normal("atlas","atlas") end)
end,draw=function()
fail(function() sc.lighting.normal("atlas","atlas") end)
fail(function() sc.lighting.point{x=0,y=0,height=0} end)
fail(function() sc.lighting.point{x=0,y=0,height=4097} end)
sc.lighting.point{x=0,y=0,height=64}
assert(sc.lighting.stats().status=="pending" and sc.lighting.stats().normal_target_bytes==0)
sc.image("atlas",0,0,128,64)
end}
''',encoding='utf-8')
    result=run(2);assert result.returncode==0,result.stderr
print('normal maps: dimension matching, atomic rebinding, phases, height, metadata and sample replay passed')
