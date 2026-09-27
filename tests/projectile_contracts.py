"""Projectile API schemas, strict call boundaries, atomic failure and real hit snapshots."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parents[1]
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
functions={r['name'].rsplit('.',1)[1]:r['contract'] for r in api['functions'] if r['name'].startswith('sc.projectiles.')}
assert set(functions)=={'configure','sprite','spawn','hits','count','clear','stats'} and all(functions.values())
for name,contract in functions.items():
    assert contract['module']=='core'
    expected=['load','init'] if name in ['configure','sprite'] else ['load','init','update']
    if name in ['hits','count','stats']:expected+=['draw','ui_update']
    assert contract['phases']==expected,(name,contract)
assert not functions['configure']['parameters'][0]['required']
spec={f['name']:f for f in api['types']['ScProjectileSpec']['fields']}
defaults={'x':0,'y':0,'vx':0,'vy':0,'ax':0,'ay':0,'radius':2,'life':3,'mask':4294967295,'color':4294967295,'sprite':0,'terrain':True,'piercing':False}
assert {name:f['default'] for name,f in spec.items()}==defaults
assert all(not f['required'] for f in spec.values())
assert spec['radius']['minimum']==.001 and spec['radius']['maximum']==256
assert spec['life']['minimum']==.001 and spec['life']['maximum']==3600
for name,names in [('ScProjectileHit',{'projectile','target','fraction','x','y'}),
                   ('ScProjectileStats',{'limit','capacity','used','available','sprites'})]:
    fields=api['types'][name]['fields']
    assert {f['name'] for f in fields}==names and all(f['readonly'] and f['required'] for f in fields)
with tempfile.TemporaryDirectory(prefix='shiny-projectile-contract-') as directory:
    project=Path(directory)
    shutil.copyfile(root/'examples/barrage/assets/wisp.png',project/'atlas.png')
    (project/'project.lua').write_text("return {limits={entities=2,particles=0,projectiles=4},resources={atlas={type='image',path='atlas.png'}}}",encoding='utf-8')
    (project/'main.lua').write_text(r'''local p=sc.projectiles
local target,shot,seen=false,false,false
local function reject(fn,...) assert(not pcall(fn,...)) end
local function reads()
 for _,fn in ipairs({p.count,p.stats,p.hits}) do reject(fn,1);reject(fn,nil) end
end
local function readonly()
 reads();reject(p.clear);reject(p.spawn,{});reject(p.configure);reject(p.sprite,'atlas',0,0,8,10)
end
reads();assert(p.count()==0 and #p.hits()==0 and p.stats().capacity==0);p.clear()
return {gravity=0,init=function()
 reject(p.configure,'4');reject(p.configure,4,1);reject(p.configure,4.5)
 assert(p.stats().capacity==0);p.configure(nil);reject(p.configure)
 assert(p.stats().limit==4 and p.stats().capacity==4)
 reject(p.sprite,1,0,0,8,10);reject(p.sprite,'atlas\0suffix',0,0,8,10)
 reject(p.sprite,'atlas','0',0,8,10);reject(p.sprite,'atlas',0,0,8,10,'8')
 reject(p.sprite,'atlas',0,0,8,10,nil,10,1);reject(p.sprite,'atlas',0,0,8,10,0)
 reject(p.sprite,'atlas',0,0,8,10,math.huge);reject(p.sprite,'atlas',31,0,8,10)
 assert(p.stats().sprites==0)
 local sprite=p.sprite('atlas',0,0,8,10,nil,nil);assert(sprite==1)
 for i=2,64 do assert(p.sprite('atlas',0,0,8,10)==i) end
 reject(p.sprite,'atlas',0,0,8,10);assert(p.stats().sprites==64)
 reject(p.spawn);reject(p.spawn,{},1);reject(p.clear,1)
 reject(p.spawn,{{x=2},{radius=0}});reject(p.spawn,{{sprite=65}})
 reject(p.spawn,{{},{},{},{},{}});assert(p.count()==0)
 assert(#p.spawn({})==0)
 local ids=p.spawn{{terrain=false,mask=0,sprite=sprite},{terrain=false,mask=0}}
 assert(ids[1]==1 and ids[2]==2 and p.count()==2)
 local stats=p.stats();stats.used=0;assert(p.stats().used==2)
 p.clear();assert(p.count()==0 and p.stats().sprites==64 and #p.hits()==0)
 target=sc.spawn{x=20,y=20,w=8,h=8,body=false,solid=true}
 shot=p.spawn{{x=0,y=24,vx=1800,terrain=false,sprite=sprite}}[1];assert(shot==3)
end,update=function()
 reads();reject(p.configure);reject(p.sprite,'atlas',0,0,8,10)
 if sc.tick()==1 then
  local hits=p.hits();assert(#hits==1 and p.count()==0)
  local h=hits[1];assert(h.projectile==shot and h.target==target and h.fraction>=0 and h.fraction<=1)
  assert(math.abs(h.y-24)<.001 and h.x>0 and h.x<30)
  h.target=0;assert(p.hits()[1].target==target)
  seen=true;sc.debug.watch('contracts',{passed=true})
  p.clear();assert(#p.hits()==0)
 end
end,draw=function() readonly() end,ui_update=function() readonly() end}
''',encoding='utf-8')
    result=subprocess.run([str(binary),str(project),'--headless','--frames','2'],capture_output=True,timeout=25)
    assert result.returncode==0,result.stderr.decode('utf-8',errors='replace')
    assert json.loads(result.stdout)['watches']['contracts']['passed']
print('projectiles: complete schemas, strict arguments/phases, atomic IDs, sprite budget and copied real hits passed')
