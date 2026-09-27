"""Visual attachment contracts and native/headless fixed-state agreement."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--capture',type=Path)
args=parser.parse_args();binary=args.binary.resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
functions={row['name']:row for row in api['functions']}
for name in ['attach','detach']:
    assert functions['sc.presentation.'+name]['contract']['phases']==['load','init','update']
fields=api['types']['ScAttachmentOffset']['fields']
assert {field['name'] for field in fields}=={'x','y','angle'}
assert all(field['default']==0 and field['finite'] and field['minimum']==-1e6 and field['maximum']==1e6 for field in fields)
with tempfile.TemporaryDirectory(prefix='shiny-attachment-') as directory:
    project=Path(directory)
    (project/'project.lua').write_text('return {limits={entities=16,particles=0,draws=16}}',encoding='utf-8')
    (project/'main.lua').write_text(r'''local p=sc.presentation
local root,child,tip
local function reject(fn,...) assert(not pcall(fn,...)) end
local function close(a,b) assert(math.abs(a-b)<.002,tostring(a)..' ~= '..tostring(b)) end
local function check()
 local r,c=sc.get(root),sc.get(child)
 local x,y=48+(c.w-r.w)/2,0+(c.h-r.h)/2
 close(c.x,r.x+(r.w-c.w)/2+x*math.cos(r.angle)-y*math.sin(r.angle))
 close(c.y,r.y+(r.h-c.h)/2+x*math.sin(r.angle)+y*math.cos(r.angle))
 close(c.angle,r.angle+.4)
end
return {width=384,height=216,gravity=0,ambient=1,map={rows={'.'},background='#111C2EFF'},
 init=function()
  local specs={{x=5,w=10,h=10},{x=100,y=100,w=10,h=10,persistent_id='batch-root'},{x=20,w=10,h=10}}
  reject(sc.spawn_many,specs,{3,1,2}) -- cycle
  reject(sc.spawn_many,specs,{3,0})
  reject(sc.spawn_many,specs,{3,0,4})
  reject(sc.spawn_many,specs,{3,0,1.5})
  reject(sc.spawn_many,specs,{3,0,'2'})
  reject(sc.spawn_many,specs,setmetatable({3,0,2},{}))
  reject(sc.spawn_many,specs,{[1]=3,[3]=2})
  assert(sc.identity.resolve('batch-root').status=='absent')
  local ids=sc.spawn_many(specs,{3,0,2})
  close(sc.get(ids[1]).x,125);close(sc.get(ids[1]).y,100)
  assert(p.attachment(ids[1]).parent==ids[3] and p.attachment(ids[3]).parent==ids[2])
  for _,id in ipairs(ids) do sc.destroy(id) end
  assert(#sc.spawn_many({},{})==0)
  local a=sc.spawn{x=100,y=100,w=10,h=10,body=false,solid=false}
  local b=sc.spawn{w=10,h=10,body=false,solid=false}
  local c=sc.spawn{w=10,h=10,body=false,solid=false}
  p.attach(b,a,{x=20});p.attach(c,b,{x=10})
  close(sc.get(c).x,130)
  reject(p.attach,a,c,{})
  reject(p.attach,b,a,{x=0/0});reject(p.attach,b,a,{x='2'})
  reject(p.attach,b,a,{extra=1});reject(p.attach,b,a,{['x\0']=1})
  reject(p.attach,b,a,setmetatable({},{__index=function() error('metamethod invoked') end}))
  reject(p.attach,b,a,{},1);reject(p.detach,b,1);reject(p.attachment,b,1)
  local relation=p.attachment(b);relation.x=100;assert(p.attachment(b).x==20)
  reject(sc.set,b,{x=1});reject(sc.set,b,{vx=1});reject(sc.set,b,{body={type='static'}})
  reject(sc.set_many,{{id=a,patch={x=150}},{id=b,patch={angle=1}}})
  close(sc.get(a).x,100);close(sc.get(c).x,130)
  sc.set(a,{x=110});close(sc.get(c).x,140)
  sc.destroy(a);assert(p.attachment(b)==nil and p.attachment(c).parent==b)
  close(sc.get(b).x,130);close(sc.get(c).x,140)
  local replacement=sc.spawn{body=false};assert(replacement~=a);reject(p.attach,b,a,{})
  p.detach(c);p.detach(c);assert(p.attachment(c)==nil);close(sc.get(c).x,140)
  sc.destroy(b);sc.destroy(c);sc.destroy(replacement)
  root=sc.spawn{x=120,y=80,w=40,h=16,body={type='dynamic',fixed_rotation=false},
    solid=false,vx=15,angular_velocity=1.2,color='#5276B8'}
  child=sc.spawn{w=12,h=8,body=false,solid=false,color='#FFC98C'}
  tip=sc.spawn{w=4,h=4,body=false,solid=false,color='#66D9B0'}
  reject(p.attach,root,tip,{})
  p.attach(child,root,{x=48,angle=.4});p.attach(tip,child,{x=18})
  p.interpolate(true);sc.camera.set{x=0,y=0,bounds=false}
  check()
 end,
 update=function()
  check() -- The previous physics step already updated all descendants.
  local r,c,t=sc.get(root),sc.get(child),sc.get(tip)
  sc.debug.watch('attachment',{passed=true,root_x=r.x,root_angle=r.angle,child_x=c.x,child_y=c.y,tip_x=t.x,tip_y=t.y})
 end,
 draw=function()
  check() -- Current physics pose, never a pre-physics attachment pose.
  reject(p.attach,child,root,{})
  reject(p.detach,child)
  sc.text('NATIVE / ATTACHMENTS',16,20,18,'#E6EDF7',true)
  sc.text('Physics root > local child > nested marker',16,168,12,'#A4B8D4',true)
  sc.text('No Lua transform update',16,192,10,'#A4B8D4',true)
 end}
''',encoding='utf-8')
    command=[str(binary),str(project),'--frames','60','--save-dir',str(project/'saves')]
    headless=subprocess.run(command+['--headless'],capture_output=True,timeout=30)
    assert headless.returncode==0,headless.stderr.decode('utf-8',errors='replace')
    fixed=json.loads(headless.stdout);assert fixed['watches']['attachment']['passed']
    if args.capture:
        output=args.capture.resolve();output.mkdir(parents=True,exist_ok=True)
        native_command=command+['--capture-hidden','--mute','--capture',str(output/'attachments.png')]
        native=subprocess.run(native_command,capture_output=True,timeout=30)
        assert native.returncode==0,native.stderr.decode('utf-8',errors='replace')
        actual=json.loads(native.stdout)
        for field in ['entities','watches','hash','rng','tick']:
            assert actual[field]==fixed[field],field
        for name,result in [('headless',headless),('native',native)]:
            (output/(name+'.json')).write_bytes(result.stdout)
            (output/(name+'.log')).write_bytes(result.stderr)
        (output/'manifest.json').write_text(json.dumps({'command':native_command,
            'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'fixed_fields_equal':['entities','watches','hash','rng','tick'],'visual_review':'pending'},indent=2)+'\n',encoding='utf-8')
print('attachments: Lua validation, phase, atomic patch, lifetime and post-physics poses passed')
