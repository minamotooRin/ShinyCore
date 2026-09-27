"""Wayfarer local clocks and bounded feedback do not own gameplay state."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
binary=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="shiny-wayfarer-presentation-") as d:
    p=Path(d);(p/"lib/shiny").mkdir(parents=True)
    for name in ["presentation.lua","quest.lua"]:
        shutil.copyfile(root/"examples/wayfarer"/name,p/name)
    shutil.copyfile(root/"examples/wayfarer/lib/shiny/animation.lua",p/"lib/shiny/animation.lua")
    (p/"main.lua").write_text('''local V=require("presentation");local Q=require("quest")
local a,b=V.actor(),V.actor()
local pose=V.animate(a,-1,0,.125);assert(pose.frame==2 and pose.flip_x)
pose=V.animate(a,0,1,0);assert(pose.frame==2 and pose.flip_x)
pose=V.animate(b,1,0,0);assert(pose.frame==1 and not pose.flip_x)
pose=V.animate(a,0,0,.1);assert(pose.frame==0 and pose.flip_x)
pose=V.animate(a,1,0,0);assert(pose.frame==1 and not pose.flip_x)
local f=V.feedback()
for i=1,12 do V.pickup(f,i,10) end
assert(#f.items==8 and f.items[1].x==5 and f.count==12)
V.advance(f,.5);assert(#f.items==8 and f.remaining==.75)
V.advance(f,.3);assert(#f.items==0)
V.advance(f,.5);assert(f.remaining==0)
V.pickup(f,9,9);assert(f.count==1 and #f.items==1)
local p={x=100,y=100}
assert(Q.in_reach(p,{x=119,y=119},Q.equipment.trail.reach))
assert(not Q.in_reach(p,{x=120,y=100},Q.equipment.trail.reach))
assert(Q.in_reach(p,{x=120,y=100},Q.equipment.field.reach))
assert(not Q.in_reach(p,{x=100,y=132},Q.equipment.field.reach))
return {init=function() sc.debug.watch("presentation",true) end}
''',encoding="utf-8")
    r=subprocess.run([str(binary),str(p),"--headless","--frames","0"],capture_output=True,text=True,encoding="utf-8",timeout=15)
    assert r.returncode==0,r.stderr
    assert json.loads(r.stdout)["watches"]["presentation"]
print("Wayfarer presentation: independent clocks, facing, bounded pickup feedback and equipment reach passed")
