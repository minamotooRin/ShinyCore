"""Bounded postprocess configuration; headless execution does not validate pixels."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
with tempfile.TemporaryDirectory(prefix='shiny-postprocess-') as directory:
    project=Path(directory)
    shutil.copytree(root/'examples'/'postprocess',project,dirs_exist_ok=True)
    def run(source=None,frames=1,replay=None):
        if source is not None: (project/'main.lua').write_text(source,encoding='utf-8')
        arguments=[str(binary),str(project),'--headless','--frames',str(frames)]
        if replay: arguments+=['--replay',str(project/replay),'--trace',str(project/'trace.jsonl')]
        return subprocess.run(arguments,capture_output=True,text=True,encoding='utf-8',timeout=15)
    if not api['modules']['postprocessing']:
        result=run('return {}')
        assert result.returncode and 'required module unavailable: postprocessing' in result.stderr
        print('postprocess: disabled-module requirement rejected');raise SystemExit(0)
    result=run(frames=10,replay='smoke.jsonl');assert result.returncode==0,result.stderr
    state=json.loads(result.stdout)
    assert state['watches']['mode']=='Bloom + grade'
    assert len(state['watches']['pipeline']['passes'])==4 and state['watches']['pipeline']['status']=='pending'
    frames=[json.loads(line)['watches'] for line in (project/'trace.jsonl').read_text(encoding='utf-8').splitlines()]
    assert [(frames[i]['mode'],len(frames[i]['pipeline']['passes'])) for i in (0,2,4,6,8)]==[
        ('Bloom',3),('Grade',1),('Warp',1),('Off',0),('Bloom + grade',4)]
    result=run('''local pass,image
local function fails(fn) assert(not pcall(fn)) end
return {width=480,height=270,init=function()
assert(sc.material.pipeline().status=="disabled")
pass=sc.material.create{shader="bloom_y",postprocess=true}
image=sc.material.create{shader="bloom_y"}
assert(sc.material.info(pass).postprocess and not sc.material.info(image).postprocess)
fails(function() sc.material.create{shader="bloom_y",postprocess="yes"} end)
fails(function() sc.material.create{shader="bloom_y",uniforms={sc_scene={type="texture",value="keeper"}}} end)
local textures={};for i=1,4 do textures["t"..i]={type="texture",value="keeper"} end
fails(function() sc.material.create{shader="bloom_y",postprocess=true,uniforms=textures} end)
local bytes=480*270*4
sc.material.postprocess({pass},bytes)
assert(sc.material.pipeline().required_color_bytes==bytes)
sc.material.postprocess({pass,pass,pass,pass},bytes*2)
local before=sc.material.pipeline()
assert(#before.passes==4 and before.required_color_bytes==bytes*2)
assert(before.target_count==0 and before.allocated_color_bytes==0 and before.status=="pending")
for _,fn in ipairs({
 function() sc.material.postprocess({pass,pass,pass,pass,pass}) end,
 function() sc.material.postprocess({pass,image}) end,
 function() sc.material.postprocess({pass,pass+100}) end,
 function() sc.material.postprocess({pass},bytes-1) end,
 function() sc.material.postprocess({pass},"1024") end,
 function() sc.material.postprocess({pass},1.5) end,
 function() sc.material.postprocess({pass},-1) end,
 function() sc.material.postprocess({pass},1073741825) end,
 function() sc.material.postprocess({[2]=pass}) end,
 function() sc.material.postprocess({pass,extra=pass}) end,
 function() sc.material.postprocess(setmetatable({pass},{__len=function() error("no metamethod") end})) end,
 function() sc.material.destroy(pass) end}) do
 fails(fn)
 local after=sc.material.pipeline()
 assert(after.revision==before.revision and #after.passes==4 and after.budget_bytes==before.budget_bytes)
end
local copy=sc.material.pipeline();copy.passes[1]=0
assert(sc.material.pipeline().passes[1]==pass)
end,update=function()
sc.material.postprocess({},0)
assert(sc.material.pipeline().status=="disabled" and sc.material.pipeline().required_color_bytes==0)
sc.material.destroy(pass)
fails(function() sc.material.postprocess({pass},1073741824) end)
pass=sc.material.create{shader="bloom_y",postprocess=true}
sc.material.postprocess({pass},480*270*4)
end,draw=function()
fails(function() sc.image("keeper",0,0,32,32,{material=pass}) end)
fails(function() sc.material.postprocess({}) end)
end}''')
    assert result.returncode==0,result.stderr
print('postprocess: pass order/limits, budgets, atomic rejection, retained handles, phases and sample playback passed')
