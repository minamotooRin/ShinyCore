"""Run snapshot interpolation contracts inside the actual restricted game VM."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='shiny-snapshot-') as directory:
    project=Path(directory)
    shutil.copytree(root/'lua/shiny',project/'lib/shiny')
    (project/'main.lua').write_text('''local S=require('shiny.snapshot')
local b=S.new(3,4); local out=S.output(b)
local function push(seq,tick,objects) assert(S.push(b,seq,tick,objects)) end
local function sample(tick,count,status,x)
    local n,state=S.sample(b,tick,out); assert(n==count and state==status)
    if x then assert(math.abs(out[1].x-x)<1e-8) end
end
return {init=function()
    sample(0,0,'empty'); assert(S.bounds(b)==nil)
    local source={{id=1,x=0,y=4},{id=3,x=30,y=0}}
    push(0xfffffffe,6,source); source[1].x=900
    sample(0,2,'buffering',0)
    push(0xffffffff,9,{{id=1,x=6,y=10},{id=2,x=20,y=0}})
    sample(7.5,2,'interpolated',3); assert(out[1].y==7 and out[2].id==3 and out[2].x==30)
    sample(9,2,'held',6); assert(out[2].id==2)
    assert(not S.push(b,0xffffffff,12,{})) -- Duplicate sequence.
    assert(not S.push(b,0xfffffffe,12,{})) -- Late sequence.
    assert(not S.push(b,0x7fffffff,12,{})) -- Ambiguous half-range.
    assert(not S.push(b,0,9,{})) -- New sequence, stale server tick.
    push(0,12,{{id=1,x=12,y=16},{id=2,x=24,y=2}})
    sample(10.5,2,'interpolated',9)
    out[1].x=999; sample(10.5,2,'interpolated',9)
    push(1,15,{})
    local first,last=S.bounds(b); assert(first==9 and last==15)
    sample(14,2,'interpolated',12); sample(15,0,'held'); sample(1000,0,'held')
    local bad={
      {[2]={id=1,x=0,y=0}}, {{id=1,x=0/0,y=0}}, {{id=1,x=math.huge,y=0}},
      {{id=1,x=0,y=0},{id=1,x=0,y=0}}, {{id=2,x=0,y=0},{id=1,x=0,y=0}},
      {{id=1,x=0,y=0,extra=true}}, setmetatable({},{}),
      {{id=1,x=0,y=0},{id=2,x=0,y=0},{id=3,x=0,y=0},{id=4,x=0,y=0},{id=5,x=0,y=0}}
    }
    for _,objects in ipairs(bad) do
        assert(not S.push(b,2,18,objects)); local a,z=S.bounds(b); assert(a==9 and z==15)
    end
    assert(not pcall(S.new,1)); assert(not pcall(S.new,65)); assert(not pcall(S.new,3,0))
    assert(not pcall(S.sample,b,math.huge,out)); assert(not pcall(S.sample,b,0/0,out))
    S.reset(b); assert(S.bounds(b)==nil)
    push(1,0,{{id=1,x=2,y=3}}); sample(500,1,'held',2)
end,update=function()
    -- Sustained 20 Hz snapshots sampled at 60 Hz; only ring slots allocated at startup.
    local tick=sc.tick()
    if tick%3==0 then
        if tick==0 then S.reset(b) end
        push(tick,tick,{{id=1,x=tick*2,y=0}})
    end
    local at=math.max(0,tick-3)
    local n=S.sample(b,at,out); assert(n==1 and out[1].x==at*2)
    assert(b.count<=3)
    if tick==599 then sc.debug.watch('verified',true) end
end}''',encoding='utf-8')
    result=subprocess.run([str(binary),'--headless',str(project),'--frames','600'],
                          capture_output=True,text=True,encoding='utf-8',timeout=20)
    if result.returncode: raise AssertionError(result.stderr)
    assert json.loads(result.stdout)['watches']['verified']
print('snapshot: wrap, ordering, interpolation, membership, atomic validation and 600 ticks passed')

example=root/'examples/snapshot'
assert (example/'lib/shiny/snapshot.lua').read_bytes()==(root/'lua/shiny/snapshot.lua').read_bytes()
for frames,status in ((220,'held'),(300,'interpolated')):
    result=subprocess.run([str(binary),'--headless',str(example),'--frames',str(frames),
                           '--replay',str(example/'smoke.txt')],
                          capture_output=True,text=True,encoding='utf-8',timeout=20)
    if result.returncode: raise AssertionError(result.stderr)
    assert json.loads(result.stdout)['watches']['snapshot']['status']==status
print('snapshot example: outage holds and resumed snapshots interpolate')
