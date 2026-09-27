"""Behavior contracts for the 0.2 host, scripting, resources and physics."""
import json
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv[1]).resolve()
ROOT=Path(sys.argv[2]).resolve()
sys.argv[1:]=[]

class Features(unittest.TestCase):
    def test_projectile_direct_batch_validation(self):
        self.file('main.lua','''return {init=function()
sc.projectiles.configure(8192)
assert(#sc.projectiles.spawn{}==0)
local bad={{x='1'},{x=0/0},{vx=math.huge},{mask=1.5},{color=-1},{terrain=1},
 {piercing='yes'},{sprite=65},{sprite=1},{radius=0},{life=3601},{typo=1},{[1]=1},
 {['x'..string.char(0)]=1},setmetatable({},{__index=function() error('must not execute') end})}
for _,spec in ipairs(bad) do
 assert(not pcall(sc.projectiles.spawn,{{x=1},spec}))
 assert(sc.projectiles.count()==0)
end
for _,batch in ipairs({{[2]={}},{{},extra={}},setmetatable({{}},{__len=function() error('must not execute') end})}) do
 assert(not pcall(sc.projectiles.spawn,batch))
 assert(sc.projectiles.count()==0)
end
local batch={}
for i=1,4096 do batch[i]={x=i,y=10,vx=1,vy=0,ax=0,ay=0,radius=1,life=3,
 terrain=false,piercing=false,mask=0,color=0xffffffff,sprite=0} end
local ids=sc.projectiles.spawn(batch)
assert(#ids==4096 and ids[1]==1 and ids[4096]==4096 and math.type(ids[1])=='integer')
assert(sc.projectiles.count()==4096)
assert(not pcall(sc.projectiles.spawn,{{sprite=1}}))
assert(sc.projectiles.spawn{{terrain=false,mask=0}}[1]==4097)
end}''')
        self.run_game('--frames',1)
    def test_particle_texture_contract(self):
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('project.lua',"return {resources={atlas={type='image',path='atlas.png'}}}")
        self.file('main.lua','''return {init=function()
local function region(resource,x,y,w,h)
 return {texture={resource=resource,x=x,y=y,w=w,h=h}}
end
for _,spec in ipairs({region('missing',0,0,12,18),region('atlas',90,0,12,18),
 region('atlas',0,0,12,19),region('atlas',0,0,0,18),region('atlas',.5,0,12,18),
 {blend='unknown'},{blend=true},{texture={resource='atlas'}}}) do
 assert(not pcall(sc.particles.define,spec))
end
assert(sc.particles.stats().emitters==0)
local spec=region('atlas',0,0,12,18); spec.blend='additive'
local id=sc.particles.define(spec)
spec.texture.w=9000; spec.blend='unknown'
assert(sc.particles.burst(id,20,20,2)==2)
assert(sc.particles.stats().used==2)
end}''')
        self.run_game('--frames',2)
    def test_particle_emitter_contract(self):
        self.file('project.lua','return {limits={particles=8},rooms={"main.lua","next.lua"}}')
        self.file('lib/shiny/particles.lua',(ROOT/'lua/shiny/particles.lua').read_text(encoding='utf-8'))
        self.file('main.lua','''local P=require('shiny.particles'); local emitter
return {init=function()
assert(not pcall(sc.particles.define,{curve={{time=0,size=2,color=0},{time=0,size=3,color=0}}}))
assert(not pcall(sc.particles.define,{speed_min=100,speed_max=10}))
assert(not pcall(sc.particles.define,{typo=1}))
assert(sc.particles.stats().emitters==0)
emitter=P.new({speed_min=0,speed_max=0,gravity=0},30)
sc.state.set('old_emitter',emitter.id)
assert(P.update(emitter,1/60,20,30)==0)
assert(P.update(emitter,1/60,20,30)==1)
assert(sc.particles.stats().used==1)
local remainder=emitter.remainder
assert(not pcall(P.update,emitter,1,20,30))
assert(emitter.remainder==remainder and sc.particles.stats().used==1)
emitter.enabled=false; assert(P.update(emitter,1,20,30)==0)
end,update=function()
assert(not pcall(sc.particles.define,{}))
sc.scene('next.lua')
end,draw=function()
assert(not pcall(sc.particles.burst,emitter.id,0,0,1))
end}''')
        self.file('next.lua', '''return {init=function()
local new=sc.particles.define({})
assert(new~=sc.state.get('old_emitter'))
assert(not pcall(sc.particles.burst,sc.state.get('old_emitter'),0,0,1))
assert(sc.particles.burst(new,0,0,1)==1)
end}''')
        self.assertEqual(self.run_game('--frames',2)['scene'],'next.lua')
    def test_particle_capacity_is_atomic(self):
        self.file('project.lua','return {limits={particles=2}}')
        self.file('main.lua','''return {init=function()
sc.emit(0,0,1,'#FFFFFFFF')
local ok,err=pcall(sc.emit,0,0,2,'#FFFFFFFF')
assert(not ok and err:find('used=1') and err:find('requested=2') and err:find('capacity=2'))
sc.emit(0,0,1,'#FFFFFFFF')
assert(not pcall(sc.emit,0,0,1,'#FFFFFFFF'))
end}''')
        self.run_game('--frames',1)
        self.file('project.lua','return {limits={particles=0}}')
        self.file('main.lua',"return {init=function() sc.emit(0,0,0,'#FFFFFFFF'); assert(not pcall(sc.emit,0,0,1,'#FFFFFFFF')) end}")
        self.run_game('--frames',1)
    def test_projectile_capacity_budget(self):
        self.file('project.lua','return {limits={projectiles=2}}')
        self.file('main.lua','''return {init=function()
local s=sc.projectiles.stats()
assert(s.limit==2 and s.capacity==0 and s.used==0 and s.available==0)
local ok,err=pcall(sc.projectiles.configure,3)
assert(not ok and err:find('requested=3') and err:find('limit=2'))
assert(sc.projectiles.stats().capacity==0)
sc.projectiles.configure()
sc.projectiles.spawn{{terrain=false,mask=0}}
ok,err=pcall(sc.projectiles.spawn,{{},{}})
assert(not ok and err:find('used=1') and err:find('requested=2') and err:find('capacity=2'))
s=sc.projectiles.stats(); assert(s.used==1 and s.capacity==2 and s.available==1)
assert(sc.projectiles.spawn{{terrain=false,mask=0}}[1]==2)
assert(not pcall(sc.projectiles.configure,1))
end,draw=function() assert(sc.projectiles.stats().used==2) end}''')
        self.run_game('--frames',1)
        self.file('project.lua','return {limits={projectiles=0}}')
        self.file('main.lua','''return {init=function()
assert(sc.projectiles.stats().limit==0 and sc.projectiles.stats().capacity==0)
assert(not pcall(sc.projectiles.configure))
assert(not pcall(sc.projectiles.configure,1))
end}''')
        self.run_game('--frames',0)
        for capacity in [-1, 65537, 1.5]:
            self.file('project.lua',f'return {{limits={{projectiles={capacity}}}}}')
            self.assertIn('invalid capacity: projectiles',self.run_game('--frames',0,ok=False))
        self.file('project.lua','return {limits={projectiles=65536}}')
        self.file('main.lua','''return {init=function()
sc.projectiles.configure()
assert(sc.projectiles.stats().capacity==65536)
end}''')
        self.run_game('--frames',0)
        self.file('project.lua','return {}')
        self.file('main.lua','''return {init=function()
assert(sc.projectiles.stats().limit==32768 and sc.projectiles.stats().capacity==0)
sc.projectiles.configure(2)
assert(sc.projectiles.stats().capacity==2)
end}''')
        self.run_game('--frames',0)
    def test_projectile_atlas(self):
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('project.lua',"return {resources={atlas={type='image',path='atlas.png'}}}")
        self.file('main.lua', '''return {init=function()
sc.projectiles.configure(8)
assert(not pcall(sc.projectiles.sprite,'missing',0,0,16,18))
assert(not pcall(sc.projectiles.sprite,'atlas',95,0,16,18))
assert(not pcall(sc.projectiles.sprite,'atlas',0,0,16,18,4097,18))
local sprite=sc.projectiles.sprite('atlas',0,0,16,18,32,36)
assert(sprite==1)
assert(not pcall(sc.projectiles.spawn,{{sprite=1},{sprite=2}}))
assert(sc.projectiles.count()==0)
sc.projectiles.spawn{{sprite=sprite,terrain=false,mask=0}}
for i=2,64 do assert(sc.projectiles.sprite('atlas',0,0,16,18)==i) end
assert(not pcall(sc.projectiles.sprite,'atlas',0,0,16,18))
end,update=function()
assert(not pcall(sc.projectiles.sprite,'atlas',0,0,16,18))
assert(sc.projectiles.count()==1)
end}''')
        self.run_game('--frames',2)
    def test_projectile_atlas_requires_bounded_png(self):
        self.file('project.lua',"return {resources={atlas={type='image',path='atlas.png'}}}")
        self.file('main.lua',"return {init=function() sc.projectiles.configure(8); sc.projectiles.sprite('atlas',0,0,1,1) end}")
        self.file('atlas.png','not a PNG')
        self.assertIn('requires a declared PNG',self.run_game('--frames',0,ok=False))
        (self.root/'atlas.png').write_bytes(b'\x89PNG\r\n\x1a\n')
        self.assertIn('invalid bounded PNG header',self.run_game('--frames',0,ok=False))
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='shiny-02-')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
    def file(self,name,text):
        path=self.root/name
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(text,encoding='utf-8')
    def run_game(self,*args,ok=True):
        p=subprocess.run([str(BINARY),'--headless',str(self.root),*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=20)
        self.assertEqual(p.returncode==0,ok,p.stderr or p.stdout)
        return json.loads(p.stdout) if ok else p.stderr
    def test_modules_and_explicit_state(self):
        self.file('lib/counter.lua','return {count=0}')
        self.file('main.lua', '''local a=require('lib.counter'); local b=require('lib.counter')
assert(a==b); a.count=a.count+1
return {init=function()
  sc.state.set('nested',{coins=3,items={true,'灯'}})
  local copy=sc.state.get('nested'); copy.coins=99
  assert(sc.state.get('nested').coins==3)
  sc.state.set('count',b.count)
end}''')
        x=self.run_game('--frames',0)
        self.assertEqual(x['state'],{'nested':{'coins':3,'items':[True,'灯']},'count':1})
    def test_module_cycle_and_draw_load(self):
        self.file('a.lua',"return require('b')")
        self.file('b.lua',"return require('a')")
        self.file('main.lua',"local a=require('a'); return {}")
        self.assertIn('circular',self.run_game('--frames',0,ok=False))
        self.file('main.lua',"return {draw=function() require('a') end}")
        self.assertIn('draw',self.run_game('--frames',0,ok=False))
    def test_invalid_state_is_atomic(self):
        self.file('main.lua', '''return {init=function()
sc.state.set('x',7)
for _,bad in ipairs({function() end, {x=0/0}, setmetatable({},{})}) do
  assert(not pcall(sc.state.set,'x',bad)); assert(sc.state.get('x')==7)
end
local cycle={}; cycle.a=cycle; assert(not pcall(sc.state.set,'x',cycle))
end}''')
        self.assertEqual(self.run_game('--frames',0)['state']['x'],7)
    def test_transition_state_and_fresh_vm(self):
        self.file('main.lua',"return {update=function() sc.state.set('coins',8); sc.scene('rooms/second.lua') end}")
        self.file('rooms/second.lua',"return {init=function() assert(sc.tick()==0); sc.state.set('seen',sc.state.get('coins')) end}")
        x=self.run_game('--frames',2)
        self.assertEqual(x['scene'],'rooms/second.lua')
        self.assertEqual(x['state'],{'coins':8,'seen':8})
    def test_disk_checkpoint_restarts_room(self):
        self.file('project.lua',"return {id='checkpoint',data_version=1,rooms={'main.lua'}}")
        self.file('main.lua',"return {update=function() sc.state.set('coins',9); assert(sc.save.write('slot')) end}")
        saves=self.root/'saves'
        self.run_game('--frames',1,'--save-dir',saves)
        save=saves/'checkpoint/slot.json'
        self.assertEqual(json.loads(save.read_text())['state'],{'coins':9})
        self.file('main.lua',"return {update=function() if not sc.state.get('coins') then assert(sc.save.load('slot')) end end}")
        x=self.run_game('--frames',2,'--save-dir',saves)
        self.assertEqual(x['state']['coins'],9)
        saved=save.read_bytes()
        self.run_game('--check','--save-dir',saves)
        self.assertEqual(save.read_bytes(),saved)
        save.write_text('{broken')
        self.assertIn('JSON',self.run_game('--frames',1,'--save-dir',saves,ok=False))
    def test_project_entry_and_all_rooms(self):
        self.file('project.lua',"return {entry='rooms/start.lua',rooms={'rooms/start.lua','rooms/bad.lua'}}")
        self.file('rooms/start.lua','return {}')
        self.file('rooms/bad.lua','return {widht=3}')
        self.assertEqual(self.run_game('--frames',0)['scene'],'rooms/start.lua')
        self.assertIn('widht',self.run_game('--check-all',ok=False))
    def test_rigid_bodies_ground_and_push(self):
        self.file('main.lua', '''local p,b
return {gravity=600,init=function()
sc.spawn({tag='floor',x=0,y=180,w=384,h=20,body={type='static'}})
p=sc.spawn({tag='player',x=20,y=130,w=12,h=16,body={type='dynamic',friction=0}})
b=sc.spawn({tag='box',x=55,y=140,w=12,h=12,body={type='dynamic',friction=0}})
end,update=function()
if sc.tick()>60 then sc.set(p,{vx=45}) end
if sc.tick()==60 then assert(sc.get(p).grounded); assert(sc.get(p).normal_y<-.5) end
end}''')
        x=self.run_game('--frames',180)
        box=next(e for e in x['entities'] if e['tag']=='box')
        self.assertGreater(box['x'],70)
    def test_one_way_drop(self):
        self.file('main.lua', '''local p
return {init=function()
sc.spawn({x=0,y=140,w=300,h=4,body={type='static',one_way=true}})
p=sc.spawn({tag='player',x=30,y=100,w=10,h=12,body={type='dynamic'}})
end,update=function()
if sc.tick()==60 then assert(sc.get(p).grounded); sc.physics.drop(p,.5); sc.set(p,{vy=70}) end
end}''')
        p=next(e for e in self.run_game('--frames',85)['entities'] if e['tag']=='player')
        self.assertGreater(p['y'],140)
    def test_queries_joints_and_impulse(self):
        self.file('main.lua', '''local a,b,j
return {gravity=0,init=function()
a=sc.spawn({tag='anchor',x=80,y=30,w=8,h=8,body={type='static'}})
b=sc.spawn({tag='bob',x=80,y=60,w=8,h=8,body={type='dynamic',shape='circle'}})
j=sc.physics.joint('distance',a,b,84,34,30)
assert(#sc.physics.query(70,20,30,60)==2)
assert(sc.physics.ray(0,34,100,0).id==a)
sc.physics.impulse(b,10,0)
end,update=function() if sc.tick()==5 then sc.physics.unjoint(j); assert(not pcall(sc.physics.unjoint,j)) end end}''')
        self.run_game('--frames',10)

    def test_exact_overlap_and_sweep(self):
        self.file('main.lua', '''local circle,rotated
return {gravity=0,init=function()
circle=sc.spawn{x=20,y=20,w=20,h=20,body={type='static',shape='circle'}}
sc.spawn{x=20,y=20,w=20,h=20,body=false}
assert(#sc.physics.query(20,20,2,2)==0)
assert(sc.physics.overlap({30,30},1)[1]==circle)
assert(sc.physics.overlap({15,30,45,30},2)[1]==circle)
assert(sc.physics.overlap({22,22,38,22,30,38},0)[1]==circle)
rotated=sc.spawn{x=70,y=20,w=30,h=4,angle=math.pi/2,body={type='static'}}
assert(sc.physics.query(84,6,2,4)[1]==rotated)
assert(#sc.physics.query(69,20,4,4)==0)
assert(sc.physics.query(70,20,30,4,math.pi/2)[1]==rotated)
assert(#sc.physics.query(20,20,0,20)==0)
local compound=sc.spawn{x=50,y=55,w=16,h=8,body={type='static',sensor=true,category=2,mask=2,
    shapes={{shape='box',w=8,h=8},{shape='box',x=4,w=8,h=8}}}}
local found=sc.physics.overlap({56,59},3); assert(#found==1 and found[1]==compound)
sc.set(compound,{solid=false}); assert(#sc.physics.overlap({56,59},3)==0)
local hit=sc.physics.sweep({10,30},2,50,0)
assert(hit.id==circle and math.abs(hit.fraction-.16)<.02 and hit.nx<-.99)
hit=sc.physics.sweep({30,30},2,0,0)
assert(hit.id==circle and hit.fraction==0 and hit.nx==0 and hit.ny==0)
assert(sc.physics.overlap({-1,30},2)[1]==0)
sc.destroy(circle)
circle=sc.spawn{x=20,y=20,w=20,h=20,body={type='static',shape='circle'}}
local twin=sc.spawn{x=20,y=20,w=20,h=20,body={type='static',shape='circle'}}
local expected=math.min(circle,twin)
assert(sc.physics.ray(10,30,50,0).id==expected)
assert(sc.physics.sweep({10,30},2,50,0).id==expected)
found=sc.physics.query(10,5,100,80)
assert(#found==3); for i=2,#found do assert(found[i-1]<found[i]) end
for _,bad in ipairs({{30,30},{30,30,30,30},{20,20,40,20,25,25,40,40,20,40},
                      {20,20,40,20,20,40,foo=1},{20,20,0/0,40,20,40}}) do
    assert(not pcall(sc.physics.overlap,bad,0))
end
assert(not pcall(sc.physics.sweep,{30,30},2,math.huge,0))
assert(not pcall(sc.physics.overlap,{30,30},4097))
sc.set(circle,{x=200}); assert(sc.physics.overlap({30,30},1)[1]==twin)
sc.debug.watch('queries',true)
end,draw=function()
assert(not pcall(sc.physics.query,0,0,10,10))
assert(not pcall(sc.physics.overlap,{30,30},2))
assert(not pcall(sc.physics.sweep,{30,30},2,1,0))
end}''')
        self.assertTrue(self.run_game('--frames',1)['watches']['queries'])

    def test_joint_control_atomicity_and_lifecycle(self):
        self.file('main.lua', '''local a,b,j
return {gravity=0,init=function()
a=sc.spawn{x=80,y=80,w=10,h=10,solid=false,body={type='static'}}
b=sc.spawn{x=120,y=80,w=10,h=10,solid=false,body={type='dynamic',fixed_rotation=false}}
j=sc.physics.joint('distance',a,b,85,85,40,125,85)
local c=sc.physics.joint_control(j)
assert(not c.motor and not c.limit and c.speed==0 and c.max_effort==0)
c.speed=123; assert(sc.physics.joint_control(j).speed==0)
sc.physics.joint_control(j,{limit=true,lower=20,upper=60,motor=true,speed=48,max_effort=100})
for _,bad in ipairs({{motor=false,lower=70},{motor=false,speed=0/0},{motor=false,unknown=1},
                      {motor=1},{max_effort=-1},{lower=0},{speed=1e7},{upper=5000}}) do
    assert(not pcall(sc.physics.joint_control,j,bad))
    c=sc.physics.joint_control(j); assert(c.motor and c.speed==48 and c.lower==20)
end
assert(not pcall(sc.physics.joint,'revolute',a,b,85,85,8,125,85))
assert(not pcall(sc.physics.joint,'distance',a,b,85,85,40,125))
end,update=function()
if sc.tick()==120 then
    assert(math.abs(sc.get(b).x-140)<1)
    sc.physics.joint_control(j,{speed=-48})
elseif sc.tick()==240 then
    assert(math.abs(sc.get(b).x-100)<1)
    sc.physics.unjoint(j); assert(not pcall(sc.physics.joint_control,j))
    local next_joint=sc.physics.joint('prismatic',a,b,85,85,24)
    assert(next_joint~=j and not pcall(sc.physics.joint_control,j,{motor=true}))
    sc.set(b,{w=11}); assert(not pcall(sc.physics.joint_control,next_joint))
    sc.debug.watch('joint_control',true)
end
end,draw=function() assert(not pcall(sc.physics.joint_control,j)) end}''')
        self.assertTrue(self.run_game('--frames',241)['watches']['joint_control'])

    def test_flow_invalidation_and_budgeted_refresh(self):
        self.file('main.lua', '''local f
return {init=function()
    f=assert(sc.navigation.flow(4,4,1))
    local dx,dy,status=sc.navigation.direction(f,8,8)
    assert(dx==0 and dy==0 and status=='budget_exhausted')
    repeat status=sc.navigation.refresh(f,32) until status=='ok'
    sc.tile(4,4,'#')
    dx,dy,status=sc.navigation.direction(f,8,8)
    assert(dx==0 and dy==0 and status=='stale')
    assert(sc.navigation.refresh(f,1)=='unreachable')
    sc.tile(4,4,'.'); assert(select(3,sc.navigation.direction(f,8,8))=='stale')
    repeat status=sc.navigation.refresh(f,32) until status=='ok'
    sc.tile(4,4,'.'); assert(select(3,sc.navigation.direction(f,8,8))=='ok')
    local actor=sc.spawn{x=8,y=8,w=4,h=4,vx=10}
    sc.tile(3,4,'#'); sc.navigation.steer(f,{actor},20)
    assert(sc.get(actor).vx==0 and sc.get(actor).vy==0)
    assert(not pcall(sc.navigation.refresh,f,0))
    sc.debug.watch('flow_refresh',true)
end,draw=function() assert(not pcall(sc.navigation.refresh,f,10)) end}''')
        self.assertTrue(self.run_game('--frames',1)['watches']['flow_refresh'])

    def test_joint_handles_are_room_local(self):
        create='''local a=sc.spawn{x=80,y=80,w=8,h=8,body={type='static'}}
local b=sc.spawn{x=100,y=80,w=8,h=8,body={type='dynamic'}}
local j=sc.physics.joint('distance',a,b,84,84,20,104,84)
'''
        self.file('main.lua', 'return {init=function()\n'+create+'''
assert(j>0xffffffff and j<2^52)
sc.state.set('old_joint',j)
end,update=function() sc.scene('next.lua') end}''')
        self.file('next.lua', 'return {init=function()\n'+create+'''
local old=sc.state.get('old_joint')
assert(j~=old and not pcall(sc.physics.joint_control,old))
assert(not pcall(sc.physics.joint_control,old,{motor=true,speed=100}))
assert(not pcall(sc.physics.unjoint,old))
assert(not sc.physics.joint_control(j).motor)
sc.physics.unjoint(j)
assert(not pcall(sc.physics.joint_control,j))
sc.debug.watch('joint_room_isolation',true)
end}''')
        self.assertTrue(self.run_game('--frames',2)['watches']['joint_room_isolation'])
    def test_tiled_layer_collision_and_edit(self):
        tiled={'orientation':'orthogonal','infinite':False,'width':12,'height':12,'tilewidth':8,'tileheight':8,
          'tilesets':[{'firstgid':1,'image':'atlas.png','tilewidth':8,'tileheight':8,'columns':1,'tilecount':1,
            'tiles':[{'id':0,'properties':[{'name':'collision','type':'string','value':'solid'}]}]}],
          'layers':[{'type':'tilelayer','name':'ground','data':[0]*132+[1]*12},{'type':'objectgroup','name':'actors','objects':[{'id':1,'name':'spawn','x':16,'y':16}]}]}
        self.file('map.tmj',json.dumps(tiled))
        # Atlas path is checked independently by graphical resource preflight.
        import shutil
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('main.lua', '''return {map='map.tmj',init=function()
assert(sc.objects()[1].name=='spawn'); assert(sc.map('ground',0,11)==1)
sc.map('ground',0,11,0); assert(sc.map('ground',0,11)==0)
sc.spawn({tag='p',x=16,y=20,w=6,h=8,body={type='dynamic'}})
end}''')
        p=self.run_game('--frames',100)['entities'][0]
        self.assertTrue(p['grounded']); self.assertAlmostEqual(p['y'],80,delta=.6)
    def test_reproducibility(self):
        self.file('main.lua',"return {entities={{x=40,y=30,w=8,h=8,body={type='dynamic'}}},update=function() sc.state.set('roll',sc.random()) end}")
        self.assertEqual(self.run_game('--frames',100),self.run_game('--frames',100))

    def test_tiled_navigation_edit_and_atomic_failure(self):
        cells=[0]*15
        for i in (2,7,12): cells[i]=1
        tiled={'orientation':'orthogonal','infinite':False,'width':5,'height':3,'tilewidth':8,'tileheight':8,
          'tilesets':[{'firstgid':1,'image':'atlas.png','tilewidth':8,'tileheight':8,'columns':2,'tilecount':2,
            'tiles':[{'id':0,'properties':[{'name':'collision','type':'string','value':'solid'}]}]}],
          'layers':[{'type':'tilelayer','name':'walls','data':cells}]}
        self.file('map.tmj',json.dumps(tiled))
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('main.lua', '''return {map='map.tmj',init=function()
assert(sc.navigation.path(0,1,4,1).status=='unreachable')
assert(sc.navigation.mask().rows[2]=='..#..')
local f,status=sc.navigation.flow(4,1); assert(status=='ok')
assert(sc.navigation.direction(f,4,12)==0)
sc.map('walls',2,1,0)
assert(sc.navigation.mask().rows[2]=='.....')
assert(select(3,sc.navigation.direction(f,4,12))=='stale')
assert(sc.navigation.path(0,1,4,1).status=='ok')
repeat status=sc.navigation.refresh(f,1) until status=='ok'
assert(sc.navigation.direction(f,4,12)>0)
sc.map('walls',2,1,2) -- Artwork without collision does not change passability.
assert(select(3,sc.navigation.direction(f,4,12))=='ok')
assert(not pcall(sc.map,'walls',2,1,999))
assert(sc.map('walls',2,1)==2 and sc.navigation.mask().rows[2]=='.....'
    and select(3,sc.navigation.direction(f,4,12))=='ok')
sc.map('walls',2,1,1)
assert(select(3,sc.navigation.direction(f,4,12))=='stale')
assert(sc.navigation.path(0,1,4,1).status=='unreachable')
assert(sc.navigation.refresh(f,1)=='budget_exhausted')
sc.map('walls',2,1,0) -- Discard partially rebuilt distances from the previous topology.
assert(select(3,sc.navigation.direction(f,4,12))=='stale')
repeat status=sc.navigation.refresh(f,2) until status=='ok'
assert(sc.navigation.direction(f,4,12)>0)
sc.debug.watch('tiled_navigation',true)
end}''')
        self.assertTrue(self.run_game('--frames',0)['watches']['tiled_navigation'])

    def test_tiled_edit_updates_custom_navigation_region(self):
        solid=[{'name':'collision','type':'string','value':'solid'}]
        cells=[0]*15
        for index in (2,7,12): cells[index]=1
        tiled={'orientation':'orthogonal','infinite':False,'width':5,'height':3,'tilewidth':8,'tileheight':8,
          'tilesets':[{'firstgid':1,'image':'atlas.png','tilewidth':8,'tileheight':8,'columns':1,'tilecount':1,
            'tiles':[{'id':0,'properties':solid}]}],
          'layers':[{'type':'tilelayer','name':'walls','data':cells}]}
        self.file('map.tmj',json.dumps(tiled))
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('main.lua', '''return {map='map.tmj',init=function()
local nav=sc.navigation
nav.region(8,0,{'......','......','......','......','......','.....#'},4)
local f=nav.flow(5,2)
assert(nav.path(0,2,5,2).status=='unreachable')
sc.map('walls',2,1,0)
assert(nav.path(0,2,5,2).status=='ok','custom region kept a removed wall')
assert(select(3,nav.direction(f,10,10))=='stale')
assert(nav.refresh(f,64)=='ok' and nav.direction(f,10,10)>0)
sc.map('walls',0,0,1) -- Outside the region: keep its valid flow.
assert(select(3,nav.direction(f,10,10))=='ok')
assert(nav.path(5,5,5,2).status=='unreachable') -- Authored blocker is preserved.
assert(not pcall(sc.map,'walls',2,1,0x10000001))
assert(sc.map('walls',2,1)==0 and select(3,nav.direction(f,10,10))=='ok')
sc.map('walls',2,1,1)
assert(nav.path(0,2,5,2).status=='unreachable','custom region missed a new wall')
assert(select(3,nav.direction(f,10,10))=='stale')
assert(nav.refresh(f,64)=='ok' and nav.direction(f,10,10)==0)
nav.region()
assert(nav.path(0,0,0,0).status=='unreachable') -- Room grid updated too.
assert(nav.path(0,1,4,1).status=='unreachable')
sc.debug.watch('custom_region_edit',true)
end}''')
        self.assertTrue(self.run_game('--frames',0)['watches']['custom_region_edit'])

    def test_tiled_object_terrain_survives_tile_edits(self):
        solid=[{'name':'collision','type':'string','value':'solid'}]
        cells=[0]*15; cells[7]=1
        tiled={'orientation':'orthogonal','infinite':False,'width':5,'height':3,'tilewidth':8,'tileheight':8,
          'tilesets':[{'firstgid':1,'image':'atlas.png','tilewidth':8,'tileheight':8,'columns':1,'tilecount':1,
            'tiles':[{'id':0,'properties':solid}]}],
          'layers':[{'type':'tilelayer','name':'door','data':cells},
            {'type':'objectgroup','name':'walls','offsetx':8,'objects':[
                {'id':1,'x':8,'y':0,'width':8,'height':8,'properties':solid},
                {'id':2,'x':8,'y':16,'width':8,'height':8,'properties':solid},
                {'id':3,'name':'spawn','x':0,'y':8,'width':32,'height':8}]}]}
        self.file('map.tmj',json.dumps(tiled))
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('main.lua', '''return {map='map.tmj',gravity=0,init=function()
assert(sc.objects()[1].x==16 and sc.objects()[3].x==8)
assert(sc.navigation.path(0,1,4,1).status=='unreachable')
local f=sc.navigation.flow(4,1)
sc.map('door',2,1,0)
assert(select(3,sc.navigation.direction(f,4,12))=='stale')
assert(sc.navigation.refresh(f,64)=='ok')
assert(sc.navigation.path(0,1,4,1).status=='ok')
assert(sc.navigation.path(2,0,4,1).status=='unreachable')
assert(sc.navigation.path(2,2,4,1).status=='unreachable')
local hit=sc.physics.ray(4,4,30,0)
assert(hit and hit.id==0 and math.abs(hit.x-16)<.1)
assert(not sc.physics.ray(4,12,30,0))
assert(not pcall(sc.map,'door',2,1,999))
assert(sc.map('door',2,1)==0 and select(3,sc.navigation.direction(f,4,12))=='ok')
sc.projectiles.configure(8)
sc.projectiles.spawn{{x=4,y=4,vx=1800,radius=1},{x=4,y=12,vx=1800,radius=1}}
end,update=function()
if sc.tick()==1 then
 local hits=sc.projectiles.hits()
 assert(#hits==1 and hits[1].target==0 and math.abs(hits[1].fraction-11/30)<.0001)
 assert(sc.projectiles.count()==1)
 sc.debug.watch('object_terrain',true)
end
end}''')
        self.assertTrue(self.run_game('--frames',2)['watches']['object_terrain'])

    def test_tiled_object_geometry_and_diagnostics(self):
        def collision(value): return [{'name':'collision','type':'string','value':value}]
        objects=[{'id':1,'x':16,'y':0,'width':8,'height':16,'rotation':90,'ellipse':False,'properties':collision('solid')},
            {'id':2,'x':0,'y':8,'polygon':[{'x':0,'y':0},{'x':16,'y':0},{'x':0,'y':16}],
                'properties':collision('solid')},
            {'id':3,'x':24,'y':9,'width':8,'height':10,'properties':collision('one_way')}]
        tiled={'orientation':'orthogonal','infinite':False,'width':5,'height':4,'tilewidth':8,'tileheight':8,
            'tilesets':[],'layers':[{'type':'objectgroup','name':'geometry','objects':objects}]}
        self.file('map.tmj',json.dumps(tiled))
        self.file('main.lua', '''return {map='map.tmj',init=function()
local function blocked(x,y) return sc.navigation.path(x,y,x,y).status=='unreachable' end
assert(blocked(0,0) and blocked(1,0) and not blocked(2,0))
assert(blocked(0,1) and blocked(1,1) and blocked(0,2) and not blocked(1,2))
assert(blocked(3,1) and not blocked(3,2))
local hit=sc.physics.ray(28,1,0,20)
assert(hit and hit.id==0 and math.abs(hit.y-9)<.1)
sc.navigation.region(0,0,{'.....','.....','.....','.....'},8)
assert(blocked(0,0) and not blocked(1,2))
sc.debug.watch('object_geometry',true)
end}''')
        self.assertTrue(self.run_game('--frames',0)['watches']['object_geometry'])
        self.file('main.lua', "return {map='map.tmj'}")
        invalid=[{'width':0}, {'ellipse':True}, {'point':True}, {'gid':1},
            {'properties':collision('typo')}, {'properties':collision(True)},
            {'properties':collision('one_way'),'rotation':45},
            {'x':1000001}, {'rotation':'90'},
            {'polygon':[{'x':0,'y':0},{'x':16,'y':0},{'x':8,'y':4},{'x':16,'y':16},{'x':0,'y':16}]}]
        base={'id':7,'x':8,'y':8,'width':8,'height':8,'properties':collision('solid')}
        for patch in invalid:
            with self.subTest(patch=patch):
                tiled['layers'][0]['objects']=[dict(base,**patch)]
                self.file('map.tmj',json.dumps(tiled))
                error=self.run_game('--check',ok=False)
                self.assertIn('map.tmj',error)
                self.assertRegex(error,r'layer geometry, object 7(?:\.0)? at \(')

    def test_tiled_navigation_polygon_flip_and_offset(self):
        tiled={'orientation':'orthogonal','infinite':False,'width':3,'height':3,'tilewidth':8,'tileheight':8,
          'tilesets':[{'firstgid':1,'image':'atlas.png','tilewidth':8,'tileheight':8,'columns':1,'tilecount':1,
            'tiles':[{'id':0,'objectgroup':{'objects':[{'x':0,'y':0,'polygon':[
                {'x':0,'y':0},{'x':16,'y':0},{'x':0,'y':16}]}]}}]}],
          'layers':[{'type':'tilelayer','name':'shape','data':[1,0,0,0,0,0,0,0,0]}]}
        self.file('map.tmj',json.dumps(tiled))
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('main.lua', '''return {map='map.tmj',init=function()
assert(sc.navigation.path(1,1,2,2).status=='ok') -- Empty corner inside the triangle's bounding box.
assert(sc.navigation.path(0,1,2,2).status=='unreachable')
sc.map('shape',0,0,0); sc.map('shape',1,1,0x80000001)
assert(sc.navigation.path(0,2,0,2).status=='ok') -- Mirrored triangle's empty corner.
assert(sc.navigation.path(0,1,2,2).status=='unreachable')
assert(sc.navigation.path(1,2,2,2).status=='unreachable')
sc.debug.watch('polygon_navigation',true)
end}''')
        self.assertTrue(self.run_game('--frames',0)['watches']['polygon_navigation'])
        tiled['tilesets'][0]['tiles']=[{'id':0,'properties':[{'name':'collision','type':'string','value':'solid'}]}]
        tiled['layers'][0]['offsetx']=4
        self.file('map.tmj',json.dumps(tiled))
        self.file('main.lua', '''return {map='map.tmj',init=function()
assert(sc.navigation.path(0,0,2,2).status=='unreachable')
assert(sc.navigation.path(1,0,2,2).status=='unreachable')
assert(sc.navigation.path(2,0,2,2).status=='ok')
sc.debug.watch('offset_navigation',true)
end}''')
        self.assertTrue(self.run_game('--frames',0)['watches']['offset_navigation'])

    def test_projectiles_tiled_polygon_edit_and_flip(self):
        cells=[0]*16; cells[5]=1
        tiled={'orientation':'orthogonal','infinite':False,'width':4,'height':4,'tilewidth':16,'tileheight':16,
          'tilesets':[{'firstgid':1,'image':'atlas.png','tilewidth':16,'tileheight':16,'columns':1,'tilecount':1,
            'tiles':[{'id':0,'objectgroup':{'objects':[{'x':0,'y':0,'polygon':[
                {'x':0,'y':0},{'x':16,'y':16},{'x':0,'y':16}]}]}}]}],
          'layers':[{'type':'tilelayer','name':'slope','data':cells}]}
        self.file('map.tmj',json.dumps(tiled))
        shutil.copy(ROOT/'examples/lantern/assets/keeper.png',self.root/'atlas.png')
        self.file('main.lua', '''return {map='map.tmj',gravity=0,init=function()
sc.projectiles.configure(8)
sc.projectiles.spawn{{x=4,y=24,vx=2400,radius=1}}
end,update=function()
if sc.tick()==1 then
    local hits=sc.projectiles.hits()
    assert(#hits==1 and hits[1].target==0 and math.abs(hits[1].fraction-.275)<.0001)
    sc.map('slope',1,1,0)
    sc.projectiles.spawn{{x=4,y=24,vx=2400,radius=1}}
elseif sc.tick()==2 then
    assert(#sc.projectiles.hits()==0 and sc.projectiles.count()==1)
    sc.projectiles.clear(); sc.map('slope',1,1,0x80000001)
    sc.projectiles.spawn{{x=44,y=24,vx=-2400,radius=1}}
elseif sc.tick()==3 then
    local hits=sc.projectiles.hits()
    assert(#hits==1 and hits[1].target==0 and math.abs(hits[1].fraction-.275)<.0001)
    sc.debug.watch('projectile_terrain',true)
end
end}''')
        self.assertTrue(self.run_game('--frames',4)['watches']['projectile_terrain'])

    def test_compound_geometry_and_atomic_patch(self):
        self.file('main.lua', '''local p
return {init=function()
p=sc.spawn({x=40,y=30,w=20,h=12,body={type='dynamic',shapes={
 {shape='box',w=12,h=12},{shape='circle',x=12,w=8,h=8}}}})
local old=sc.get(p); assert(#old.body.shapes==2)
for _,vertices in ipairs({{0,0,1,1,2,2},{0,0,20,0,10,5,20,20,0,20}}) do
 assert(not pcall(sc.set,p,{x=99,body={shape='polygon',shapes={{shape='polygon',vertices=vertices}}}}))
 assert(sc.get(p).x==40); assert(#sc.get(p).body.shapes==2)
end
old.body.shapes[1].w=99; assert(sc.get(p).body.shapes[1].w==12)
end}''')
        x=self.run_game('--frames',120)
        self.assertTrue(x['entities'][0]['grounded'])

    def test_moving_platform_and_slope_contacts(self):
        self.file('main.lua', '''local p,platform,s
return {init=function()
platform=sc.spawn({x=20,y=140,w=80,h=6,vy=-12,body={type='kinematic',one_way=true}})
p=sc.spawn({tag='rider',x=40,y=115,w=10,h=16,body={type='dynamic'}})
sc.spawn({x=160,y=100,w=80,h=60,body={type='static',friction=10,shape='polygon',vertices={0,60,80,0,80,60}}})
s=sc.spawn({x=200,y=60,w=8,h=12,body={type='dynamic',friction=10}})
end,update=function()
if sc.get(s).grounded and math.abs(sc.get(s).normal_x)>.2 then sc.state.set('slope',true) end
if sc.tick()==80 then assert(sc.get(p).support==platform); assert(sc.get(p).grounded) end
end}''')
        x=self.run_game('--frames',90)
        self.assertTrue(x['state']['slope'])
        self.assertLess(next(e for e in x['entities'] if e['tag']=='rider')['y'],110)

    def test_sensor_begin_end_and_collision_filters(self):
        self.file('main.lua', '''local p
return {gravity=0,init=function()
sc.spawn({x=40,y=40,w=24,h=24,body={type='static',sensor=true}})
p=sc.spawn({x=45,y=45,w=8,h=8,body={type='dynamic'}})
end,update=function()
for _,c in ipairs(sc.physics.contacts()) do if c.sensor then sc.state.set(c.phase,true) end end
if sc.tick()==3 then sc.set(p,{x=100}) end
end}''')
        self.assertEqual(self.run_game('--frames',10)['state'],{'begin':True,'end':True})

    def test_walking_up_slope_stays_grounded(self):
        self.file('main.lua', '''local p
return {init=function()
sc.spawn({x=0,y=180,w=384,h=20,body={type='static'}})
sc.spawn({x=160,y=120,w=80,h=60,body={type='static',shape='polygon',vertices={0,60,80,0,80,60}}})
p=sc.spawn({tag='walker',x=130,y=164,w=10,h=16,body={type='dynamic',shape='capsule',friction=0}})
end,update=function()
local e=sc.get(p);local vy=e.vy
if e.grounded and e.normal_y<-.5 then vy=-40*e.normal_x/e.normal_y end
if e.grounded and e.vy < -5 then sc.state.set('uphill',(sc.state.get('uphill') or 0)+1) end
sc.set(p,{vx=40,vy=vy})
end}''')
        x=self.run_game('--frames',130)
        walker=next(e for e in x['entities'] if e['tag']=='walker')
        self.assertTrue(walker['grounded']);self.assertGreater(x['state']['uphill'],40)

    def test_save_rejects_old_versions_and_recovers_backup(self):
        self.file('project.lua',"return {id='versions',data_version=2}")
        self.file('main.lua',"return {update=function() local ok,e=sc.save.load('slot'); if not ok then sc.state.set('error',e) end end}")
        save=self.root/'saves/versions/slot.json'; save.parent.mkdir(parents=True)
        record={'format':1,'project':'versions','data_version':2,'scene':'main.lua','state':{'coins':5}}
        save.write_text(json.dumps(record),encoding='utf-8')
        self.assertIn('unsupported save format',self.run_game('--frames',1,'--save-dir',self.root/'saves')['state']['error'])
        record['format']=3; record['data_version']=1
        save.write_text(json.dumps(record),encoding='utf-8')
        self.assertIn('unsupported save data',self.run_game('--frames',1,'--save-dir',self.root/'saves')['state']['error'])
        self.file('main.lua',"return {update=function() if sc.tick()==0 then assert(not sc.save.write('slot')); assert(sc.save.delete('slot')) end; sc.state.set('coins',sc.tick()+1); assert(sc.save.write('slot')) end}")
        self.run_game('--frames',2,'--save-dir',self.root/'saves')
        backup=Path(str(save)+'.bak')
        self.assertEqual(json.loads(backup.read_text())['state']['coins'],1)
        saved_backup=backup.read_bytes(); save.write_text('{broken')
        self.file('main.lua',"return {update=function() if not sc.state.get('coins') then assert(sc.save.load('slot')) end end}")
        self.assertEqual(self.run_game('--frames',2,'--save-dir',self.root/'saves')['state']['coins'],1)
        self.file('main.lua',"return {update=function() sc.state.set('coins',9); assert(sc.save.write('slot')) end}")
        self.run_game('--frames',1,'--save-dir',self.root/'saves')
        self.assertEqual(backup.read_bytes(),saved_backup)

    def test_save_slot_listing_read_and_delete(self):
        self.file('project.lua',"return {id='slots'}")
        self.file('main.lua',"""return {update=function()
 sc.state.set('coins',7); assert(sc.save.write('b')); assert(sc.save.write('a'))
 local slots=sc.save.list(); assert(#slots==2 and slots[1].slot=='a' and slots[2].slot=='b')
 assert(slots[1].valid and slots[1].saved_at and slots[1].frame==0)
 assert(sc.save.read('a').state.coins==7)
 assert(sc.save.delete('a')); assert(sc.save.read('a')==nil); assert(#sc.save.list()==1)
end}""")
        self.run_game('--frames',1,'--save-dir',self.root/'saves')
        self.assertFalse((self.root/'saves/slots/a.json').exists())
        self.run_game('--frames',1)

    def test_required_module_validation(self):
        self.file('main.lua','return {}')
        self.file('project.lua',"return {modules={'physics','navigation'}}")
        self.run_game('--check-all')
        self.file('project.lua',"return {modules={'unknown'}}")
        self.assertIn('unknown required module',self.run_game('--check-all',ok=False))
        api=json.loads(subprocess.check_output([str(BINARY),'--api'],encoding='utf-8'))
        enabled=api['modules']['advanced_render']
        for name in ['materials','postprocessing','geometry_shadows','normal_maps']:
            self.assertEqual(api['modules'][name],enabled,name)
        self.file('project.lua',"return {modules={'advanced_render'}}")
        if enabled:
            self.run_game('--check-all')
        else:
            self.assertIn('required module unavailable: advanced_render',self.run_game('--check-all',ok=False))

    def test_persistent_network_candidate_guard(self):
        api=json.loads(subprocess.check_output([str(BINARY),'--api'],encoding='utf-8'))
        if not api['network']['available']: self.skipTest('network disabled')
        self.file('main.lua',"""local h
return {init=function() h=assert(sc.net.host('127.0.0.1',0)); assert(h:persist('coop')); sc.state.set('port',h:port()) end,
update=function() sc.scene('next.lua') end}""")
        self.file('next.lua',"""local h
return {init=function()
 h=assert(sc.net.bind('coop')); assert(h:port()==sc.state.get('port'))
 local ok,e=pcall(h.flush,h); assert(not ok and e:find('candidate initialization'))
 ok,e=pcall(h.close,h); assert(not ok and e:find('candidate initialization'))
 sc.state.set('rebound',true)
end,update=function() assert(h:flush()) end}""")
        self.assertTrue(self.run_game('--frames',3)['state']['rebound'])

    def test_audio_handles_pause_fade_and_room_persistence(self):
        for name in ['chime.wav','theme.ogg']: shutil.copy(ROOT/'examples/workshop/assets'/name,self.root/name)
        self.file('project.lua',"return {resources={s={type='sound',path='chime.wav'},m={type='music',path='theme.ogg'}}}")
        self.file('main.lua', '''local music,sound
return {init=function()
music=assert(sc.audio.play('m',{loop=true,paused=true,persistent=true,volume=.4}))
sound=assert(sc.audio.play('s',{loop=true,volume=.5}))
assert(not pcall(sc.audio.set,sound,{volume=2}))
end,update=function()
if sc.tick()==2 then sc.audio.stop(sound,.05) end
if sc.tick()==8 then assert(not pcall(sc.audio.stop,sound)); sc.scene('second.lua') end
end}''')
        self.file('second.lua','return {}')
        x=self.run_game('--frames',12)
        self.assertEqual(len(x['audio']),1)
        self.assertTrue(x['audio'][0]['paused']);self.assertEqual(x['audio'][0]['position'],0)
        self.assertAlmostEqual(x['audio'][0]['volume'],.4,places=4)

    def test_audio_buses_priority_and_age(self):
        shutil.copy(ROOT/'examples/workshop/assets/chime.wav',self.root/'chime.wav')
        self.file('project.lua',"return {limits={sound_voices=2},resources={s={type='sound',path='chime.wav'}}}")
        self.file('main.lua',"""local first,second,last
return {init=function()
 first=assert(sc.audio.play('s',{loop=true,priority=2,pan=-1,bus='ui'}))
 second=assert(sc.audio.play('s',{loop=true,priority=2,pan=1,bus='ui'}))
 assert(sc.audio.play('s',{priority=1})==nil)
 last=assert(sc.audio.play('s',{loop=true,priority=2,bus='ui'}))
 assert(not pcall(sc.audio.set,first,{volume=.5})); sc.audio.set(second,{volume=.5})
 sc.audio.bus('ui',{paused=true}); sc.audio.bus('master',{volume=.5,fade=.1})
end,update=function()
 if sc.tick()==10 then
  assert(sc.audio.bus('master').volume==.5)
  assert(sc.audio.bus('ui').paused)
  sc.scene('second.lua')
 end
end}""")
        x=self.run_game('--frames',10)
        self.assertEqual(len(x['audio']),2)
        self.assertTrue(all(v['position']==0 for v in x['audio']))
        self.file('second.lua',"return {init=function() assert(sc.audio.bus('master').volume==.5 and sc.audio.bus('ui').paused) end}")
        self.assertEqual(self.run_game('--frames',12)['scene'],'second.lua')

    def test_utf8_measure_wrap_and_bad_font(self):
        shutil.copy(ROOT/'examples/workshop/assets/workshop.ttf',self.root/'font.ttf')
        self.file('project.lua',"return {resources={ui={type='font',path='font.ttf',size=16,characters='星灯'}}}")
        self.file('main.lua', '''return {init=function()
local w,h=sc.measure('星灯',16,'ui'); assert(w>16 and h==16)
local narrow,tall=sc.measure('星灯',16,'ui',w/2); assert(narrow<=w/2+.01 and tall==32)
assert(not pcall(sc.measure,'x',16,'typo'))
assert(not pcall(sc.measure,string.char(255),16))
sc.state.set('width',w)
end,draw=function() sc.text('星灯',10,10,16,'#ffffff',true,{font='ui',wrap=40,align=1}) end}''')
        self.assertGreater(self.run_game('--frames',0)['state']['width'],16)
        self.file('font.ttf','broken font')
        self.assertIn('font',self.run_game('--check',ok=False))

    def test_tiled_rejects_unsupported_semantics(self):
        tiled=json.loads((ROOT/'examples/workshop/workshop.tmj').read_text(encoding='utf-8'))
        shutil.copytree(ROOT/'examples/workshop/assets',self.root/'assets')
        self.file('main.lua',"return {map='map.tmj'}")
        for field,value in [('orientation','isometric'),('renderorder','left-up'),('infinite',True),('infinite','false')]:
            bad=dict(tiled); bad[field]=value;self.file('map.tmj',json.dumps(bad))
            self.run_game('--check',ok=False)
        tiled['layers'][0]['parallaxx']=.5;self.file('map.tmj',json.dumps(tiled))
        self.assertIn('parallax',self.run_game('--check',ok=False))

    def test_animation_clips_are_editable_lua(self):
        shutil.copy(ROOT/'examples/workshop/game/animation.lua',self.root/'animation.lua')
        self.file('main.lua', '''local A=require('animation')
return {init=function()
local a=A.new({walk={{frame=1,duration=.1},{frame=2,duration=.1,marker='step'},loop=false}})
A.play(a,'walk'); local frame,done,markers=A.update(a,.11)
assert(frame==2 and not done and markers[1]=='step')
frame,done=A.update(a,.2); assert(frame==2 and done)
end}''')
        self.run_game('--frames',0)

    def test_failed_save_preserves_previous_file(self):
        self.file('project.lua',"return {id='atomic'}")
        self.file('main.lua',"return {update=function() sc.state.set('coins',4); local ok,err=sc.save.write('slot'); if not ok then sc.state.set('error',err) end end}")
        saves=self.root/'saves';self.run_game('--frames',1,'--save-dir',saves)
        slot=saves/'atomic/slot.json';previous=slot.read_bytes()
        temporary=slot.with_suffix('.json.tmp');temporary.mkdir()
        self.assertIn('error',self.run_game('--frames',1,'--save-dir',saves)['state'])
        self.assertEqual(slot.read_bytes(),previous);self.assertTrue(temporary.is_dir())

    def test_max_depth_state_survives_disk_envelope(self):
        self.file('project.lua',"return {id='deep'}")
        self.file('main.lua',"return {update=function() local node={leaf=true}; for i=1,14 do node={child=node} end; sc.state.set('deep',node); assert(sc.save.write('slot')) end}")
        saves=self.root/'saves';saved=self.run_game('--frames',1,'--save-dir',saves)['state']
        self.file('main.lua',"return {update=function() if not sc.state.get('deep') then assert(sc.save.load('slot')) end end}")
        self.assertEqual(self.run_game('--frames',2,'--save-dir',saves)['state'],saved)

    def test_settings_persistence_and_candidate_guard(self):
        self.file('project.lua',"return {id='settings',display={width=960,height=540,scale='smooth'}}")
        self.file('main.lua',"""return {init=function()
 assert(sc.settings.get().scale=='smooth')
 assert(not pcall(sc.settings.apply,{width=640}))
end,update=function()
 local ok,err=sc.settings.apply({volume={master=2}}); assert(not ok and err)
 assert(sc.settings.get().width==960)
 assert(sc.settings.apply({width=1280,volume={master=.4},bindings={play={jump={{key='space'}}}}}))
 sc.scene('second.lua')
end}""")
        self.file('second.lua',"""return {init=function()
 assert(sc.settings.get().width==1280)
 assert(not pcall(sc.settings.apply,{width=640}))
 assert(sc.settings.get().bindings.play.jump[1].key=='space')
end}""")
        x=self.run_game('--frames',2,'--save-dir',self.root/'data')
        self.assertEqual(x['settings']['width'],1280)
        settings=self.root/'data/settings/config/settings.json'
        self.assertEqual(json.loads(settings.read_text())['settings']['width'],1280)
        self.file('main.lua',"return {init=function() assert(sc.settings.get().width==1280) end}")
        self.run_game('--frames',0,'--save-dir',self.root/'data')
        settings.write_text('{broken')
        self.file('main.lua',"return {init=function() assert(sc.settings.get().width==960 and #sc.settings.error()>0) end}")
        self.run_game('--frames',0,'--save-dir',self.root/'data')

    def test_record_trace_profile_and_clip_validation(self):
        self.file('main.lua',"return {update=function() sc.debug.watch('tick',sc.tick()) end}")
        record=self.root/'record.jsonl'; trace=self.root/'trace.jsonl'; profile=self.root/'profile.jsonl'
        self.run_game('--frames',3,'--record',record,'--trace',trace,'--profile',profile)
        self.assertEqual(len(record.read_text().splitlines()),4)
        self.assertEqual(len(trace.read_text().splitlines()),3)
        self.assertTrue(profile.stat().st_size>0)
        replayed=self.root/'replayed.jsonl'
        self.run_game('--frames',3,'--replay',record,'--trace',replayed)
        self.assertEqual(trace.read_bytes(),replayed.read_bytes())
        self.assertIn('already exists',self.run_game('--frames',1,'--trace',trace,ok=False))
        self.assertIn('mutually exclusive',self.run_game('--frames',1,'--record',self.root/'other','--replay',record,ok=False))
        for draw in ('sc.clip()', 'sc.clip(0,0,20,20)'):
            self.file('main.lua','return {draw=function() '+draw+' end}')
            self.assertIn('clip stack',self.run_game('--frames',0,ok=False))

    def test_api_metadata_matches_annotations(self):
        import re
        metadata=json.loads(subprocess.check_output([str(BINARY),'--api'],text=True,encoding='utf-8'))
        annotations=(ROOT/'docs/api.lua').read_text(encoding='utf-8')
        declared=set(re.findall(r'^function ([\w.:]+)\(',annotations,re.M))
        for function in metadata['functions']:
            self.assertTrue(function.get('signature'),function)
            self.assertTrue(function.get('description'),function)
            name=function['name']
            if name.startswith('ScNetSession:'): name=name.replace('ScNetSession:', 'session:')
            self.assertIn(name,declared)
        for key in metadata['entity_fields']+metadata['entity_readonly_fields']:
            self.assertRegex(annotations,r'---@field '+key+r'\?? ')

if __name__=='__main__': unittest.main(verbosity=2)

