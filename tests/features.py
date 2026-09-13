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

    def test_save_migration_and_rejected_result(self):
        self.file('project.lua',"return {id='migrate',data_version=2,migrate='game.migrate'}")
        self.file('main.lua',"return {update=function() if not sc.state.get('coins') then local ok,e=sc.save.load('slot'); if not ok then sc.state.set('error',e) end end end}")
        save=self.root/'saves/migrate/slot.json'; save.parent.mkdir(parents=True)
        record={'format':1,'project':'migrate','data_version':1,'scene':'main.lua','state':{'gold':5}}
        save.write_text(json.dumps(record),encoding='utf-8'); original=save.read_bytes()
        self.file('game/migrate.lua',"return function(old,new,data) assert(old==1 and new==2); assert(not pcall(sc.spawn,{})); return {coins=data.gold} end")
        self.assertEqual(self.run_game('--frames',2,'--save-dir',self.root/'saves')['state'],{'coins':5})
        self.assertEqual(save.read_bytes(),original)
        for result in ["{coins=string.char(255)}", "{coins=0/0}", "{coins=string.rep('x',270000)}", "{coins=string.rep('x',20000000)}"]:
            self.file('game/migrate.lua','return function() return '+result+' end')
            x=self.run_game('--frames',1,'--save-dir',self.root/'saves')
            self.assertIn('error',x['state']); self.assertNotIn('coins',x['state'])
            self.assertEqual(save.read_bytes(),original)
        record['state']={'coins':[None]};save.write_text(json.dumps(record),encoding='utf-8')
        self.assertIn('null',self.run_game('--frames',1,'--save-dir',self.root/'saves')['state']['error'])

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

