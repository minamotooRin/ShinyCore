"""Structured network contracts and named-session boundaries; no external endpoints."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
entries={f['name']:f['contract'] for f in api['functions'] if f['name'].startswith(('sc.net.','ScNetSession:'))}
assert len(entries)==15 and all(c and c['module']=='network' for c in entries.values())
read=['load','init','update','draw','ui_update'];mutate=['load','init','update']
for name,c in entries.items():
    readonly=name in ['sc.net.time','sc.net.bind','ScNetSession:state','ScNetSession:stats','ScNetSession:port','ScNetSession:rtt']
    assert c['phases']==(read if readonly else mutate),(name,c)
assert entries['ScNetSession:state']['mutation']=={'parameter':'value','when':'present','phases':mutate}
assert entries['sc.net.host']['parameters'][2]['default']==8
assert entries['ScNetSession:send']['parameters'][2]['default']=='reliable'
assert entries['ScNetSession:disconnect']['parameters'][1]['default']==0
stats={f['name']:f for f in api['types']['ScNetStats']['fields']}
assert set(stats)=={'queued','readable','receive_capacity','service_budget','receive_tick_budget','send_remaining','send_bytes_remaining','state_bytes','state_capacity','open','error'}
assert all(f['readonly'] and f['required']==(name!='error') for name,f in stats.items())
events={f['name']:f for f in api['types']['ScNetEvent']['fields']}
assert {name for name,f in events.items() if f['required']}=={'type','peer'} and events['data']['maximum_bytes']==1200
with tempfile.TemporaryDirectory(prefix='shiny-net-contract-') as directory:
    project=Path(directory)
    (project/'project.lua').write_text("return {modules={'network'},rooms={'main.lua','next.lua'},limits={particles=0,projectiles=0}}")
    (project/'main.lua').write_text(r"""local h
local function reject(fn,...) assert(not pcall(fn,...)) end
local function inspect()
 if not h then return end
 local copy=h:state();copy.held=false;assert(h:state().held)
 assert(h:port()>0 and h:stats().open and sc.net.time()>=0)
 assert(sc.net.bind('coop'):port()==h:port())
 reject(h.state,h,nil);reject(h.state,h,{});reject(h.poll,h);reject(h.send,h,0,'x')
 reject(h.close,h);reject(h.flush,h);reject(h.disconnect,h,1);reject(h.persist,h,'other')
 reject(sc.net.token);reject(sc.net.host,'127.0.0.1',0);reject(sc.net.join,'127.0.0.1',1)
end
return {init=function()
 local initial=assert(sc.net.host('127.0.0.1',0));initial:close();assert(#sc.net.token()==32)
 reject(sc.net.bind,1);reject(sc.net.bind,'coop'..string.char(0));reject(sc.net.bind,'coop',1)
 reject(sc.net.time,1);assert(sc.net.bind('missing')==nil)
end,update=function()
 if h then sc.scene('next.lua');return end
 h=assert(sc.net.host('127.0.0.1',0,nil))
 reject(h.persist,h,1);reject(h.persist,h,'coop'..string.char(0));assert(h:port()>0)
 assert(h:persist('coop'));assert(sc.net.bind('coop'):port()==h:port())
 local s=assert(h:stats());assert(s.queued==0 and s.readable==0 and s.receive_capacity==256)
 assert(s.service_budget==64 and s.receive_tick_budget==64 and s.send_remaining==64 and s.send_bytes_remaining==65536)
 assert(s.state_capacity==65536 and s.state_bytes==0 and s.open and s.error==nil)
 assert(h:state({held=true}));assert(h:state(nil));assert(h:state()==nil and h:stats().state_bytes==0)
 assert(h:state({held=true}));local ok,err=h:state({0,1});assert(not ok and err and h:state().held)
 reject(h.state,h,nil,1);assert(h:state().held)
 local old=assert(sc.net.bind('coop'));h:close()
 h=assert(sc.net.host('127.0.0.1',0));assert(h:persist('coop'));assert(h:state({held=true}))
 assert(old:port()==nil and old:state()==nil)
 sc.state.set('port',h:port())
end,ui_update=inspect,draw=inspect}
""",encoding='utf-8')
    (project/'next.lua').write_text(r"""local h
return {init=function()
 h=assert(sc.net.bind('coop'));assert(h:port()==sc.state.get('port') and h:state().held)
 assert(not pcall(h.state,h,nil));assert(not pcall(h.close,h))
 assert(not pcall(sc.net.token));assert(not pcall(sc.net.host,'127.0.0.1',0))
end,update=function()
 sc.debug.watch('network_contracts',{passed=true,port=h:port(),held=h:state().held})
end}
""",encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','3'],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['watches']['network_contracts']['passed']
print('network: 15 schemas, budgets, strict names, explicit-nil mutation and cross-room ownership passed')
