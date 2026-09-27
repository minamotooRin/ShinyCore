"""Bounded camera contract checks; never opens a native window."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
api = json.loads(subprocess.check_output([str(binary), '--api'], encoding='utf-8'))
functions = {entry['name']: entry for entry in api['functions']}
assert functions['sc.camera.set']['contract']['phases'] == ['load', 'init', 'update']
assert 'draw' in functions['sc.camera.to_world']['contract']['phases']
assert 'sc.camera' not in functions
with tempfile.TemporaryDirectory(prefix='shiny-camera-') as directory:
    project = Path(directory)
    (project/'project.lua').write_text('return {id="camera.test",display={width=400,height=200}}', encoding='utf-8')
    (project/'main.lua').write_text('''local target,updated
local function near(a,b) assert(math.abs(a-b)<.001,tostring(a).." ~= "..tostring(b)) end
local function fail(fn) assert(not pcall(fn)) end
return {width=400,height=200,init=function()
assert(sc.camera.read().zoom==1)
sc.camera.set{bounds=false,x=10.5,y=20.25,pixel_snap=false,zoom=2,rotation=math.pi/2}
local p=sc.camera.to_screen(220.5,120.25);near(p.x,200);near(p.y,120)
local q=sc.camera.to_world(p.x,p.y);near(q.x,220.5);near(q.y,120.25)
for _,bad in ipairs({{zoom=0},{zoom=9},{rotation=1/0},{x="1"},{pixel_snap=1},{unknown=true},
 {x=4,zoom=0},{bounds=true},{bounds="invalid"},{bounds={x=0,y=0,w=-1,h=1}},
 {bounds={x=999999,y=0,w=2,h=1}},{bounds={x=0,y=0,w=1,h=1,extra=0}},
 setmetatable({zoom=1},{__index=function() error("metamethod") end})}) do
 fail(function() sc.camera.set(bad) end);near(sc.camera.read().x,10.5);near(sc.camera.read().zoom,2)
end
sc.camera.set{bounds={x=0,y=0,w=500,h=400},x=10000,y=-10000}
local v=sc.camera.read().visible;near(v.x+v.w,500);near(v.y,0)
sc.camera.set{bounds=false,zoom=1,rotation=0,smoothing=1}
target=sc.spawn{x=300,y=100,w=20,h=20}
sc.camera.follow(target);near(sc.camera.read().x,110);near(sc.camera.read().y,10)
fail(function() sc.camera.follow("1") end)
sc.destroy(target);fail(function() sc.camera.follow(target) end)
assert(sc.camera.read().target==0)
target=sc.spawn{x=300,y=100,w=20,h=20};sc.camera.follow(target)
sc.camera.shake(8,.1,42)
fail(function() sc.camera.shake(1,1,-1) end)
end,update=function()
 if not updated then sc.set(target,{x=400});updated=true
 else near(sc.camera.read().x,210);sc.camera.follow(nil);sc.camera.shake(0,0);sc.scene("next.lua") end
end,draw=function()
fail(function() sc.camera.set{x=0} end)
fail(function() sc.camera.follow(nil) end)
fail(function() sc.camera.shake(0,0) end)
local mx,my=sc.input.mouse();local world=sc.camera.to_world(mx,my)
local back=sc.camera.to_screen(world.x,world.y);near(back.x,mx);near(back.y,my)
end}
''', encoding='utf-8')
    (project/'next.lua').write_text('''return {width=400,height=200,init=function()
local c=sc.camera.read();assert(c.zoom==1 and c.rotation==0 and c.pixel_snap and c.bounds=="map" and c.target==0)
print("camera room reset passed") end}
''', encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','3'],capture_output=True,text=True,encoding='utf-8',timeout=15)
    assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['scene']=='next.lua',result.stdout
print('camera Lua atomic validation, conversions, phases and room reset passed')
