"""Prefab native batch hierarchy and center-based local transforms; optional hidden visual check."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--capture',type=Path)
args=parser.parse_args();binary=args.binary.resolve()
with tempfile.TemporaryDirectory(prefix='shiny-prefab-') as directory:
    project=Path(directory);(project/'lib/shiny').mkdir(parents=True)
    shutil.copyfile(root/'lua/shiny/prefab.lua',project/'lib/shiny/prefab.lua')
    (project/'project.lua').write_text('return {limits={entities=12,identities=12,particles=1,draws=16}}',encoding='utf-8')
    (project/'main.lua').write_text('''local P=require('shiny.prefab')
local function close(a,b) assert(math.abs(a-b)<.0001,tostring(a)..' ~= '..tostring(b)) end
return {width=384,height=216,gravity=0,ambient=1,map={rows={'.'},background='#111C2EFF'},init=function()
 local touched=false
 assert(not pcall(P.merge,{},setmetatable({},{__pairs=function() touched=true;return next,{},nil end})))
 assert(not touched)
 local cyclic={};cyclic.self=cyclic;assert(not pcall(P.merge,{},cyclic))
 local merged=P.merge({items={1,2},fields={a=1}},{items={},fields={b=2}})
 assert(#merged.items==0 and merged.fields.a==1 and merged.fields.b==2)
 assert(not pcall(P.spawn,{entity={persistent_id='failed'},children={bad={w=-1}}}))
 assert(sc.identity.resolve('failed').status=='absent')
 assert(not pcall(P.spawn,{entity={persistent_id='body'},children={bad={dynamic=true}}}))
 assert(sc.identity.resolve('body').status=='absent')
 local definition={entity={persistent_id='root',x=100,y=100,w=20,h=10,angle=math.pi/2,body=false},
   children={a={x=20,w=4,h=4,angle=math.pi/4,persistent_id='a'},b={x=40,w=4,h=4,persistent_id='b'}}}
 local instance=P.spawn(definition)
 local a=sc.get(instance.children.a.id);local b=sc.get(instance.children.b.id)
 close(a.x,111);close(a.y,115);close(a.angle,math.pi*.75)
 assert(definition.children.a.x==20 and definition.children.a.angle==math.pi/4)
 assert(P.update==nil)
 assert(sc.presentation.attachment(instance.children.a.id).parent==instance.id)
 sc.set(instance.id,{x=120,angle=0})
 a=sc.get(instance.children.a.id);b=sc.get(instance.children.b.id)
 close(a.x,140);close(a.y,100);close(a.angle,math.pi/4)
 -- Invalid batch pose fails before root identity is allocated.
 assert(not pcall(P.spawn,{entity={persistent_id='edge',x=999999},children={a={x=20}}}))
 assert(sc.identity.resolve('edge').status=='absent')
 sc.destroy(instance.children.b.id)
 assert(not pcall(P.destroy,instance))
 assert(sc.get(instance.id).x==120 and sc.get(instance.children.a.id).x==a.x)
 sc.destroy(instance.children.a.id);sc.destroy(instance.id)
 local live=P.spawn{entity={body=false},children={a={x=10}}}
 assert(P.destroy(live) and not P.destroy(live))
 -- A single bad capacity batch must not reserve root or child identities.
 local children={};for i=1,12 do children['c'..i]={} end
 assert(not pcall(P.spawn,{entity={persistent_id='full'},children=children}))
 assert(sc.identity.resolve('full').status=='absent')
 for i,angle in ipairs({0,math.pi/2,math.pi}) do
   P.spawn{entity={x=38+(i-1)*128,y=96,w=40,h=16,angle=angle,body=false,solid=false,color='#5276B8'},
     children={lamp={x=44,y=4,w=12,h=8,angle=math.pi/4,color='#FFC98C',solid=false},
               marker={x=18,y=-12,w=4,h=4,color='#66D9B0',solid=false}}}
 end
 sc.camera.set{x=0,y=0,bounds=false}
 sc.debug.watch('prefab',{passed=true})
end,draw=function()
 sc.text('PREFAB / LOCAL TRANSFORMS',16,20,18,'#E6EDF7',true)
 for i,label in ipairs({'0 DEG','90 DEG','180 DEG'}) do sc.text(label,30+(i-1)*128,160,12,'#A4B8D4',true) end
 sc.text('Child rotation and center offsets are preserved.',16,190,10,'#A4B8D4',true)
end}''',encoding='utf-8')
    command=[str(binary),str(project),'--frames','1','--save-dir',str(project/'saves')]
    if args.capture:
        output=args.capture.resolve();output.mkdir(parents=True,exist_ok=True)
        command+=['--capture-hidden','--mute','--capture',str(output/'attachments.png')]
    else: command+=['--headless']
    result=subprocess.run(command,capture_output=True,timeout=30)
    assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['watches']['prefab']['passed']
    if args.capture:
        (output/'snapshot.json').write_bytes(result.stdout);(output/'run.log').write_bytes(result.stderr)
        (output/'manifest.json').write_text(json.dumps({'command':command,'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'reproduce':'python tests/prefab_integration.py build/full/shiny.exe --capture build/prefab-reviewed',
            'visual_review':'pending'},indent=2)+'\n',encoding='utf-8')
print('Prefab: atomic native hierarchy, local angle/center, stale handles and rollback passed')
