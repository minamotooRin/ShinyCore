"""Material content and Lua contracts; never creates a graphics context."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parents[1]
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
with tempfile.TemporaryDirectory(prefix='shiny-material-') as directory:
    project=Path(directory)
    shutil.copytree(root/'examples'/'materials',project,dirs_exist_ok=True)
    def run(source,success=True,frames=1):
        (project/'main.lua').write_text(source,encoding='utf-8')
        result=subprocess.run([str(binary),str(project),'--headless','--frames',str(frames)],capture_output=True,text=True,encoding='utf-8',timeout=15)
        assert (result.returncode==0)==success,result.stderr
        return result
    if not api['modules']['materials']:
        result=run('return {}',False)
        assert 'required module unavailable: materials' in result.stderr
        assert not any(f['name'].startswith('sc.material.') for f in api['functions'])
        print('materials: disabled module rejected');raise SystemExit(0)
    run('''local id
local function fails(fn) assert(not pcall(fn)) end
return {init=function()
assert(sc.material.capacity().used==0)
local spec={shader="tint",uniforms={strength={type="float",value=.25},tint={type="vec3",value={1,.5,0}}}}
id=sc.material.create(spec)
assert(math.type(id)=="integer" and sc.material.info(id).status=="pending")
assert(sc.material.info(id).compiled_revision==0)
sc.material.set(id,{strength=.75,tint={0,1,0}})
fails(function() sc.material.set(id,{strength=.9,tint={1,2}}) end)
assert(sc.material.info(id).uniforms.strength==.75)
fails(function() sc.material.set(id,{unknown=1}) end)
fails(function() sc.material.create{shader="keeper"} end)
fails(function() sc.material.create{shader="tint",uniforms={texture0={type="texture",value="keeper"}}} end)
fails(function() sc.material.create{shader="tint",uniforms={v={type="bool",value=1}}} end)
fails(function() sc.material.create{shader="tint",uniforms={v={type="int",value=2147483648}}} end)
fails(function() sc.material.create{shader="tint",uniforms={v={type="vec2",value={0/0,1}}}} end)
local five={}; for i=1,5 do five["t"..i]={type="texture",value="keeper"} end
fails(function() sc.material.create{shader="tint",uniforms=five} end)
local many={};for i=1,33 do many["u"..i]={type="float",value=0} end
fails(function() sc.material.create{shader="tint",uniforms=many} end)
local extra=sc.material.create{shader="tint",uniforms={flag={type="bool",value=true},n={type="int",value=-17},map={type="texture",value="keeper"}}}
assert(sc.material.info(extra).uniforms.flag==true)
assert(sc.material.info(extra).uniforms.map=="assets/keeper.png")
sc.material.destroy(extra)
fails(function() sc.material.info(extra) end)
local replacement=sc.material.create(spec);assert(replacement~=extra)
local ids={replacement};for i=1,62 do ids[#ids+1]=sc.material.create(spec) end
assert(sc.material.capacity().used==64)
fails(function() sc.material.create(spec) end)
for _,v in ipairs(ids) do sc.material.destroy(v) end
assert(sc.material.capacity().used==1)
end,update=function()
sc.material.reload(id);assert(sc.material.info(id).revision==2)
end,draw=function()
sc.image("keeper",0,0,32,32,{material=id})
assert(not pcall(sc.material.set,id,{strength=1}))
assert(not pcall(sc.image,"keeper",0,0,32,32,{material=id+1}))
end}''')
    # Room references are intentionally not persistent material handles.
    (project/'second.lua').write_text('''return {init=function()
local old=sc.state.get("material")
local current=sc.material.create{shader="tint"}
assert(current~=old and not pcall(sc.material.info,old))
end}''',encoding='utf-8')
    run('''return {init=function()
sc.state.set("material",sc.material.create{shader="tint"})
end,update=function() sc.scene("second.lua") end}''',frames=2)
    if api['modules']['devtools']:
        (project/'main.lua').write_text('''local id
return {init=function() id=sc.material.create{shader="tint"} end,
update=function()
local ok,message=pcall(sc.material.reload,id)
sc.debug.watch("reload",{ok=ok,error=tostring(message),revision=sc.material.info(id).revision})
end}''',encoding='utf-8')
        process=subprocess.Popen([str(binary),str(project),'--headless','--debug-stdio','--frames','3'],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        def command(name,**fields):
            process.stdin.write(json.dumps({'id':1,'command':name,**fields})+'\n');process.stdin.flush()
            response=json.loads(process.stdout.readline());assert response['ok'],response
            return response
        try:
            assert json.loads(process.stdout.readline())['event']=='ready'
            original=(project/'tint.frag').read_bytes();(project/'tint.frag').write_bytes(b'\0broken')
            command('step');assert json.loads(process.stdout.readline())['event']=='stopped'
            state=command('watches',path=['reload'])['result']['items']
            state={item['key']:item['value'] for item in state}
            assert not state['ok'] and state['revision']==1 and 'without NUL' in state['error']
            (project/'tint.frag').write_bytes(original)
            command('step');assert json.loads(process.stdout.readline())['event']=='stopped'
            state=command('watches',path=['reload'])['result']['items']
            state={item['key']:item['value'] for item in state}
            assert state['ok'] and state['revision']==2
            command('quit');assert json.loads(process.stdout.readline())['event']=='terminated'
            assert process.wait(timeout=10)==0,process.stderr.read()
        finally:
            if process.poll() is None: process.kill();process.wait(timeout=10)
            process.stdin.close();process.stdout.close();process.stderr.close()
    (project/'tint.frag').write_bytes(b'\x00broken')
    assert 'without NUL' in run('return {}',False).stderr
print('materials: typed atomic values, stale/room handles, capacity, draw phase and source checks passed')
