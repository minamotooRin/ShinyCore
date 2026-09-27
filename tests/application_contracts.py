"""Application/state/watch contracts, strict calls and settings failure isolation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
functions={f['name']:f for f in api['functions']}
read=['load','init','update','draw','ui_update']
mutate=['load','init','update']
for name,phases in {'sc.app.pause':mutate+['ui_update'],'sc.app.quit':mutate+['ui_update'],
                    'sc.app.paused':read,'sc.settings.get':read,'sc.settings.error':read,
                    'sc.settings.apply':['update'],'sc.state.get':read,'sc.state.set':mutate,
                    'sc.debug.watch':mutate,'require':read}.items():
    assert functions[name]['contract']['phases']==phases,(name,functions[name])
assert functions['sc.settings.apply']['contract']['parameters'][1]['default'] is True
for name,patch in [('ScSettings',False),('ScSettingsPatch',True),('ScVolume',False),('ScVolumePatch',True)]:
    fields=api['types'][name]['fields']
    assert all(f['required']==(not patch) and f['readonly']==(not patch) for f in fields)
    assert api['types'][name]['unknown_fields']=='reject'
with tempfile.TemporaryDirectory(prefix='shiny-app-contract-') as directory:
    project=Path(directory)
    def write(name,text): (project/name).write_text(text,encoding='utf-8')
    write('project.lua',"return {id='app-contract',display={width=640,height=360,volume={music=.8}},limits={particles=0,projectiles=0}}")
    write('cache.lua','return {n=1}')
    write('empty.lua','return nil')
    write('later.lua','return 1')
    write('retry.lua',"if not sc.state.get('retry') then sc.state.set('retry',true);error('first load') end;return 7")
    write('main.lua',r"""local function reject(fn,...) assert(not pcall(fn,...)) end
local a,s,w=sc.app,sc.settings,sc.debug.watch
local cached=require('cache');assert(require('cache')==cached and require('empty')==true)
reject(require,'cache',nil);reject(require,1)
reject(require,'retry');assert(require('retry')==7)
local function reads()
 for _,fn in ipairs({a.paused,s.get,s.error}) do reject(fn,nil);reject(fn,1) end
 reject(sc.state.get,'key',nil);reject(sc.state.get,1)
 assert(require('cache')==cached)
end
local function no_mutation()
 reject(sc.state.set,'key',1);reject(w,'test',1);reject(s.apply,{})
end
reads();assert(not a.paused());reject(a.pause);reject(a.pause,1);reject(a.pause,true,nil)
reject(a.quit,nil);assert(not a.paused())
sc.state.set('key',{n=1});local value=sc.state.get('key');value.n=4
assert(sc.state.get('key').n==1)
reject(sc.state.set,'key',nil,1);reject(sc.state.set,'key');assert(sc.state.get('key').n==1)
reject(sc.state.set,'key'..string.char(0),5);reject(sc.state.set,'key',setmetatable({},{}))
sc.state.set('remove',1);sc.state.set('remove',nil);assert(sc.state.get('remove')==nil)
w('test',{n=1});reject(w,1,2);reject(w,'test'..string.char(0)..'suffix',2)
reject(w,'test',1,2);reject(w,'test');reject(w,'test',string.char(255))
reject(w,string.char(255),1);reject(w,'test',setmetatable({},{}))
for i=1,63 do w('slot'..i,nil) end
reject(w,'overflow',1);w('slot63',true)
return {gravity=0,init=function()
 reads();reject(s.apply,{});assert(s.get().width==640 and s.get().volume.music>.79)
end,ui_update=function()
 reads();no_mutation();reject(require,'later')
 a.pause(true);assert(a.paused());a.pause(false);assert(not a.paused())
end,update=function()
 reads()
 local before=s.get();before.volume.music=0;assert(s.get().volume.music>.79)
 reject(s.apply);reject(s.apply,{},false,1);reject(s.apply,{},1)
 assert(s.error()=='')
 local ok,err=s.apply({width='700'},false)
 assert(ok==nil and type(err)=='string' and s.error()==err and s.get().width==640)
 assert(s.apply({width=700,volume={sfx=.5},bindings={jump={'space'}}},false))
 local current=s.get();assert(current.width==700 and current.height==360 and current.volume.music>.79 and current.volume.sfx==.5)
 assert(s.error()=='')
 ok,err=s.apply({width=900,volume={ui=2}},false)
 assert(not ok and err and s.get().width==700 and s.get().volume.ui==1)
 assert(s.apply({bindings={dash={'j'}}},nil))
 assert(s.get().bindings.jump==nil and s.get().bindings.dash[1]=='j')
 -- Watch nil consumes a slot; overwrites still succeed at the limit.
 w('slot63',{passed=true});a.pause(true)
end,draw=function()
 reads();no_mutation();reject(a.pause,false);reject(a.quit);reject(require,'later')
 if sc.tick()>0 then assert(a.paused()) end
end}
""")
    result=subprocess.run([str(binary),str(project),'--headless','--frames','1','--save-dir',str(project/'data')],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    snapshot=json.loads(result.stdout)
    assert snapshot['watches']['test']=={'n':1} and snapshot['watches']['slot63']['passed']
    saved=json.loads((project/'data/app-contract/config/settings.json').read_text())['settings']
    assert saved['width']==700 and saved['bindings']=={'dash':['j']}
print('application: contracts, strict phases/arity, copy isolation, watch limits and settings transactions passed')
