"""CPU material binding/lifetime contracts; no window or shader-driver assertions."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
if not api['modules']['materials']:
    assert not any(f['name'].startswith('sc.material.') for f in api['functions'])
    print('material bindings: disabled API omitted');raise SystemExit(0)
functions={f['name']:f for f in api['functions']}
assert functions['sc.material.bind_entity']['contract']['phases']==['load','init','update']
assert 'draw' in functions['sc.material.entity_material']['contract']['phases']
with tempfile.TemporaryDirectory(prefix='shiny-material-bindings-') as directory:
    project=Path(directory)
    shutil.copytree(root/'examples/materials',project,dirs_exist_ok=True)
    (project/'project.lua').write_text('''return {id="material.bindings",modules={"materials"},limits={entities=2},resources={
keeper={type="image",path="assets/keeper.png"},alias={type="image",path="assets/keeper.png"},tint={type="shader",path="tint.frag"}}}
''',encoding='utf-8')
    (project/'main.lua').write_text('''local m,other,post,e,dead
local function fails(fn) assert(not pcall(fn)) end
local function material(post) return sc.material.create{shader="tint",postprocess=post or false,
uniforms={strength={type="float",value=.5},tint={type="vec3",value={1,.5,0}}}} end
return {init=function()
assert(sc.material.image_material("keeper")==0)
m=material();other=material();post=material(true)
e=sc.spawn{sprite="keeper"};dead=sc.spawn{sprite="alias"}
sc.material.bind_image("keeper",m)
assert(sc.material.image_material("alias")==m and sc.material.entity_material(e)==m)
assert(sc.material.capacity().image_bindings==1 and sc.material.capacity().entity_binding_capacity==2)
fails(function() sc.material.destroy(m) end)
sc.material.bind_entity(e,other)
assert(sc.material.entity_material(e)==other and sc.material.entity_material(dead)==m)
fails(function() sc.material.destroy(other) end)
for _,bad in ipairs({post,m+65536,true,-1,1.5,"0"}) do
 fails(function() sc.material.bind_entity(e,bad) end)
 assert(sc.material.entity_material(e)==other)
 fails(function() sc.material.bind_image("keeper",bad) end)
 assert(sc.material.image_material("keeper")==m)
end
fails(function() sc.material.bind_image("tint",m) end)
fails(function() sc.material.bind_image("keeper"..string.char(0),m) end)
sc.material.bind_entity(e,false);assert(sc.material.entity_material(e)==0)
sc.material.bind_entity(e,0);assert(sc.material.entity_material(e)==m)
sc.material.bind_entity(dead,other);sc.destroy(dead)
fails(function() sc.material.bind_entity(dead,m) end)
fails(function() sc.material.entity_material(dead) end)
local reused=sc.spawn{sprite="keeper"};assert(reused~=dead)
assert(sc.material.entity_material(reused)==m and sc.material.capacity().entity_bindings==0)
sc.material.destroy(other) -- A dead generation cannot retain its material.
sc.material.bind_image("alias",0)
assert(sc.material.image_material("keeper")==0 and sc.material.capacity().image_bindings==0)
sc.material.bind_entity(e,m)
sc.state.set("old_entity",e);sc.state.set("old_material",m)
end,update=function() if sc.tick()==1 then sc.scene("next.lua") end end,draw=function()
fails(function() sc.material.bind_entity(e,0) end)
fails(function() sc.material.bind_image("keeper",0) end)
sc.image("keeper",0,0,12,18,{material=false})
sc.image("keeper",16,0,12,18,{material=m})
fails(function() sc.image("keeper",0,0,12,18,{material=true}) end)
end}
''',encoding='utf-8')
    (project/'next.lua').write_text('''return {init=function()
assert(sc.material.image_material("keeper")==0 and sc.material.capacity().entity_bindings==0)
assert(not pcall(sc.material.entity_material,sc.state.get("old_entity")))
local m=sc.material.create{shader="tint",uniforms={strength={type="float",value=1},tint={type="vec3",value={1,1,1}}}}
local e=sc.spawn{sprite="keeper"};assert(sc.material.entity_material(e)==0)
assert(not pcall(sc.material.bind_entity,e,sc.state.get("old_material")))
sc.material.bind_entity(e,m);sc.material.bind_entity(e,false);sc.material.destroy(m)
assert(sc.material.entity_material(e)==0)
end}
''',encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','3'],capture_output=True,text=True,encoding='utf-8',timeout=15)
    assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['scene']=='next.lua'
print('material bindings: aliases, overrides, atomic errors, references, generations and room reset passed')
