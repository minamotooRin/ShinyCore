"""Crossing presentation follows actual campaign progress and local animation."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
binary=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='shiny-crossing-presentation-') as d:
    p=Path(d);(p/'lib/shiny').mkdir(parents=True)
    for name in ['presentation.lua','campaign.lua']:shutil.copyfile(root/'examples/crossing'/name,p/name)
    shutil.copyfile(root/'examples/crossing/lib/shiny/animation.lua',p/'lib/shiny/animation.lua')
    (p/'main.lua').write_text('''local V=require('presentation');local C=require('campaign')
local a=V.player();local ground={grounded=true,vy=0}
local frame,left=V.animate(a,ground,-1,0);assert(frame==1 and left)
frame,left=V.animate(a,ground,0,.1);assert(frame==0 and left and a.clock.name=='idle')
frame,left=V.animate(a,ground,1,.125);assert(frame==2 and not left)
frame,left=V.animate(a,ground,0,.01,true);assert(frame==2 and a.clock.name=='rise')
frame,left=V.animate(a,{grounded=false,vy=30},0,1);assert(frame==3 and a.clock.name=='fall')
assert(a.clock.done);frame=V.animate(a,ground,0,0);assert(frame==0)
local c=C.new();local stage=c.stages[1]
local _,state=V.control(1,stage,1);assert(state=='locked')
C.collect(stage,'light.1');C.collect(stage,'light.2');_,state=V.control(1,stage,1);assert(state=='ready')
C.interact(c,1,1);_,state=V.control(1,stage,1);assert(state=='done')
local signal=c.stages[3]
local function states(a,b,d)
 local _,x=V.control(3,signal,1);local _,y=V.control(3,signal,2);local _,z=V.control(3,signal,3)
 assert(x==a and y==b and z==d)
end
states('ready','locked','locked');C.interact(c,3,1);states('done','locked','ready')
C.interact(c,3,3);states('done','ready','done');C.interact(c,3,3);states('ready','locked','locked')
for _,i in ipairs(C.signal_order) do C.interact(c,3,i) end;states('done','done','done')
return {init=function() sc.debug.watch('presentation',true) end}
''',encoding='utf-8')
    r=subprocess.run([str(binary),str(p),'--headless','--frames','0'],capture_output=True,text=True,encoding='utf-8',timeout=15)
    assert r.returncode==0,r.stderr
    assert json.loads(r.stdout)['watches']['presentation']
print('Crossing presentation: facing, idle/walk/rise/fall, lever and ordered/reset signals passed')
