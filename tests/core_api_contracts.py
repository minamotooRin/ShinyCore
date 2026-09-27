"""Core API contracts, optional-argument semantics and native boundary regressions."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
assert all(f.get('contract') for f in api['functions']),[f['name'] for f in api['functions'] if not f.get('contract')]
f={e['name']:e['contract'] for e in api['functions']}
for name in ['clip','rect','circle','text']:assert f['sc.'+name]['phases']==['draw']
assert [r['name'] for r in f['sc.measure']['returns']]==['width','height']
assert f['sc.map']['mutation']['when']==f['sc.tile']['mutation']['when']=='present'
assert f['sc.emit']['parameters'][4]['default']==30 and f['sc.emit']['parameters'][5]['default']==.5
assert f['sc.tone']['parameters'][1]['default']==.1 and f['sc.tone']['parameters'][2]['default']==.2
assert {v['name']:v['default'] for v in api['types']['ScTextOptions']['fields']}=={'font':'','wrap':0,'align':0}
with tempfile.TemporaryDirectory(prefix='shiny-core-api-') as directory:
    p=Path(directory)
    (p/'project.lua').write_text("return {limits={particles=2,projectiles=0}}")
    (p/'main.lua').write_text(r"""local function reject(fn,...) assert(not pcall(fn,...)) end
local function read_checks()
 reject(sc.objects,1);reject(sc.objects,nil);assert(#sc.objects()==0)
 reject(sc.tick,nil);reject(sc.time,1);assert(sc.time()==sc.tick()/60)
 for _,fn in ipairs({sc.down,sc.pressed,sc.released}) do reject(fn,'unknown');reject(fn,1);assert(type(fn('jump'))=='boolean') end
 assert(sc.key_down('a')==sc.input.key_down('a'))
 assert(sc.key_pressed('a')==sc.input.key_pressed('a'))
 assert(sc.key_released('a')==sc.input.key_released('a'))
 assert(sc.gamepad_connected()==sc.input.gamepad_connected())
 assert(sc.gamepad_axis('left_x')==sc.input.gamepad_axis('left_x'))
 reject(sc.gamepad_axis,'left_x',1);reject(sc.gamepad_axis,'left_x','0')
 reject(sc.gamepad_connected,nil)
 assert(sc.tile(-1,0)=='#');reject(sc.tile,0,0,nil)
 local width,height=sc.measure('abc',12,nil,nil);assert(width>0 and height>0)
 reject(sc.measure,'abc',12,nil,-1);reject(sc.measure,'abc',12,nil,0,1)
end
read_checks();reject(sc.rect,0,0,1,1,'#ffffff')
return {map={rows={'...','...'}},init=function()
 read_checks();sc.tile(0,0,'=');assert(sc.tile(0,0)=='=');sc.tile(0,0,'.')
 reject(sc.tile,-1,0,'#');assert(sc.tile(0,0)=='.')
 sc.emit(0,0,1,'#ffffffff',0,.001);reject(sc.emit,0,0,0,'#ffffff',nil)
 sc.tone(440,.001,0);reject(sc.tone,440,nil);reject(sc.tone,440,.1,nil)
 reject(sc.random,1);reject(sc.random,2,1);reject(sc.random,nil,nil)
 local n=sc.random();assert(n>=0 and n<1)
 assert(sc.random(4,4)==4 and math.type(sc.random(1,2))=='integer')
 local real=sc.random(1.0,2.0);assert(real>=1 and real<=2 and math.type(real)=='float')
 sc.message('');reject(sc.message,string.rep('a',192))
 reject(sc.scene,'../main.lua');reject(sc.scene,'main.lua',nil)
 sc.log('core contract init')
end,update=function() sc.debug.watch('core_contracts',true) end,draw=function()
 read_checks();reject(sc.random);reject(sc.tile,0,0,'.');reject(sc.emit,0,0,0,'#ffffff');reject(sc.tone,440)
 sc.clip(0,0,100,100);sc.rect(0,0,5,5,'#ffffff');sc.circle(10,10,4,'#ffffff',true)
 sc.text('abc',0,0,12,'#ffffff',true,{wrap=50,align=1});sc.clip()
 reject(sc.clip,1);reject(sc.clip,0,0,1,nil)
 reject(sc.rect,0,0,1,1,'#ffffff',nil);reject(sc.text,'abc',0,0,12,'#ffffff',nil,{})
 reject(sc.text,'abc',0,0,12,'#ffffff',true,{typo=1});reject(sc.text,'abc',0,0,12,'#ffffff',true,{align=3})
end}
""",encoding='utf-8')
    result=subprocess.run([str(binary),str(p),'--headless','--frames','1'],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['watches']['core_contracts']
print('core: complete function metadata, read/draw/mutation phases, strict optionals and minimum lifetimes passed')
