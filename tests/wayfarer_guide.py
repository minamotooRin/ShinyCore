"""Quest guidance uses only supplied loaded objects and stable authored landmarks."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
binary=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="shiny-wayfarer-guide-") as d:
    p=Path(d);shutil.copytree(root/"examples/wayfarer/lib",p/"lib")
    for name in ["guide.lua","scenery.lua"]: shutil.copyfile(root/"examples/wayfarer"/name,p/name)
    (p/"main.lua").write_text('''local G=require("guide");local S=require("scenery")
local p={x=100,y=100};local herbs={{x=200,y=100},{x=80,y=90},{x=80,y=110}}
assert(G.target("meet",0,false,p,herbs).kind=="healer")
local t=G.target("gather",0,false,p,herbs);assert(t.x==80 and t.y==90)
t=G.target("gather",24,false,p,herbs);assert(t.kind=="road" and t.x==328)
t=G.target("gather",24,true,p,herbs);assert(t.kind=="healer")
assert(G.target("gather",23,true,p,{})==nil)
assert(G.target("complete",24,true,p,herbs)==nil)
assert(S.region(511,511)~=S.region(512,511))
assert(S.region(511,511)~=S.region(511,512))
assert(S.region(512,511)~=S.region(512,512))
return {init=function() sc.debug.watch("guide",true) end}
''',encoding="utf-8")
    r=subprocess.run([str(binary),str(p),"--headless","--frames","0"],capture_output=True,text=True,encoding="utf-8",timeout=15)
    assert r.returncode==0,r.stderr
    assert json.loads(r.stdout)["watches"]["guide"]
print("Wayfarer guide: quest phases, nearest loaded herb/tie, depleted region and district boundaries passed")
