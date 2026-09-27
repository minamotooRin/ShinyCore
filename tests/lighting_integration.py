"""Only shadow authoring/boundary checks; native pixel behavior remains unverified."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
with tempfile.TemporaryDirectory(prefix='shiny-lighting-') as directory:
    project=Path(directory)
    shutil.copytree(root/'examples/geometry_shadows',project,dirs_exist_ok=True)
    def run(frames=1,replay=False):
        args=[str(binary),str(project),'--headless','--frames',str(frames)]
        if replay: args+=['--replay',str(project/'smoke.jsonl'),'--trace',str(project/'trace.jsonl')]
        return subprocess.run(args,capture_output=True,text=True,encoding='utf-8',timeout=15)
    if not api['modules']['geometry_shadows']:
        assert not any(f['name'].startswith('sc.lighting.') for f in api['functions'])
        result=run();assert result.returncode and 'required module unavailable: geometry_shadows' in result.stderr
        print('lighting: disabled-module capability and API omission passed');raise SystemExit(0)
    result=run(6,True);assert result.returncode==0,result.stderr
    states=[json.loads(line)['watches'] for line in (project/'trace.jsonl').read_text(encoding='utf-8').splitlines()]
    assert states[0]['lighting']['softness']==8 and states[0]['lighting']['effective_samples']==4
    assert states[4]['lighting']['softness']==0 and states[4]['lighting']['effective_samples']==1
    (project/'main.lua').write_text('''local updated=false
local function fail(fn) assert(not pcall(fn)) end
return {init=function()
local initial=sc.lighting.stats()
assert(initial.status=="pending" and initial.softness==0 and initial.samples==1)
assert(initial.occluders==0 and initial.lights==0 and initial.occluder_capacity==8192 and initial.light_capacity==32)
sc.lighting.configure{softness=8,samples=4}
for _,bad in ipairs({{softness=-1},{softness=33},{samples=0},{samples=9},{samples=1.5},
 {samples="4"},{softness=1/0},{softness=0/0},{softness=4,unknown=2},{softness=1,samples=9},
 {true},setmetatable({softness=2},{__index=function() error("must not run") end})}) do
 fail(function() sc.lighting.configure(bad) end)
 local unchanged=sc.lighting.stats();assert(unchanged.softness==8 and unchanged.samples==4)
end
sc.lighting.configure{samples=8};assert(sc.lighting.stats().softness==8)
sc.lighting.configure{softness=0};assert(sc.lighting.stats().effective_samples==1)
end,update=function() sc.lighting.configure{softness=32,samples=8};updated=true end,
draw=function() fail(function() sc.lighting.configure{samples=2} end)
local state=sc.lighting.stats();assert(state.softness==(updated and 32 or 0) and state.samples==8 and state.status=="pending") end}
''',encoding='utf-8')
    result=run();assert result.returncode==0,result.stderr
    point_api=next(f for f in api['functions'] if f['name']=='sc.lighting.point')
    assert point_api['contract']['phases']==['draw']
    fields={f['name']:f for f in api['types']['ScPointLight']['fields']}
    assert fields['x']['required'] and fields['y']['required'] and fields['radius']['default']==128
    assert fields['shadows']['default'] is True and fields['intensity']['maximum']==1
    (project/'main.lua').write_text('''local entity,stale
local function fail(fn) assert(not pcall(fn)) end
return {init=function()
fail(function() sc.lighting.point{x=0,y=0} end)
entity=sc.spawn{x=20,y=20,w=5,h=5}
stale=sc.spawn{x=30,y=20,w=5,h=5};sc.destroy(stale)
sc.lighting.configure{softness=4,samples=4}
end,update=function() fail(function() sc.lighting.point{x=0,y=0} end) end,
draw=function()
assert(sc.lighting.stats().point_commands==0)
for _,bad in ipairs({{}, {x=0}, {x=0,y=0,radius=0}, {x=0,y=0,radius=1025},
 {x=0,y=0,intensity=2}, {x=0,y=0,softness=33}, {x=0,y=0,samples=2.5},
 {x=0,y=0,shadows=1}, {x=0,y=0,color="#FF00GG"}, {x=0,y=0,color="#FFF"},
 {x=0,y=0,ignore=stale}, {x=0,y=0,ignore="0"}, {x=0,y=0,unknown=true},
 {x=1/0,y=0}, {x=0,y=0,intensity="1"},setmetatable({x=0,y=0},{})}) do
 fail(function() sc.lighting.point(bad) end);assert(sc.lighting.stats().point_commands==0)
end
sc.lighting.point{x=20,y=20,ignore=entity,shadows=true,color="#00FF0080",samples=8}
sc.lighting.point{x=50,y=20,ignore=0,shadows=false,color="#FF0000"}
for i=3,32 do sc.lighting.point{x=50,y=50,shadows=false} end
assert(sc.lighting.stats().point_commands==32)
fail(function() sc.lighting.point{x=50,y=50} end)
assert(sc.lighting.stats().point_commands==32)
end}
''',encoding='utf-8')
    result=run(3);assert result.returncode==0,result.stderr
    (project/'main.lua').write_text('return {draw=function() for i=1,17 do sc.lighting.point{x=50,y=50} end end}',encoding='utf-8')
    result=run();assert result.returncode and 'visible shadow light capacity exceeded' in result.stderr
    (project/'main.lua').write_text('return {init=function() sc.spawn{x=50,y=50,w=2,h=2,glow=100} end,draw=function() for i=1,32 do sc.lighting.point{x=50,y=50,shadows=false} end end}',encoding='utf-8')
    result=run();assert result.returncode and 'visible light capacity exceeded' in result.stderr
    (project/'project.lua').write_text('return {id="test.occluders",modules={"geometry_shadows"},limits={entities=2}}',encoding='utf-8')
    (project/'next.lua').write_text('''return {init=function()
assert(sc.lighting.stats().occluder_overrides==0)
local id=sc.spawn{x=10,y=10,w=8,h=8};assert(sc.lighting.occluder_mode(id)=="body")
sc.debug.watch("occluder_reset",true)
end}''',encoding='utf-8')
    (project/'main.lua').write_text('''local id
local function fail(fn) assert(not pcall(fn)) end
return {init=function()
assert(sc.lighting.stats().override_capacity==2)
id=sc.spawn{x=10,y=10,w=8,h=8}
assert(sc.lighting.occluder_mode(id)=="body")
for _,mode in ipairs({"bounds","shape","none","body"}) do
 sc.lighting.occluder(id,mode);assert(sc.lighting.occluder_mode(id)==mode)
end
sc.lighting.occluder(id,"bounds")
for _,mode in ipairs({false,1,"unknown","none\\0extra"}) do
 fail(function() sc.lighting.occluder(id,mode) end)
 assert(sc.lighting.occluder_mode(id)=="bounds")
end
fail(function() sc.lighting.occluder(tostring(id),"none") end)
assert(sc.lighting.stats().occluder_overrides==1 and sc.get(id).body==false)
local old=id;sc.destroy(id)
assert(sc.lighting.stats().occluder_overrides==0)
fail(function() sc.lighting.occluder(old,"none") end)
fail(function() sc.lighting.occluder_mode(old) end)
id=sc.spawn{x=10,y=10,w=8,h=8};assert(id~=old and sc.lighting.occluder_mode(id)=="body")
sc.lighting.occluder(id,"bounds")
end,draw=function()
local mode=sc.lighting.occluder_mode(id);assert(mode=="bounds" or mode=="shape")
fail(function() sc.lighting.occluder(id,"none") end)
end,update=function()
sc.lighting.occluder(id,"shape");assert(sc.lighting.occluder_mode(id)=="shape")
sc.scene("next.lua")
end}''',encoding='utf-8')
    result=run(2);assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['watches']['occluder_reset'] is True
print('lighting: settings, point budgets, visual occluders, stale/reused handles, room reset and replay passed')
