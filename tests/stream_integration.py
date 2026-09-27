"""Scheduled streaming through the host, fixed input recording and native rendering."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest
import queue
import threading
import time

BINARY = None
NATIVE = False


class Streaming(unittest.TestCase):
    def setUp(self):
        api = json.loads(subprocess.check_output([str(BINARY), '--api'], encoding='utf-8'))
        if not api['modules']['streaming']:
            self.skipTest('streaming is disabled in this build')
        self.temp = tempfile.TemporaryDirectory(prefix='shiny-stream-host-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        entries = []
        for x in range(3):
            data = json.dumps({'x': x, 'y': -1, 'layers': {'0': [x+1]*1024}, 'objects': []}, separators=(',', ':'))
            (self.root/f'{x}.json').write_text(data, encoding='utf-8')
            entries.append({'x': x, 'y': -1, 'path': f'{x}.json', 'bytes': len(data.encode())})
        (self.root/"input.replay").write_text("0 0\n", encoding="utf-8")
        self.index = {'format': 3, 'tilewidth': 8, 'tileheight': 8, 'layers': [{'type': 'tilelayer'}, {'type': 'objectgroup'}], 'chunk_size': 32, 'chunks': entries}
        self.write_index()
        (self.root/'project.lua').write_text('return {id="stream-test",modules={"streaming"}}', encoding='utf-8')

    def test_room_loader_isolation(self):
        other = self.root/'other'
        other.mkdir()
        for path in self.root.glob('*.json'):
            shutil.copyfile(path, other/path.name)
        # Same coordinates and byte length, different room payload.
        path = other/'0.json'
        chunk = json.loads(path.read_text(encoding='utf-8'))
        chunk['layers']['0'] = [7]*1024
        path.write_text(json.dumps(chunk, separators=(',', ':')), encoding='utf-8')
        self.scene('''return {init=function()
    sc.stream.open('index.json')
    sc.stream.request(0,-1,0)
    sc.stream.request(1,-1,100)
end,update=function()
    assert(sc.stream.get(0,-1).layers['0'][1]==1)
    sc.scene('next.lua')
end}''')
        (self.root/'next.lua').write_text('''return {init=function()
    sc.stream.open('other/index.json')
    assert(sc.stream.request(0,-1,0)==1)
end,update=function()
    assert(sc.stream.get(0,-1).layers['0'][1]==7)
    assert(sc.stream.stats().pinned==1 and sc.stream.stats().last_sequence==1)
    sc.debug.watch('new_room',true)
end}''', encoding='utf-8')
        result = self.run_game('room-loader')
        self.assertTrue(result['watches']['new_room'])

    def test_interest_regions(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.scene('''local Regions=require("shiny.stream_regions")
local region=Regions.new{tilewidth=8,tileheight=8,margin=0,capacity=4}
return {init=function()
    sc.stream.open("index.json")
    local plan=Regions.request(region,{{x=-1,y=-1},{x=8,y=-8},{x=8,y=-8}},0)
    assert(#plan.enter==2 and plan.enter[1].x==-1 and plan.enter[2].x==0)
    assert(sc.stream.stats().pinned==1)
end,update=function()
    local tick=sc.tick()
    if tick==0 then
        assert(not Regions.contains(region,{x=8,y=-8}))
        assert(#Regions.ready(region)==2); Regions.commit(region)
        assert(Regions.contains(region,{x=-1,y=-1,w=257,h=0}))
        assert(not Regions.contains(region,{x=0,y=-1,w=257,h=0}))
        assert(not pcall(Regions.request,region,{{x=0,y=0,w=5000}},1))
        assert(sc.stream.stats().pinned==1)
        local plan=Regions.request(region,{{x=8,y=-8},{x=256,y=-8}},1)
        assert(#plan.enter==1 and #plan.leave==1)
        assert(Regions.ready(region)==nil)
        assert(not Regions.contains(region,{x=256,y=-8}))
    elseif tick==1 then
        assert(#Regions.ready(region)==1); Regions.commit(region)
        assert(sc.stream.stats().pinned==2 and Regions.contains(region,{x=256,y=-8}))
        Regions.request(region,{},2); Regions.cancel(region)
        assert(sc.stream.stats().pinned==2)
    elseif tick==2 then
        Regions.request(region,{{x=512,y=-8}},3); Regions.cancel(region)
        assert(sc.stream.stats().pinned==2)
    elseif tick==3 then
        Regions.request(region,{{x=512,y=-8}},4)
    elseif tick==4 then
        assert(#Regions.ready(region)==1); Regions.commit(region)
        assert(sc.stream.stats().pinned==1)
        assert(not Regions.contains(region,{x=8,y=-8}))
        assert(Regions.contains(region,{x=512,y=-8}))
    end
end}''')
        self.run_game('interest-regions')

    def test_cross_chunk_object_anchor_coverage(self):
        self.index['object_coverage']=[{'x':1,'y':-1,'anchors':[{'x':0,'y':-1}]}]
        self.write_index()
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.scene('''local Regions=require("shiny.stream_regions")
local region
return {init=function()
    sc.stream.open("index.json")
    local coverage=sc.stream.metadata().object_coverage
    assert(#coverage==1 and coverage[1].anchors[1].x==0)
    local small=Regions.new{tilewidth=8,tileheight=8,margin=0,capacity=1,coverage=coverage}
    assert(not pcall(Regions.request,small,{{x=264,y=-8}},0))
    assert(sc.stream.stats().pinned==0)
    region=Regions.new{tilewidth=8,tileheight=8,margin=0,capacity=3,boundary=true,coverage=coverage}
    local plan=Regions.request(region,{{x=264,y=-8}},0)
    assert(#plan.enter==2 and plan.enter[1].x==0 and plan.enter[2].x==1)
end,update=function()
    if sc.tick()==0 then
        assert(#Regions.ready(region)==2); Regions.commit(region)
        assert(sc.stream.stats().pinned==2)
        assert(Regions.contains(region,{x=264,y=-8}))
        assert(not Regions.contains(region,{x=8,y=-8}))
        local left=false
        for _,id in ipairs(region.walls) do if sc.get(id).x==254 then left=true end end
        assert(left and #region.walls==4) -- The owner pin does not open this loading edge.
        local plan=Regions.request(region,{{x=8,y=-8}},1)
        assert(#plan.enter==0 and #plan.leave==1 and plan.leave[1].x==1)
    elseif sc.tick()==1 then
        Regions.ready(region); Regions.commit(region)
        assert(sc.stream.stats().pinned==1)
        assert(Regions.contains(region,{x=8,y=-8}))
        assert(not Regions.contains(region,{x=264,y=-8}))
        local right=false
        for _,id in ipairs(region.walls) do if sc.get(id).x==256 then right=true end end
        assert(right and #region.walls==4)
        sc.debug.watch("coverage",true)
    end
end}''')
        self.assertTrue(self.run_game('object-coverage',frames=2)['watches']['coverage'])

    def test_stream_world_loads_cross_chunk_owner(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.index['layers']=[{'type':'objectgroup'}]
        self.index['tilesets']=[]
        self.index['object_coverage']=[{'x':1,'y':-1,'anchors':[{'x':0,'y':-1}]}]
        for x in range(2):
            data=json.dumps({'x':x,'y':-1,'layers':{},'objects':[
                {'persistent_id':'bridge:1','layer':0,'x':248,'y':-8,'width':24,'height':8}
            ] if x==0 else []})
            (self.root/f'{x}.json').write_text(data,encoding='utf-8')
            self.index['chunks'][x]['bytes']=len(data.encode())
        self.write_index()
        self.scene('''local World=require("shiny.stream_world")
local world
return {init=function()
    world=World.new{index="index.json",name="bridge",slot="slot",margin=0,
        boundary=false,navigation=false,
        prepare=function(object,saved) return {x=saved and saved.x or object.x,y=object.y},{} end,
        export=function(entity) return {x=entity.x} end}
    local plan=World.request(world,{{x=264,y=-8}},0)
    assert(#plan.enter==2)
end,update=function(dt)
    local _,err,event=World.update(world,dt)
    assert(not err,err)
    if event=="published" then
        assert(World.contains(world,{x=264,y=-8}))
        assert(not World.contains(world,{x=8,y=-8}))
        assert(sc.identity.resolve("bridge:1").status=="active")
        sc.debug.watch("covered",true)
    end
end}''')
        self.assertTrue(self.run_game('world-object-coverage',frames=30,save=True)['watches']['covered'])

    def test_physical_loading_boundary(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        (self.root/'project.lua').write_text('return {id="boundary",modules={"streaming"},limits={entities=14}}',encoding='utf-8')
        self.scene('''local Regions=require("shiny.stream_regions")
local region=Regions.new{tilewidth=8,tileheight=8,margin=0,boundary=true}
local player
local rows={}; for i=1,40 do rows[i]=string.rep(".",128) end
return {gravity=0,map={tile_size=8,rows=rows},init=function()
    sc.stream.open("index.json")
    Regions.request(region,{{x=128,y=128}},0)
    player=sc.spawn{x=240,y=128,w=8,h=8,body={type="dynamic",bullet=true,fixed_rotation=true}}
end,update=function()
    local tick=sc.tick()
    if tick==0 then
        assert(Regions.ready(region)); Regions.commit(region)
        assert(#sc.find_all("shiny.loading-boundary")==4)
    elseif tick==1 then
        assert(sc.get(player).x<=248.1,"body crossed unloaded boundary")
        Regions.request(region,{{x=0,y=128,w=512}},2)
    elseif tick==2 then
        assert(sc.get(player).x<=248.1)
        assert(Regions.ready(region)); Regions.commit(region)
        assert(#sc.find_all("shiny.loading-boundary")==6)
    elseif tick==3 then
        assert(sc.get(player).x>256,"old internal wall was not removed")
        Regions.request(region,{{x=0,y=128,w=768}},4)
    elseif tick==4 then
        assert(Regions.ready(region))
        assert(not pcall(Regions.commit,region),"expected replacement capacity failure")
        assert(#sc.find_all("shiny.loading-boundary")==6)
        assert(not Regions.contains(region,{x=600,y=128}))
        Regions.cancel(region)
    end
    sc.set(player,{vx=1200,vy=0})
end}''')
        self.run_game('physical-boundary')

    def test_streamed_tile_commands(self):
        from PIL import Image
        Image.new('RGBA',(16,8),(255,255,255,255)).save(self.root/'atlas.png')
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        (self.root/'project.lua').write_text('return {id="tiles",modules={"streaming"},resources={atlas={type="image",path="atlas.png"}}}',encoding='utf-8')
        self.index['tilesets']=[{'firstgid':1,'tilecount':2,'columns':2,'tilewidth':8,'tileheight':8,'image':'atlas.png',
            'tiles':[{'id':0,'animation':[{'tileid':0,'duration':100},{'tileid':1,'duration':100}]}]}]
        self.index['layers']=[{'type':'tilelayer','offsetx':4,'parallaxx':.5,'opacity':.5},
                              {'type':'tilelayer','visible':False}]
        cells=[0]*1024; cells[992]=0x80000001; cells[993]=2
        data=json.dumps({'x':0,'y':-1,'layers':{'0':cells,'1':cells},'objects':[]})
        (self.root/'0.json').write_text(data,encoding='utf-8')
        self.index['chunks'][0]['bytes']=len(data.encode()); self.write_index()
        self.scene('''local Tiles=require("shiny.stream_tiles")
local view,prepared,calls
local image=sc.image
sc.image=function(resource,x,y,w,h,options)
    calls[#calls+1]={x=x,y=y,source=options.source_x,flip=options.flip_x,color=options.color}
    assert(options.layer==0 and not options.screen)
    return image(resource,x,y,w,h,options)
end
return {init=function()
    sc.camera.set{bounds=false,x=8,y=-8,zoom=.5,rotation=.5,pixel_snap=false}
    sc.stream.open("index.json")
    local metadata=sc.stream.metadata()
    assert(metadata.chunks==nil and metadata.objects==nil and metadata.tilewidth==8)
    view=Tiles.new(metadata,{["atlas.png"]="atlas"})
    metadata.layers[1].opacity=0
    assert(sc.stream.metadata().layers[1].opacity==.5)
    sc.stream.request(0,-1,0)
end,update=function()
    if not prepared then
        local chunk=sc.stream.get(0,-1)
        prepared=Tiles.prepare(view,{chunk})
        assert(not pcall(Tiles.prepare,view,{chunk,chunk}))
        chunk.layers["0"][993]=99
        assert(not pcall(Tiles.prepare,view,{chunk}))
    end
    Tiles.update(view,.1)
end,draw=function()
    if not prepared then return end
    assert(not pcall(sc.stream.metadata))
    calls={}; Tiles.draw(view,prepared,{x=8,y=-8,visible={x=8,y=-8,w=32,h=16}})
    assert(#calls==2 and calls[1].x==8 and calls[2].x==16 and calls[1].y==-8)
    assert(calls[1].flip and calls[1].color=="#FFFFFF80")
    assert(calls[1].source==((math.floor(view.time/100)%2)*8) and calls[2].source==8)
    calls={}; Tiles.draw(view,prepared); assert(#calls==2 and calls[1].x==8)
    calls={}; Tiles.draw(view,prepared,{x=8,y=-8,anchor_x=16,anchor_y=-8,visible={x=8,y=-8,w=32,h=16}})
    assert(#calls==2 and calls[1].x==12) -- Display anchor drives parallax, not fixed x.
    calls={}; Tiles.draw(view,prepared,{x=800,y=800,visible={x=800,y=800,w=32,h=16}}); assert(#calls==0)
end}''')
        self.run_game('streamed-tile-commands')

    def test_tile_objects_draw_order_rotation_and_deletion(self):
        from PIL import Image
        Image.new('RGBA',(16,8),(255,255,255,255)).save(self.root/'atlas.png')
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        (self.root/'project.lua').write_text('return {id="tile-objects",resources={atlas={type="image",path="atlas.png"}}}',encoding='utf-8')
        self.scene('''local Tiles=require("shiny.stream_tiles")
local view,prepared,calls={},nil,{}
local image=sc.image
sc.image=function(name,x,y,w,h,options)
    calls[#calls+1]={x=x,y=y,angle=options.angle,source=options.source_x}
    return image(name,x,y,w,h,options)
end
return {init=function()
    view=Tiles.new({format=3,chunk_size=32,tilewidth=8,tileheight=8,
        tilesets={{firstgid=1,tilecount=2,columns=2,tilewidth=8,tileheight=8,image="atlas.png",objectalignment="center"}},
        layers={{type="objectgroup",draworder="index"}}},{["atlas.png"]="atlas"})
    prepared=Tiles.prepare(view,{{x=0,y=0,layers={},objects={
        {persistent_id="flower:2",layer=0,draw_order=1,gid=2,x=40,y=20,width=8,height=8},
        {persistent_id="flower:1",layer=0,draw_order=0,gid=1,x=50,y=40,width=8,height=8,rotation=90}}}})
    assert(Tiles.images(view,prepared)[1]=="atlas" and #Tiles.terrain(view,prepared)==0)
end,update=function()
    if sc.tick()>=2 and sc.identity.resolve("flower:1").status~="deleted" then sc.identity.remove("flower:1") end
end,draw=function()
    calls={}; Tiles.draw(view,prepared,{x=0,y=0,visible={x=0,y=0,w=100,h=100}})
    if sc.identity.resolve("flower:1").status~="deleted" then
        assert(#calls==2 and calls[1].x==46 and calls[1].y==36)
        assert(math.abs(calls[1].angle-math.pi/2)<.00001 and calls[1].source==0)
        assert(calls[2].x==36 and calls[2].source==8)
    else assert(#calls==1 and calls[1].source==8) end
end}''')
        self.run_game('tile-objects',frames=4)

    def test_built_image_layers(self):
        from PIL import Image
        Image.new('RGBA',(8,8),(255,255,255,255)).save(self.root/'background.png')
        Image.new('RGBA',(5000,8),(255,255,255,255)).save(self.root/'wide.png')
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        spec=importlib.util.spec_from_file_location('assets',Path(__file__).resolve().parents[1]/'tools/assets.py')
        assets=importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
        data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,'parallaxoriginx':4,'layers':[
            {'type':'group','name':'backgrounds','opacity':.5,'layers':[
                {'type':'imagelayer','name':'sky','image':'background.png','repeatx':True,'parallaxx':.5}]},
            {'type':'imagelayer','name':'wide','image':'wide.png','parallaxx':0},
            {'type':'imagelayer','name':'hidden','image':'background.png','visible':False}]}
        (self.root/'map.json').write_text(json.dumps(data),encoding='utf-8')
        (self.root/'assets.json').write_text(json.dumps({'maps':{'world':'map.json'}}),encoding='utf-8')
        built=assets.build(self.root/'assets.json',self.root/'built')
        index=(built/'map-world/index.json').relative_to(self.root).as_posix()
        (self.root/'project.lua').write_text('return {id="images",modules={"streaming"},resources={bg={type="image",path="background.png"},wide={type="image",path="wide.png"}}}',encoding='utf-8')
        self.scene('''local Tiles=require("shiny.stream_tiles")
local view,prepared,wide,wide_prepared,calls
local image=sc.image
sc.image=function(resource,x,y,w,h,options)
    calls[#calls+1]={resource=resource,x=x,w=w,sx=options.source_x,color=options.color}
    return image(resource,x,y,w,h,options)
end
return {init=function()
    sc.stream.open("'''+index+'''")
    local metadata=sc.stream.metadata()
    assert(metadata.layers[1].imagewidth==8 and metadata.layers[2].imagewidth==5000)
    assert(metadata.parallaxoriginx==4 and metadata.layers[1].repeatx)
    view=Tiles.new(metadata,{["background.png"]="bg",["wide.png"]="wide"})
    prepared=Tiles.prepare(view,{})
    metadata.layers={metadata.layers[2]}; metadata.parallaxoriginx=0
    wide=Tiles.new(metadata,{["wide.png"]="wide"}); wide_prepared=Tiles.prepare(wide,{})
end,draw=function()
    calls={}; Tiles.draw(view,prepared,{x=.5,y=0,visible={x=.5,y=0,w=16,h=8}})
    assert(#calls==4 and calls[1].resource=="bg" and calls[4].resource=="wide")
    assert(calls[1].sx==2.25 and calls[1].x==.5 and calls[2].x==6.25 and calls[3].w==2.25)
    assert(calls[1].color=="#FFFFFF80" and calls[4].sx==4)
    calls={}; Tiles.draw(wide,wide_prepared,{x=0,y=0,visible={x=0,y=0,w=5000,h=8}})
    assert(#calls==2 and calls[1].w==4096 and calls[2].w==904 and calls[2].sx==4096)
end}''')
        self.run_game('built-image-layers')
        (self.root/'background.png').write_bytes(b'not PNG')
        with self.assertRaisesRegex(ValueError,'map.json:backgrounds/sky: image layer requires a PNG'):
            assets.build(self.root/'assets.json',self.root/'invalid')

    def test_collection_tileset(self):
        from PIL import Image
        folder=self.root/'tiles'; folder.mkdir()
        Image.new('RGBA',(8,8),(255,0,0,255)).save(folder/'small.png')
        Image.new('RGBA',(12,16),(0,255,0,255)).save(folder/'tall.png')
        tileset={'name':'objects','columns':0,'tilecount':2,'tilewidth':12,'tileheight':16,'tiles':[
            {'id':0,'image':'small.png','animation':[{'tileid':0,'duration':100},{'tileid':3,'duration':100}]},
            {'id':3,'image':'tall.png'}]}
        (folder/'objects.tsj').write_text(json.dumps(tileset),encoding='utf-8')
        data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
            'tilesets':[{'firstgid':1,'source':'tiles/objects.tsj'}],
            'layers':[{'type':'tilelayer','name':'ground','chunks':[{'x':-1,'y':0,'width':2,'height':1,'data':[0x80000001,4]}]}]}
        (self.root/'map.json').write_text(json.dumps(data),encoding='utf-8')
        (self.root/'assets.json').write_text(json.dumps({'maps':{'world':'map.json'}}),encoding='utf-8')
        spec=importlib.util.spec_from_file_location('assets',Path(__file__).resolve().parents[1]/'tools/assets.py')
        assets=importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
        built=assets.build(self.root/'assets.json',self.root/'built')
        index=(built/'map-world/index.json').relative_to(self.root).as_posix()
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        (self.root/'project.lua').write_text('return {id="collection",modules={"streaming"},resources={small={type="image",path="tiles/small.png"},tall={type="image",path="tiles/tall.png"}}}',encoding='utf-8')
        self.scene('''local Tiles=require("shiny.stream_tiles")
local view,prepared,calls
local images={["tiles/small.png"]="small",["tiles/tall.png"]="tall"}
local image=sc.image
sc.image=function(resource,x,y,w,h,options)
    calls[#calls+1]={resource=resource,x=x,y=y,w=w,h=h,flip=options.flip_x}
    return image(resource,x,y,w,h,options)
end
return {init=function()
    sc.stream.open("'''+index+'''")
    local metadata=sc.stream.metadata()
    assert(metadata.tilesets[1].tiles[2].image=="tiles/tall.png")
    assert(metadata.tilesets[1].tiles[2].imageheight==16)
    view=Tiles.new(metadata,images)
    metadata.tilesets[1].tiles[1].animation[1].tileid=2
    assert(not pcall(Tiles.new,metadata,images))
    sc.stream.request(-1,0,0); sc.stream.request(0,0,0)
end,update=function()
    if not prepared then
        local left,right=sc.stream.get(-1,0),sc.stream.get(0,0)
        prepared=Tiles.prepare(view,{right,left})
        right.layers["0"][1]=3
        assert(not pcall(Tiles.prepare,view,{right}))
    end
    Tiles.update(view,.1)
end,draw=function()
    if not prepared then return end
    calls={}; Tiles.draw(view,prepared,{x=-8,y=-8,visible={x=-8,y=-8,w=32,h=32}})
    assert(#calls==2 and calls[1].x==-8 and calls[2].x==0 and calls[1].flip)
    assert(calls[2].resource=="tall" and calls[2].y==-8)
    if math.floor(view.time/100)%2==1 then
        assert(calls[1].resource=="tall" and calls[1].w==12 and calls[1].y==-8)
    else assert(calls[1].resource=="small" and calls[1].h==8 and calls[1].y==0) end
end}''')
        self.run_game('collection-tileset')
        Image.new('RGBA',(8,8),(0,0,255,255)).save(folder/'small.png')
        self.assertNotEqual(built.name,assets.build(self.root/'assets.json',self.root/'built').name)

    def test_streamed_terrain_commit(self):
        from PIL import Image
        Image.new('RGBA',(24,8),(255,255,255,255)).save(self.root/'terrain.png')
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.index['tilesets']=[{'firstgid':1,'tilecount':3,'columns':3,'tilewidth':8,'tileheight':8,'image':'terrain.png',
            'tiles':[{'id':0,'properties':[{'name':'collision','value':'solid'}]},
                     {'id':1,'properties':[{'name':'collision','value':'one_way'}]}]}]
        cells=[0]*1024; cells[992]=1; cells[993]=2
        data=json.dumps({'x':0,'y':-1,'layers':{'0':cells},'objects':[]})
        (self.root/'0.json').write_text(data,encoding='utf-8'); self.index['chunks'][0]['bytes']=len(data.encode())
        self.write_index()
        self.scene('''local Tiles=require("shiny.stream_tiles")
local view
return {init=function()
    sc.stream.open("index.json"); view=Tiles.new(sc.stream.metadata())
    sc.stream.request(0,-1,0)
end,update=function()
    if sc.tick()==0 then
        local prepared=Tiles.prepare(view,{sc.stream.get(0,-1)})
        local shapes=Tiles.terrain(view,prepared)
        assert(#shapes==2 and shapes[1].y==-8 and shapes[2].one_way)
        assert(sc.stream.terrain(shapes))
        local hit=sc.physics.ray(4,-32,0,64); assert(hit and hit.id==0 and math.abs(hit.y+8)<.01)
        assert(not pcall(sc.stream.terrain,{{x=0,y=0,w=0,h=1}}))
        assert(sc.physics.ray(4,-32,0,64))
    elseif sc.tick()==1 then
        assert(sc.stream.terrain({}))
        assert(sc.physics.ray(4,-32,0,64)==nil)
        assert(sc.physics.ray(-16,-16,32,0)==nil,"finite room border remained")
    end
end,draw=function() assert(not pcall(sc.stream.terrain,{})) end}''')
        self.run_game('streamed-terrain')

    def test_streamed_polygon_terrain(self):
        from PIL import Image
        spec=importlib.util.spec_from_file_location('assets',Path(__file__).resolve().parents[1]/'tools/assets.py')
        assets=importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
        Image.new('RGBA',(8,8),(255,255,255,255)).save(self.root/'terrain.png')
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        obj={'id':7,'x':0,'y':0,'polygon':[{'x':x,'y':y} for x,y in [(0,0),(8,0),(8,2),(4,2),(4,8),(0,8)]]}
        data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
            'tilesets':[{'firstgid':1,'tilecount':1,'columns':1,'tilewidth':8,'tileheight':8,'image':'terrain.png',
                'tiles':[{'id':0,'objectgroup':{'objects':[obj]}}]}],
            'layers':[{'type':'tilelayer','name':'floor','chunks':[{'x':0,'y':-1,'width':2,'height':1,'data':[1,0x80000001]}]}]}
        (self.root/'map.json').write_text(json.dumps(data),encoding='utf-8')
        (self.root/'assets.json').write_text(json.dumps({'maps':{'world':'map.json'}}),encoding='utf-8')
        built=assets.build(self.root/'assets.json',self.root/'built')
        index=(built/'map-world/index.json').relative_to(self.root).as_posix()
        self.scene('''local Tiles=require("shiny.stream_tiles")
local view
return {init=function()
    sc.stream.open("'''+index+'''"); view=Tiles.new(sc.stream.metadata()); sc.stream.request(0,-1,0)
end,update=function()
    if sc.tick()~=0 then return end
    local prepared=Tiles.prepare(view,{sc.stream.get(0,-1)})
    local shapes=Tiles.terrain(view,prepared)
    assert(#shapes==8 and #shapes[1].vertices==6)
    sc.stream.terrain(shapes)
    local function bottom(x,y)
        local hit=sc.physics.ray(x,4,0,-20)
        assert(hit and hit.id==0 and math.abs(hit.y-y)<.01)
    end
    bottom(2,0); bottom(6,-6); bottom(10,-6); bottom(14,0)
end}''')
        self.run_game('polygon-terrain')
        rotated=assets.tile_collision({'objects':[{'id':1,'x':8,'y':0,'width':8,'height':4,'rotation':90}]})
        self.assertEqual(len(rotated),2)
        xs=[v[i] for v in rotated for i in (0,2,4)]
        ys=[v[i] for v in rotated for i in (1,3,5)]
        self.assertAlmostEqual(min(xs),4); self.assertAlmostEqual(max(xs),8)
        self.assertAlmostEqual(min(ys),0); self.assertAlmostEqual(max(ys),8)
        with self.assertRaisesRegex(ValueError,'self-intersecting'):
            assets.triangulate([{'x':x,'y':y} for x,y in [(0,0),(6,6),(0,8),(8,0)]])
        obj['polygon']=[{'x':0,'y':0}]*3
        (self.root/'map.json').write_text(json.dumps(data),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,r'map.json: tileset .* tile 0: object 7 at .*degenerate'):
            assets.build(self.root/'assets.json',self.root/'built')

    def test_stream_world_object_layer_collision(self):
        spec=importlib.util.spec_from_file_location('assets',Path(__file__).resolve().parents[1]/'tools/assets.py')
        assets=importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        source={'orientation':'orthogonal','infinite':True,'tilewidth':8,'tileheight':8,'layers':[
            {'type':'tilelayer','name':'ground','chunks':[{'x':32,'y':-1,'width':1,'height':1,'data':[0]}]},
            {'type':'objectgroup','name':'walls','objects':[{'id':1,'x':248,'y':-8,'width':24,'height':8,
                'properties':[{'name':'collision','value':'solid'}]}]}]}
        (self.root/'map.json').write_text(json.dumps(source),encoding='utf-8')
        (self.root/'assets.json').write_text(json.dumps({'maps':{'world':'map.json'}}),encoding='utf-8')
        built=assets.build(self.root/'assets.json',self.root/'built')
        index=(built/'map-world/index.json').relative_to(self.root).as_posix()
        self.scene('''local World=require("shiny.stream_world")
local world,stage
return {init=function()
    world=World.new{index="'''+index+'''",name="walls",slot="slot",margin=0,boundary=false,
        prepare=function(object) return {x=object.x,y=object.y,body=false},{} end,
        export=function() return {} end}
    World.request(world,{{x=264,y=-4}},0); stage=0
end,update=function(dt)
    local changed,err,event=World.update(world,dt)
    assert(not err,err)
    if event=="published" and stage==0 then
        assert(changed and world.owners["0:-1"] and World.contains(world,{x=264,y=-4}))
        local hit=sc.physics.ray(260,-16,0,24)
        assert(hit and hit.id==0 and math.abs(hit.y+8)<.01)
        assert(sc.navigation.path(32,31,32,31).status=="unreachable")
        assert(World.patch(world,{{x=33,y=-1,layer=0,gid=0}})==1)
        assert(sc.physics.ray(260,-16,0,24).id==0)
        assert(sc.navigation.path(32,31,32,31).status=="unreachable")
        World.request(world,{{x=600,y=-4}},sc.tick()+1); stage=1
    elseif event=="published" and stage==1 then
        assert(changed and not world.owners["0:-1"])
        assert(sc.physics.ray(260,-16,0,24)==nil)
        sc.debug.watch("object_terrain",true); stage=2
    end
end}''')
        self.assertTrue(self.run_game('stream-object-terrain',save=True,frames=12)['watches']['object_terrain'])

    def test_tile_object_collision_matches_visual_transform(self):
        from PIL import Image
        spec=importlib.util.spec_from_file_location('assets',Path(__file__).resolve().parents[1]/'tools/assets.py')
        assets=importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
        Image.new('RGBA',(20,10),(255,255,255,255)).save(self.root/'atlas.png')
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        (self.root/'project.lua').write_text('return {id="tile-object-terrain",modules={"streaming"},resources={atlas={type="image",path="atlas.png"}}}',encoding='utf-8')
        source={'orientation':'orthogonal','tilewidth':10,'tileheight':10,
            'tilesets':[{'firstgid':1,'tilecount':2,'columns':2,'tilewidth':10,'tileheight':10,
                'image':'atlas.png','objectalignment':'center','tileoffset':{'x':2,'y':-3},'tiles':[
                    {'id':0,'objectgroup':{'objects':[{'id':1,'x':0,'y':0,'width':5,'height':10}]}},
                    {'id':1,'properties':[{'name':'collision','value':'one_way'}]}]}],
            'layers':[{'type':'objectgroup','name':'obstacles','objects':[
                {'id':1,'gid':1,'x':32,'y':32,'width':20,'height':20},
                {'id':2,'gid':0x80000001,'x':80,'y':32,'width':20,'height':20},
                {'id':3,'gid':1,'x':140,'y':64,'width':20,'height':20,'rotation':90},
                {'id':4,'gid':2,'x':180,'y':32,'width':20,'height':20}]}]}
        (self.root/'map.json').write_text(json.dumps(source),encoding='utf-8')
        (self.root/'assets.json').write_text(json.dumps({'maps':{'world':'map.json'}}),encoding='utf-8')
        built=assets.build(self.root/'assets.json',self.root/'built')
        index=(built/'map-world/index.json').relative_to(self.root).as_posix()
        self.scene('''local Tiles=require("shiny.stream_tiles")
local view,prepared,calls=nil,nil,{}
local image=sc.image
sc.image=function(name,x,y,w,h,options)
    calls[#calls+1]={x=x,y=y}
    return image(name,x,y,w,h,options)
end
return {init=function()
    sc.stream.open("'''+index+'''"); view=Tiles.new(sc.stream.metadata(),{["atlas.png"]="atlas"})
    sc.stream.request(0,0,0)
end,update=function()
    if sc.tick()~=0 then return end
    local chunk=sc.stream.get(0,0)
    prepared=Tiles.prepare(view,{chunk})
    local shapes=Tiles.terrain(view,prepared,{chunk})
    assert(#shapes==7 and shapes[7].one_way)
    assert(math.abs(shapes[1].x-24)<.01 and math.abs(shapes[1].y-19)<.01)
    assert(math.abs(shapes[3].x-82)<.01 and math.abs(shapes[5].y-51)<.01)
    sc.stream.terrain(shapes)
    local function hit(x,y,expected)
        local result=sc.physics.ray(x,y,0,40)
        assert((result~=nil)==expected)
    end
    hit(27,10,true); hit(37,10,false)
    hit(85,10,true); hit(75,10,false)
    hit(140,45,true); hit(180,10,true)
    sc.debug.watch("tile_object_collision",true)
end,draw=function()
    if not prepared then return end
    calls={}; Tiles.draw(view,prepared,{x=0,y=0,visible={x=0,y=0,w=220,h=100}})
    assert(#calls==4 and math.abs(calls[1].x-24)<.01 and math.abs(calls[1].y-19)<.01)
end}''')
        self.assertTrue(self.run_game('tile-object-terrain',frames=3)['watches']['tile_object_collision'])

    def test_streamed_navigation_region(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.scene('''local Tiles=require("shiny.stream_tiles")
local nav=sc.navigation
return {init=function()
    sc.stream.open("index.json")
    local view=Tiles.new(sc.stream.metadata())
    local grid=Tiles.navigation(view,{{x=-1,y=-1},{x=1,y=-1}})
    assert(grid.x==-256 and grid.y==-256 and #grid.rows==32 and #grid.rows[1]==96)
    assert(grid.rows[1]:sub(33,64)==string.rep("#",32))
    nav.region(grid.x,grid.y,grid.rows,grid.cell_size)
    assert(nav.path(0,0,64,0).status=="unreachable")
    nav.region(-16,-16,{"....","....","...."},8)
    local field,status=nav.flow(3,0)
    assert(status=="ok")
    local dx,dy=nav.direction(field,-12,-12); assert(dx>0 and math.abs(dy)<.001)
    assert(not pcall(nav.region,0,0,{"..","."},8))
    assert(select(3,nav.direction(field,-12,-12))=="ok")
    sc.stream.terrain({{x=-8,y=-16,w=8,h=8}})
    assert(select(3,nav.direction(field,-12,-12))=="stale")
    local result=nav.path(0,0,3,0); assert(result.status=="ok" and #result.points==6)
    local state=nav.refresh(field,1); assert(state=="budget_exhausted")
    while state=="budget_exhausted" do state=nav.refresh(field,1) end
    dx,dy=nav.direction(field,-12,-12); assert(dy>0 and math.abs(dx)<.001)
    assert(not pcall(sc.stream.terrain,{{x=0,y=0,w=0,h=8}}))
    assert(select(3,nav.direction(field,-12,-12))=="ok")
    sc.stream.terrain({})
    assert(select(3,nav.direction(field,-12,-12))=="stale")
    nav.refresh(field,32)
    dx,dy=nav.direction(field,-12,-12); assert(dx>0 and math.abs(dy)<.001)
    nav.region(); assert(not pcall(nav.direction,field,0,0))
end,draw=function() assert(not pcall(nav.region,0,0,{"."},8)) end}''')
        self.run_game('streamed-navigation')

    def test_terrain_navigation_transaction(self):
        self.scene('''local nav=sc.navigation
return {init=function()
    sc.stream.open("index.json")
    sc.stream.terrain({{x=8,y=0,w=8,h=8}})
    assert(#nav.path(0,0,2,0).points==5,"default grid retained old passability")
    nav.region(-16,-16,{"....","...."},8)
    local field=nav.flow(3,0)
    local function retained()
        assert(sc.physics.ray(12,-8,0,24))
        assert(select(3,nav.direction(field,-12,-12))=="ok")
    end
    assert(not pcall(sc.stream.terrain,{}, {x=0,y=0,rows={"..","."}})); retained()
    assert(not pcall(sc.stream.terrain,{{x=0,y=0,w=0,h=1}}, {x=0,y=0,rows={".."}})); retained()
    assert(not pcall(sc.stream.terrain,{}, {x=0,y=0,rows={".."},typo=1})); retained()
    sc.stream.terrain({{x=-8,y=-16,w=8,h=8}}, {x=-16,y=-16,rows={"....","...."}})
    assert(not pcall(nav.direction,field,-12,-12))
    assert(sc.physics.ray(12,-8,0,24)==nil)
    assert(#nav.path(0,0,3,0).points==6)
    field=nav.flow(3,0)
    local dx,dy=nav.direction(field,-12,-12); assert(dy>0 and math.abs(dx)<.001)
    sc.stream.terrain({},nil)
    assert(select(3,nav.direction(field,-12,-12))=="stale")
    assert(#nav.path(0,0,3,0).points==4)
end}''')
        self.run_game('terrain-navigation-transaction')

    def test_terrain_entering_entities(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.scene('''local Regions=require("shiny.stream_regions")
local region=Regions.new{tilewidth=8,tileheight=8,margin=0,boundary=true}
return {init=function()
    sc.stream.open("index.json")
    sc.stream.terrain({{x=8,y=0,w=8,h=8}})
    local function retained()
        assert(sc.physics.ray(12,-8,0,24))
        assert(#sc.find_all("candidate")==0)
    end
    assert(not pcall(sc.stream.terrain,{},nil,{{w=0,tag="candidate"}})); retained()
    local too_many={}
    for i=1,4097 do too_many[i]={tag="candidate"} end
    assert(not pcall(sc.stream.terrain,{},nil,too_many)); retained()
    assert(not pcall(sc.stream.terrain,{{w=0,h=8}},nil,{{tag="candidate"}})); retained()
    assert(not pcall(sc.stream.terrain,{},nil,{{x=-8,body=false},{x=8,dynamic=true}},{0,1})); retained()
    assert(not pcall(sc.stream.terrain,{},nil,{{x=-8,body=false},{x=8,body=false}},{0,3})); retained()
    local ok,ids=sc.stream.terrain({},nil,{{x=-8,y=-8,tag="candidate"},{x=8,y=-8,tag="candidate"}})
    assert(ok and #ids==2 and ids[1]~=ids[2] and #sc.find_all("candidate")==2)
    assert(sc.physics.ray(12,-8,0,24)==nil)
    for _,id in ipairs(ids) do sc.destroy(id) end
    ok,ids=sc.stream.terrain({},nil,{{x=-8,y=-8,body=false},{x=8,body=false}},{0,1})
    assert(ok and sc.presentation.attachment(ids[2]).parent==ids[1] and sc.get(ids[2]).x==0)
    for _,id in ipairs(ids) do sc.destroy(id) end
    Regions.request(region,{{x=-8,y=-8}},0)
end,update=function()
    if sc.tick()==0 then
        assert(Regions.ready(region))
        assert(not pcall(Regions.commit,region,{{w=0,h=8}}))
        assert(region.pending and #region.walls==0 and not Regions.contains(region,{x=-8,y=-8}))
        Regions.commit(region,{{x=-16,y=-8,w=16,h=8}},{x=-32,y=-32,rows={"....","....","....","...."}})
        assert(#region.walls==4 and Regions.contains(region,{x=-8,y=-8}))
        assert(sc.physics.ray(-8,-16,0,24))
        assert(sc.physics.ray(-128,-128,-256,0),"loading boundary absent")
    end
end}''')
        self.run_game('terrain-entering')

    def test_streamed_object_publication(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.scene('''local Regions=require("shiny.stream_regions")
local Objects=require("shiny.stream_objects")
local region=Regions.new{tilewidth=8,tileheight=8,margin=0,boundary=true}
local chunk={objects={{persistent_id="actors:1",x=-16},{persistent_id="actors:2",x=-8},{persistent_id="actors:3",x=-4}}}
local saved={format=2,objects={["actors:2"]={data={opened=true}},["actors:3"]={deleted=true}},imports={}}
local function prepare(object,state) return {x=object.x,y=-32,tag="actor"},state or {} end
return {init=function()
    sc.stream.open("index.json")
    sc.stream.terrain({{x=8,y=0,w=8,h=8}})
    Regions.request(region,{{x=-8,y=-8}},0)
end,update=function()
    if sc.tick()~=0 then return end
    assert(Regions.ready(region))
    assert(not pcall(Objects.load,chunk,saved,prepare,{region=region,terrain={{w=0,h=8}}}))
    assert(#sc.find_all("actor")==0 and #region.walls==0 and region.pending)
    assert(sc.identity.resolve("actors:1").status=="unloaded")
    assert(sc.physics.ray(12,-8,0,24))
    local owner=Objects.load(chunk,saved,prepare,{region=region,terrain={{x=-16,y=-8,w=16,h=8}},
        navigation={x=-32,y=-32,rows={"....","....","....","...."}}})
    assert(#owner.entries==3 and #region.walls==4 and region.pending==nil)
    assert(owner.entries[2].data.opened and owner.entries[3].id==nil)
    assert(sc.identity.resolve("actors:3").status=="deleted")
    assert(sc.get(owner.entries[1].id).x==-16 and sc.get(owner.entries[2].id).x==-8)
    assert(sc.physics.ray(12,-8,0,24)==nil and sc.physics.ray(-8,-16,0,24))
    assert(Objects.unload(owner,"slot","forest",function(entity,data) return {x=entity.x,opened=data.opened} end))
    local restored=Objects.load(chunk,sc.save.read_chunk("slot","forest"),prepare,{terrain={}})
    assert(restored.entries[2].data.opened and restored.entries[3].id==nil)
end}''')
        self.run_game('object-publication',save=True)

    def test_streamed_object_transition(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        # A directory at the slot file injects an actual disk write failure.
        (self.root/'saves/stream-test/blocked.json').mkdir(parents=True)
        self.scene('''local Objects=require("shiny.stream_objects")
local old_chunk={objects={{persistent_id="old:1",x=-16}}}
local next_chunk={objects={{persistent_id="next:1",x=16}}}
local function prepare(object,saved) return {x=saved and saved.x or object.x,y=-16},{} end
local function export(entity) return {x=entity.x} end
return {init=function()
    sc.stream.open("index.json")
end,update=function()
    if sc.tick()~=0 then return end
    local old=Objects.load(old_chunk,nil,prepare,{terrain={{x=-16,y=0,w=8,h=8}}})
    local id=old.entries[1].id; sc.set(id,{x=-24})
    local leaving={{owner=old,key="old"}}
    local entering={{chunk=next_chunk}}
    local publication={terrain={{x=16,y=0,w=8,h=8}}}
    local function retained()
        assert(not old.unloaded and sc.get(id).x==-24)
        assert(sc.identity.resolve("next:1").status~="active")
        assert(sc.physics.ray(-12,-8,0,24))
    end
    local result,err=Objects.transition(leaving,entering,"blocked",prepare,export,publication)
    assert(result==nil and err); retained()
    assert(not pcall(Objects.transition,leaving,entering,"slot",prepare,export,{terrain={{w=0,h=8}}}))
    retained()
    assert(sc.save.read_chunk("slot","old").objects["old:1"].data.x==-24)
    local owners=assert(Objects.transition(leaving,entering,"slot",prepare,export,publication))
    assert(old.unloaded and not pcall(sc.get,id) and sc.identity.resolve("old:1").status=="unloaded")
    assert(sc.get(owners[1].entries[1].id).x==16)
    assert(sc.physics.ray(-12,-8,0,24)==nil and sc.physics.ray(20,-8,0,24))
    local restored=assert(Objects.transition({{owner=owners[1],key="next"}},
        {{chunk=old_chunk,saved=sc.save.read_chunk("slot","old")}},"slot",prepare,export,{terrain={}}))
    assert(sc.get(restored[1].entries[1].id).x==-24)
    assert(owners[1].unloaded and sc.identity.resolve("next:1").status=="unloaded")
end}''')
        self.run_game('object-transition',save=True)

    def test_stream_world_coordinator(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.index['tilesets']=[{'firstgid':1,'tilecount':3,'columns':3,'tilewidth':8,'tileheight':8,'image':'terrain.png'}]
        for x in range(2):
            cells=[0]*1024; cells[0]=x+1
            data=json.dumps({'x':x,'y':-1,'layers':{'0':cells},'objects':[
                {'persistent_id':f'actor:{x}','layer':1,'x':x*256+8,'y':-8}]})
            (self.root/f'{x}.json').write_text(data,encoding='utf-8')
            self.index['chunks'][x]['bytes']=len(data.encode())
        self.write_index()
        (self.root/'saves/stream-test').mkdir(parents=True)
        (self.root/'saves/stream-test/corrupt.json').write_text('{broken',encoding='utf-8')
        self.scene('''local World=require("shiny.stream_world")
local world,old,old_child
return {init=function()
    local missing,err=sc.save.read_chunk("slot","forest:0:-1"); assert(missing==nil and err==nil)
    local bad,why=sc.save.read_chunk("corrupt","forest:0:-1"); assert(bad==nil and type(why)=="string")
    world=World.new{index="index.json",name="forest",slot="slot",margin=0,
        prepare=function(object,saved) return {entity={x=saved and saved.x or object.x,y=object.y,body=false},
            children={marker={x=5,w=2,h=2,body=false}}},{} end,
        export=function(entity) return {x=entity.x} end}
    World.request(world,{{x=8,y=-8}},0)
end,update=function(dt)
    local changed=World.update(world,dt)
    local tick=sc.tick()
    if tick==1 then
        assert(changed and World.contains(world,{x=8,y=-8}) and #world.region.walls==4)
        local path=World.path(world,8,-8,16,-8)
        assert(path.status=="ok" and path.points[1].x==12 and path.points[#path.points].x==20
            and path.points[1].y==-4)
        assert(World.path(world,8,-8,264,-8).status=="unloaded")
        assert(not pcall(World.path,world,8,-8,264,-8,0))
        assert(not pcall(World.flow,world,264,-8,0))
        local field,status=World.flow(world,16,-8,1024,1)
        assert(field and status=="ok")
        local dx,dy=sc.navigation.direction(field,12,-4)
        assert(dx>0 and dy==0)
        local entry=world.owners["0:-1"].entries[1]
        old=entry.id; old_child=entry.children.marker.id; sc.set(old,{x=72})
        assert(sc.get(old_child).x==77)
        assert(world.prepared[1][1].id==0)
        World.request(world,{{x=264,y=-8}},sc.tick()+1)
    elseif tick==2 then
        assert(not changed and World.status(world).status=='pending' and sc.app.paused())
        assert(sc.get(old).x==72 and World.contains(world,{x=8,y=-8}))
        assert(World.path(world,8,-8,16,-8).status=="ok")
        assert(World.path(world,264,-8,272,-8).status=="unloaded")
        assert(not pcall(World.patch,world,{}))
    elseif tick==4 then
        assert(changed and not pcall(sc.get,old) and not pcall(sc.get,old_child) and world.owners["0:-1"]==nil)
        assert(sc.save.read_chunk("slot","forest:0:-1").objects["actor:0"].data.x==72)
        assert(world.prepared[1][1].id==1 and World.contains(world,{x=264,y=-8}))
        assert(World.path(world,8,-8,264,-8).status=="unloaded")
        assert(World.path(world,264,-8,272,-8).status=="ok")
        World.request(world,{{x=8,y=-8}},sc.tick()+1)
    elseif tick==3 or tick==5 or tick==6 or tick==8 then assert(not changed and World.status(world))
    elseif tick==7 then
        local entry=world.owners["0:-1"].entries[1]
        assert(changed and sc.get(entry.id).x==72 and sc.get(entry.children.marker.id).x==77)
        assert(world.prepared[1][1].id==0)
        World.request(world,{},sc.tick()+1)
    elseif tick==9 then
        assert(changed and next(world.owners)==nil and #world.prepared[1]==0)
        assert(#world.region.walls==0 and not World.contains(world,{x=8,y=-8}))
        assert(World.path(world,8,-8,16,-8).status=="unloaded")
        assert(select(2,World.flow(world,16,-8))=="unloaded")
        assert(sc.identity.resolve("actor:0").status=="unloaded")
    else assert(changed==false) end
end}''')
        self.run_game('world-coordinator',save=True,frames=10)

    def test_stream_world_object_transfer_roundtrip(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.index['tilesets']=[{'firstgid':1,'tilecount':3,'columns':3,'tilewidth':8,'tileheight':8,'image':'terrain.png'}]
        chunk=self.root/'0.json'
        data=json.loads(chunk.read_text(encoding='utf-8'))
        data['objects']=[{'persistent_id':'traveller:1','layer':1,'x':8,'y':-8}]
        encoded=json.dumps(data,separators=(',',':'))
        chunk.write_text(encoded,encoding='utf-8')
        self.index['chunks'][0]['bytes']=len(encoded.encode())
        self.write_index()
        common='''local World=require("shiny.stream_world")
local world,root,reference,phase=nil,nil,nil,0
local function make()
    return World.new{index="index.json",name="forest",slot="slot",margin=0,
        prepare=function(object,saved) return {x=saved and saved.x or object.x,y=-8,body=false},{} end,
        export=function(entity) return {x=entity.x} end}
end
'''
        self.scene(common+'''return {init=function()
    world=make(); World.request(world,{{x=8,y=-8},{x=264,y=-8}},0)
end,update=function(dt)
    local _,err,event=World.update(world,dt); assert(not err,err)
    if phase==0 and event=="published" then
        root=world.owners["0:-1"].entries[1].id
        reference=require("shiny.stream_objects").reference(world.owners["0:-1"].entries[1])
        assert(World.transfer(world,"traveller:1")==false)
        sc.set(root,{x=264})
        local plan,why=World.request(world,{{x=264,y=-8}},sc.tick()+1)
        assert(plan==nil and why:find("traveller:1") and world.region.pending==nil)
        assert(sc.get(root).x==264 and world.owners["0:-1"].entries[1].id==root)
        assert(World.transfer(world,"traveller:1")); phase=1
    elseif phase==1 and event=="transferred" then
        assert(sc.identity.resolve("traveller:1").id==root)
        assert(require("shiny.stream_objects").resolve(reference).id==root)
        assert(world.owners["0:-1"].entries[1].moved)
        assert(world.owners["1:-1"].entries[1].id==root)
        World.request(world,{{x=264,y=-8}},sc.tick()+1); phase=2
    elseif phase==2 and event=="published" then
        assert(world.owners["0:-1"]==nil and sc.get(root).x==264)
        local saved=assert(sc.save.read_chunk("slot","forest:0:-1"))
        assert(saved.objects["traveller:1"].moved)
        sc.set(root,{x=520})
        local request,why=World.transfer(world,"traveller:1")
        assert(request==nil and why:find("not loaded") and sc.get(root).x==520)
        sc.set(root,{x=264})
        World.request(world,{{x=8,y=-8},{x=264,y=-8}},sc.tick()+1); phase=3
    elseif phase==3 and event=="published" then
        assert(world.owners["0:-1"].entries[1].moved and sc.identity.resolve("traveller:1").id==root)
        assert(require("shiny.stream_objects").resolve(reference).id==root)
        sc.set(root,{x=8}); assert(World.transfer(world,"traveller:1")); phase=4
    elseif phase==4 and event=="transferred" then
        assert(world.owners["0:-1"].entries[1].id==root)
        assert(#world.owners["1:-1"].entries==0 and sc.identity.resolve("traveller:1").id==root)
        World.request(world,{{x=8,y=-8}},sc.tick()+1); phase=5
    elseif phase==5 and event=="published" then
        assert(world.owners["1:-1"]==nil and sc.get(root).x==8)
        phase=6
    end
    sc.debug.watch("transfer_phase",phase)
end}''')
        result=self.run_game('transfer-out-back',save=True,frames=18)
        self.assertEqual(result['watches']['transfer_phase'],6)
        self.scene(common+'''return {init=function()
    world=make(); World.request(world,{{x=8,y=-8},{x=264,y=-8}},0)
end,update=function(dt)
    local _,err,event=World.update(world,dt); assert(not err,err)
    if event=="published" then
        assert(world.owners["0:-1"].entries[1].id)
        assert(sc.get(world.owners["0:-1"].entries[1].id).x==8)
        assert(#world.owners["1:-1"].entries==0)
        sc.debug.watch("restored_transfer",true)
    end
end}''')
        restored=self.run_game('transfer-fresh-process',save=True,frames=6)
        self.assertTrue(restored['watches']['restored_transfer'])

    def test_stream_world_batch_transfer(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        self.index['tilesets']=[{'firstgid':1,'tilecount':3,'columns':3,'tilewidth':8,'tileheight':8,'image':'terrain.png'}]
        chunk=self.root/'0.json'
        data=json.loads(chunk.read_text(encoding='utf-8'))
        data['objects']=[{'persistent_id':f'group:{i}','layer':1,'x':i*8,'y':-8} for i in (1,2)]
        encoded=json.dumps(data,separators=(',',':'))
        chunk.write_text(encoded,encoding='utf-8')
        self.index['chunks'][0]['bytes']=len(encoded.encode())
        self.write_index()
        common='''local World=require("shiny.stream_world")
local Objects=require("shiny.stream_objects")
local world,phase=nil,0
local function make()
    return World.new{index="index.json",name="forest",slot="slot",margin=0,
        prepare=function(object,saved)
            return {entity={x=saved and saved.x or object.x,y=-8,body=false},
                children={marker={x=2,y=0,body=false}}},{}
        end,export=function(entity) return {x=entity.x} end}
end
'''
        self.scene(common+'''local ids,refs={},{}
return {init=function()
    world=make(); World.request(world,{{x=8,y=-8},{x=264,y=-8}},0)
end,update=function(dt)
    local _,err,event=World.update(world,dt); assert(not err,err)
    if phase==0 and event=="published" then
        local entries=world.owners["0:-1"].entries
        for i,entry in ipairs(entries) do
            ids[i]={entry.id,entry.children.marker.id}
            refs[i]=Objects.reference(entry,{"marker"})
            sc.set(entry.id,{x=256+i*8})
        end
        local request,why=World.transfer_many(world,{"group:1","missing"})
        assert(request==nil and why:find("missing") and not World.status(world))
        assert(not pcall(World.transfer_many,world,{"group:1","group:1"}))
        assert(World.transfer_many(world,{"group:1","group:2"}))
        assert(sc.app.paused() and entries[1].id==ids[1][1] and entries[2].id==ids[2][1])
        phase=1
    elseif phase==1 and event=="transferred" then
        local source=world.owners["0:-1"].entries
        local target=world.owners["1:-1"].entries
        assert(source[1].moved and source[2].moved and #target==2)
        for i,entry in ipairs(target) do
            assert(entry.id==ids[i][1] and entry.children.marker.id==ids[i][2])
            assert(Objects.resolve(refs[i]).id==ids[i][2])
        end
        assert(World.transfer_many(world,{})==false)
        World.request(world,{{x=264,y=-8}},sc.tick()+1); phase=2
    elseif phase==2 and event=="published" then
        assert(world.owners["0:-1"]==nil and #world.owners["1:-1"].entries==2)
        local saved=assert(sc.save.read_chunk("slot","forest:0:-1"))
        assert(saved.objects["group:1"].moved and saved.objects["group:2"].moved)
        for i,entry in ipairs(world.owners["1:-1"].entries) do
            assert(entry.id==ids[i][1] and sc.get(entry.id).x==256+i*8)
        end
        phase=3
    end
    sc.debug.watch("batch_transfer_phase",phase)
end}''')
        result=self.run_game('batch-transfer',save=True,frames=12)
        self.assertEqual(result['watches']['batch_transfer_phase'],3)
        self.scene(common+'''return {init=function()
    world=make(); World.request(world,{{x=264,y=-8}},0)
end,update=function(dt)
    local _,err,event=World.update(world,dt); assert(not err,err)
    if event=="published" then
        local target=world.owners["1:-1"].entries
        assert(world.owners["0:-1"]==nil and #target==2)
        for i,entry in ipairs(target) do
            assert(entry.imported and sc.get(entry.id).x==256+i*8)
            assert(sc.get(entry.children.marker.id).x==258+i*8)
        end
        sc.debug.watch("batch_restored",true)
    end
end}''')
        restored=self.run_game('batch-transfer-fresh-process',save=True,frames=6)
        self.assertTrue(restored['watches']['batch_restored'])

    def test_stream_world_map_edits(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        (self.root/'route_graph.lua').write_text('''return {format=1,cell_size=8,chunk_width=256,chunk_height=256,
cells_x=32,cells_y=32,radius=0,bounds={0,-1,2,-1},node_count=3,
chunks={['0:-1']={full=true,component=1},['1:-1']={full=true,component=2},
['2:-1']={full=true,component=3}},edges={
{a=1,b=2,ax=31,ay=-1,bx=32,by=-1},
{a=2,b=3,ax=63,ay=-1,bx=64,by=-1}}}''',encoding='utf-8')
        self.index['tilesets']=[{'firstgid':1,'tilecount':1,'columns':1,'tilewidth':8,'tileheight':8,'image':'terrain.png',
            'tiles':[{'id':0,'properties':[{'name':'collision','value':'solid'}]}]}]
        for x in range(3):
            data=json.dumps({'x':x,'y':-1,'layers':{},'objects':[]})
            (self.root/f'{x}.json').write_text(data,encoding='utf-8'); self.index['chunks'][x]['bytes']=len(data.encode())
        self.write_index()
        self.scene('''local World=require("shiny.stream_world")
local Route=require("shiny.stream_route")
local world,route,unrefreshed,late
return {init=function()
    route=Route.new(require("route_graph"))
    unrefreshed=Route.new(require("route_graph"))
    world=World.new{index="index.json",name="forest",slot="slot",margin=0,
        route_index=true,
        prepare=function() error("no authored objects expected") end,export=function() return {} end}
    World.request(world,{{x=8,y=-8}},0)
end,update=function(dt)
    local changed=World.update(world,dt)
    local tick=sc.tick()
    if tick==1 then
        assert(changed)
        assert(World.refresh_route(world,route)==0 and route.revision==0)
        assert(World.refresh_route(world,unrefreshed)==0)
        assert(World.patch(world,{{x=0.0,y=-1.0,layer=0.0,gid=1}})==1)
        assert(World.refresh_route(world,route)==1 and route.revision==2)
        assert(Route.route(route,4,-4,12,-4).status=="unreachable")
        assert(world.chunks["0:-1"].layers["0"][993]==1 and #world.prepared[1]==1)
        local hit=sc.physics.ray(4,-16,0,24); assert(hit and hit.id==0 and math.abs(hit.y+8)<.01)
        assert(sc.navigation.path(0,31,1,31).status=="unreachable")
        assert(not pcall(World.patch,world,{{x=0,y=-1,layer=0,gid=0},{x=1,y=-1,layer=0,gid=99}}))
        assert(world.chunks["0:-1"].layers["0"][993]==1 and sc.physics.ray(4,-16,0,24).id==0)
        assert(not pcall(World.patch,world,{{x=256,y=-1,layer=0,gid=1}}))
        World.request(world,{{x=264,y=-8}},sc.tick()+1)
    elseif tick==2 or tick==5 or tick==8 or tick==11 then
        assert(not changed and World.status(world))
    elseif tick==4 then
        assert(changed and world.edits["0:-1"]==nil)
        assert(World.refresh_route(world,unrefreshed)==0)
        assert(Route.route(unrefreshed,264,-4,4,-4).status=="unverified")
        local saved=sc.save.read_chunk("slot","forest:0:-1")
        assert(saved.extra.format==1 and saved.extra.tiles["0"]["993"]==1)
        World.request(world,{{x=8,y=-8}},sc.tick()+1)
    elseif tick==7 then
        assert(changed and world.chunks["0:-1"].layers["0"][993]==1)
        assert(World.refresh_route(world,route)==0 and route.revision==2)
        local restored=Route.new(require("route_graph"))
        assert(World.refresh_route(world,restored)==1)
        assert(Route.route(restored,4,-4,12,-4).status=="unreachable")
        late=restored
        assert(sc.physics.ray(4,-16,0,24).id==0 and sc.navigation.path(0,31,1,31).status=="unreachable")
        World.patch(world,{{x=0,y=-1,layer=0,gid=0}})
        assert(World.refresh_route(world,route)==1 and route.revision==4)
        assert(Route.route(route,4,-4,12,-4).status=="ok")
        assert(sc.physics.ray(4,-16,0,24).id~=0 and sc.navigation.path(0,31,1,31).status=="ok")
        World.request(world,{{x=264,y=-8}},sc.tick()+1)
    elseif tick==10 then
        assert(World.refresh_route(world,late)==0)
        assert(Route.route(late,264,-4,4,-4).status=="unverified")
        World.request(world,{{x=8,y=-8}},sc.tick()+1)
    elseif tick==13 then
        assert(changed and world.chunks["0:-1"].layers["0"][993]==0 and #world.prepared[1]==0)
    end
end}''')
        self.run_game('world-map-edits',save=True,frames=14)
        self.scene('''local World=require("shiny.stream_world")
local Route=require("shiny.stream_route")
local world,route,phase
return {init=function()
    phase=0; route=Route.new(require("route_graph"))
    world=World.new{index="index.json",name="forest",slot="slot",margin=0,route_index=true,
        prepare=function() error("no authored objects expected") end,export=function() return {} end}
    World.request(world,{{x=520,y=-8}},0)
end,update=function(dt)
    local changed,err,event=World.update(world,dt)
    assert(not err,err)
    if event=="published" and phase==0 then
        assert(World.refresh_route(world,route)==0)
        local pending=Route.route(route,520,-4,4,-4)
        assert(pending.status=="unverified" and pending.pending.x==0 and pending.pending.y==-1)
        World.request(world,{{x=8,y=-8}},sc.tick()+1); phase=1
    elseif event=="published" and phase==1 then
        assert(World.refresh_route(world,route)==0)
        assert(Route.route(route,520,-4,4,-4).status=="ok")
        sc.debug.watch("remote_route_restored",true); sc.app.quit()
    end
end}''')
        self.assertTrue(self.run_game('world-route-index-restore',save=True,frames=8)['watches']['remote_route_restored'])
        self.scene('''local World=require("shiny.stream_world")
local world
return {init=function()
    world=World.new{index="index.json",name="forest",slot="legacy",margin=0,route_index=true,
        prepare=function() error("no authored objects expected") end,export=function() return {} end}
    World.request(world,{{x=8,y=-8}},0)
end,update=function(dt)
    if sc.tick()==0 then
        local ok,err=sc.save.write_chunks("legacy",{["forest:0:-1"]={format=2,objects={},imports={}}})
        assert(ok,err)
    end
    local _,err=World.update(world,dt)
    assert(not err,err)
end}''')
        self.assertIn('saved streamed world lacks route index',
                      self.run_game('world-route-index-legacy',save=True,frames=6,ok=False))

    def test_world_async_cancel_and_publication_failure(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        (self.root/'project.lua').write_text('return {id="stream-test",modules={"streaming"},limits={entities=12}}',encoding='utf-8')
        for x in range(2):
            data=json.dumps({'x':x,'y':-1,'layers':{},'objects':[{'persistent_id':f'actor:{x}','layer':1,'x':x*256+8,'y':-8}]})
            (self.root/f'{x}.json').write_text(data,encoding='utf-8');self.index['chunks'][x]['bytes']=len(data.encode())
        self.write_index()
        self.scene('''local World=require('shiny.stream_world')
local world,old,blockers={},{},{}
return {gravity=0,init=function()
    world=World.new{index='index.json',name='forest',slot='slot',margin=0,
        prepare=function(o,s) return {x=s and s.x or o.x,y=o.y,body=false},{} end,
        export=function(e) return {x=e.x} end}
    World.request(world,{{x=8,y=-8}},0)
end,update=function(dt)
    local changed,err,event=World.update(world,dt)
    local tick=sc.tick()
    if tick==1 then
        old=world.owners['0:-1'].entries[1].id;sc.set(old,{x=72})
        for i=1,3 do blockers[i]=sc.spawn{body=false} end
        World.request(world,{{x=264,y=-8}},sc.tick()+1)
    elseif tick==3 then
        assert(World.status(world).status=='pending');World.cancel(world)
    elseif tick==4 then
        assert(event=='cancelled' and not changed and not World.status(world) and not sc.app.paused())
        assert(sc.get(old).x==72 and World.contains(world,{x=8,y=-8}) and not World.contains(world,{x=264,y=-8}))
        assert(sc.save.read_chunk('slot','forest:0:-1').objects['actor:0'].data.x==72)
        World.request(world,{{x=264,y=-8}},sc.tick()+1)
    elseif tick==7 then
        assert(changed==nil and err and World.status(world).status=='failed' and sc.app.paused())
        assert(sc.get(old).x==72 and #world.region.walls==4)
        sc.destroy(blockers[1]);World.retry(world)
    elseif tick==8 then
        assert(changed and not pcall(sc.get,old) and not sc.app.paused())
        assert(World.save(world))
    elseif tick==9 then
        assert(not changed and event=='saved' and world.owners['1:-1'].unloaded==false)
        assert(sc.save.read_chunk('slot','forest:1:-1').objects['actor:1'].data.x==264)
        sc.app.pause(true);assert(World.save(world))
    elseif tick==10 then assert(event=='saved' and sc.app.paused()) end
end,ui_update=function() World.ui_update(world) end}''')
        self.run_game('world-cancel',save=True,frames=11)

    def test_world_read_preparation_and_cancel(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        for x in range(2):
            data=json.dumps({'x':x,'y':-1,'layers':{},'objects':[{'persistent_id':f'actor:{x}','layer':1,'x':x*256+8,'y':-8}]})
            (self.root/f'{x}.json').write_text(data,encoding='utf-8');self.index['chunks'][x]['bytes']=len(data.encode())
        self.write_index()
        self.scene('''local World=require('shiny.stream_world')
local world,old,allow
-- The coordinator must not fall back to synchronous chunk reads.
sc.save.read_chunk=function() error('synchronous read in world restoration') end
return {gravity=0,init=function()
    world=World.new{index='index.json',name='forest',slot='slot',margin=0,
        prepare=function(o,s)
            if o.persistent_id=='actor:1' and not allow then error('injected preparation failure') end
            return {x=s and s.x or o.x,y=o.y,body=false},{}
        end,export=function(e) return {x=e.x} end}
    World.request(world,{{x=8,y=-8}},0)
end,update=function(dt)
    local tick=sc.tick()
    if tick==0 then assert(sc.save.write_chunks('slot',{['forest:1:-1']={format=2,objects={['actor:1']={data={x=300}}},imports={}}})) end
    local changed,err,event=World.update(world,dt)
    if tick==0 then
        assert(not changed and World.status(world).phase=='read' and sc.app.paused())
        assert(next(world.chunks)==nil and #world.region.walls==0)
    elseif tick==1 then
        assert(changed and event=='published');old=world.owners['0:-1'].entries[1].id;sc.set(old,{x=72})
        World.request(world,{{x=264,y=-8}},2)
    elseif tick==2 then
        assert(World.status(world).phase=='read');World.cancel(world)
    elseif tick==3 then
        assert(event=='cancelled' and not changed and not sc.app.paused())
        assert(sc.get(old).x==72 and World.contains(world,{x=8,y=-8}) and #world.region.walls==4)
        World.request(world,{{x=264,y=-8}},4)
    elseif tick==5 then
        assert(changed==nil and err:find('injected preparation failure'))
        assert(World.status(world).phase=='prepare' and sc.get(old).x==72 and sc.app.paused())
        assert(not pcall(World.request,world,{},6) and not pcall(World.patch,world,{}))
        allow=true;World.retry(world)
    elseif tick==6 then assert(not changed and World.status(world).phase=='write')
    elseif tick==7 then
        assert(changed and not World.status(world) and not sc.app.paused() and not pcall(sc.get,old))
        assert(sc.get(world.owners['1:-1'].entries[1].id).x==300)
        sc.debug.watch('restored',true)
    end
end,ui_update=function() World.ui_update(world) end}''')
        result=self.run_game('world-read-preparation',save=True)
        self.assertTrue(result['watches']['restored'])

    def test_world_failed_read_recovery(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        data=json.dumps({'x':0,'y':-1,'layers':{},'objects':[]})
        (self.root/'0.json').write_text(data,encoding='utf-8');self.index['chunks'][0]['bytes']=len(data.encode())
        self.write_index()
        for cancel in (False,True):
            path=self.root/'saves/stream-test/slot.json';path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('{broken',encoding='utf-8')
            self.scene('''local World=require('shiny.stream_world')
local world,old
return {gravity=0,init=function()
    old=sc.spawn{persistent_id='sentinel',x=72,y=-8,body=false}
    sc.app.pause(true)
    world=World.new{index='index.json',name='forest',slot='slot',margin=0,
        prepare=function(o,s) return {x=o.x,y=o.y,body=false},{} end,export=function(e) return {} end}
    World.request(world,{{x=8,y=-8}},0)
end,update=function(dt)
    local changed,err,event=World.update(world,dt);assert(not err,err)
    if sc.tick()==0 then
        assert(World.status(world).phase=='read')
        '''+('World.cancel(world)' if cancel else '')+'''
    else
        assert(sc.tick()==1 and sc.get(old).x==72 and sc.app.paused() and not World.status(world))
        '''+('''assert(event=='cancelled' and not changed and next(world.chunks)==nil and sc.stream.stats().pinned==0)''' if cancel else
            '''assert(changed and event=='published' and World.contains(world,{x=8,y=-8}))''')+'''
        sc.debug.watch('recovered',true)
    end
end,ui_update=function()
    local status=World.status(world)
    if status and status.status=='failed' then
        assert(status.phase=='read' and sc.tick()==1 and sc.get(old).x==72 and sc.app.paused())
        assert(next(world.chunks)==nil and #world.region.walls==0)
        '''+('World.ui_update(world)' if cancel else 'World.retry(world)')+'''
    end
end}''')
            invocation=[str(BINARY),str(self.root),'--headless','--frames','2','--save-dir',str(self.root/'saves')]
            if cancel:
                process=subprocess.run(invocation,capture_output=True,text=True,encoding='utf-8',timeout=15)
                self.assertEqual(process.returncode,0,process.stderr)
                self.assertTrue(json.loads(process.stdout)['watches']['recovered'])
                self.assertEqual(path.read_text(),'{broken')
                continue
            notices=queue.Queue();errors=[]
            process=subprocess.Popen(invocation,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def read_errors():
                for line in process.stderr:
                    errors.append(line)
                    if '"code":"save"' in line:notices.put(line)
            reader=threading.Thread(target=read_errors,daemon=True);reader.start()
            try:
                notices.get(timeout=10)
                fixed=path.with_suffix('.repaired')
                fixed.write_text(json.dumps(dict(format=3,project='stream-test',data_version=1,scene='main.lua',state={})),encoding='utf-8')
                for attempt in range(100):
                    try:
                        fixed.replace(path); break
                    except PermissionError:
                        if attempt==99: raise
                        time.sleep(.01)
                process.wait(timeout=10);reader.join(timeout=1)
                self.assertEqual(process.returncode,0,''.join(errors))
                self.assertTrue(json.loads(process.stdout.read())['watches']['recovered'])
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
                reader.join(timeout=1);process.stdout.close();process.stderr.close()

    def test_world_failed_save_recovery(self):
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua/shiny',self.root/'lib/shiny')
        for x in range(2):
            data=json.dumps({'x':x,'y':-1,'layers':{},'objects':[{'persistent_id':f'actor:{x}','layer':1,'x':x*256+8,'y':-8}]})
            (self.root/f'{x}.json').write_text(data,encoding='utf-8');self.index['chunks'][x]['bytes']=len(data.encode())
        self.write_index()
        for cancel in (False,True):
            slot='cancel' if cancel else 'retry'
            path=self.root/'saves/stream-test'/f'{slot}.json';path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(json.dumps(dict(format=3,project='stream-test',data_version=1,scene='main.lua',state={'old':True})))
            blocker=Path(str(path)+'.tmp');blocker.mkdir()
            self.scene('''local World=require('shiny.stream_world')
local world,old
return {gravity=0,init=function()
    world=World.new{index='index.json',name='forest',slot='''+repr(slot)+''',margin=0,
        prepare=function(o,s) return {x=s and s.x or o.x,y=o.y,body=false},{} end,
        export=function(e) return {x=e.x} end}
    World.request(world,{{x=8,y=-8}},0)
end,update=function(dt)
    local changed,err,event=World.update(world,dt);assert(not err,err)
    local tick=sc.tick()
    if tick==1 then
        old=world.owners['0:-1'].entries[1].id;sc.set(old,{x=72});World.request(world,{{x=264,y=-8}},sc.tick()+1)
    elseif tick==3 then '''+('World.cancel(world)' if cancel else '')+'''
    elseif tick==4 then
        '''+('''assert(event=='cancelled' and sc.get(old).x==72 and not World.status(world))
        assert(World.contains(world,{x=8,y=-8}) and not sc.app.paused())
        sc.debug.watch('cancelled',true);sc.app.quit()''' if cancel else '''assert(changed and not pcall(sc.get,old));World.request(world,{{x=8,y=-8}},sc.tick()+1)''')+'''
    elseif tick==7 then
        assert(changed and sc.get(world.owners['0:-1'].entries[1].id).x==72)
        sc.debug.watch('restored',true);sc.app.quit()
    end
end,ui_update=function()
    local status=World.status(world)
    if status and status.status=='failed' then
        assert(sc.tick()==4 and sc.get(old).x==72 and sc.app.paused())
        assert(World.contains(world,{x=8,y=-8}) and not World.contains(world,{x=264,y=-8}))
        '''+('World.ui_update(world)' if cancel else 'World.retry(world)')+'''
    end
end}''')
            invocation=[str(BINARY),str(self.root),'--headless','--frames','8','--save-dir',str(self.root/'saves')]
            if cancel:
                process=subprocess.run(invocation,capture_output=True,text=True,encoding='utf-8',timeout=15)
                self.assertEqual(process.returncode,0,process.stderr)
                self.assertTrue(json.loads(process.stdout)['watches']['cancelled'])
                self.assertEqual(json.loads(path.read_text())['state'],{'old':True})
                continue
            notices=queue.Queue();errors=[]
            process=subprocess.Popen(invocation,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            def read_errors():
                for line in process.stderr:
                    errors.append(line)
                    if '"code":"save"' in line:notices.put(line)
            reader=threading.Thread(target=read_errors,daemon=True);reader.start()
            try:
                notices.get(timeout=10);blocker.rmdir()
                process.wait(timeout=10);reader.join(timeout=1)
                self.assertEqual(process.returncode,0,''.join(errors))
                self.assertTrue(json.loads(process.stdout.read())['watches']['restored'])
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
                reader.join(timeout=1);process.stdout.close();process.stderr.close()

    def test_api_phases(self):
        api = json.loads(subprocess.check_output([str(BINARY), '--api'], encoding='utf-8'))
        entries = {entry['name']: entry['contract'] for entry in api['functions'] if entry['name'].startswith('sc.stream.')}
        self.assertEqual(entries['sc.stream.open']['phases'], ['load', 'init'])
        self.assertEqual(entries['sc.stream.request']['phases'], ['load', 'init', 'update'])
        self.assertEqual([p['name'] for p in entries['sc.stream.request']['parameters']], ['x', 'y', 'commit_frame'])
        self.assertTrue(all(entry['module']=='streaming' for entry in entries.values()))
        self.assertNotIn('draw', entries['sc.stream.get']['phases'])

    def test_built_objects(self):
        spec=importlib.util.spec_from_file_location('assets',Path(__file__).resolve().parents[1]/'tools/assets.py')
        assets=importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
        data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,'layers':[
            {'type':'objectgroup','name':'actors','offsetx':-256,'objects':[{'id':7,'x':8,'y':16}]}]}
        (self.root/'map.json').write_text(json.dumps(data),encoding='utf-8')
        (self.root/'assets.json').write_text(json.dumps({'maps':{'world':'map.json'}}),encoding='utf-8')
        built=assets.build(self.root/'assets.json',self.root/'built')
        index=(built/'map-world/index.json').relative_to(self.root).as_posix()
        self.scene('''return {init=function()
            sc.stream.open("'''+index+'''"); sc.stream.request(-1,0,0)
        end,update=function()
            local chunk=sc.stream.get(-1,0)
            assert(#chunk.objects==1 and next(chunk.layers)==nil)
            local object=chunk.objects[1]
            assert(object.persistent_id=="actors:7" and object.x==-248 and object.y==16 and object.layer==0)
        end}''')
        self.run_game('built-objects')

    def test_ui_host_callback(self):
        self.scene('''local updates,ui=0,0
return {init=function()
    sc.stream.open("index.json"); sc.stream.request(0,-1,0)
end,update=function()
    assert(ui==updates+1)
    updates=updates+1
    sc.state.set("ui_before_update",ui)
end,ui_update=function(dt)
    if dt==0 then assert(ui==updates); return end
    assert(dt>0 and dt<.02)
    assert(ui==updates)
    assert(not pcall(sc.state.set,"ui_wrote_game_state",true))
    assert(not pcall(sc.stream.request,1,-1,sc.tick()+1))
    ui=ui+1
end,draw=function() assert(ui==updates) end}''')
        result=self.run_game('ui-host',replay=True)
        self.assertEqual(result['state']['ui_before_update'],8)
        self.assertNotIn('ui_wrote_game_state',result['state'])

    def test_ui_can_pause_and_exit(self):
        self.scene('''return {ui_update=function()
    sc.app.pause(true); assert(sc.app.paused())
    sc.app.pause(false); assert(not sc.app.paused())
    sc.app.quit()
end}''')
        result=self.run_game('ui-exit',replay=True)
        self.assertEqual(result['frames'],0)

    def write_index(self):
        (self.root/'index.json').write_text(json.dumps(self.index), encoding='utf-8')

    def scene(self, text):
        (self.root/'main.lua').write_text(text, encoding='utf-8')

    def run_game(self, name='run', native=False, ok=True, replay=False, save=False, frames=8):
        args = [str(BINARY), str(self.root), '--frames', str(frames), '--mute',
                '--trace', str(self.root/f'{name}.trace'),
                '--profile', str(self.root/f'{name}.profile')]
        if save: args += ['--save-dir',str(self.root/'saves')]
        args += ['--replay', str(self.root/'input.replay')] if replay else ['--record', str(self.root/f'{name}.input')]
        if not native:
            args.append('--headless')
        else:
            args += ['--capture', str(self.root/f'{name}.png')]
        result = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode == 0, ok, result.stderr)
        return json.loads(result.stdout) if ok else result.stderr

    def lifecycle_scene(self):
        self.scene('''local player
return {gravity=0,init=function()
    player=sc.spawn{x=10,y=10,vx=60,color="#40CCFF"}
    sc.stream.open("index.json")
    assert(not pcall(sc.stream.open,123))
    assert(not pcall(sc.stream.open,"index.json"..string.char(0).."ignored"))
    assert(not pcall(sc.stream.request,"0",-1,0))
    assert(not pcall(sc.stream.request,0,-1,"0"))
    assert(not pcall(sc.stream.request,0,-1))
    assert(not pcall(sc.stream.get,0,-1,0))
    assert(not pcall(sc.stream.stats,0))
    assert(sc.stream.request(0,-1,0)==1)
    assert(sc.stream.request(1,-1,3)==2)
    assert(sc.stream.request(2,-1,3)==3)
    assert(sc.stream.get(0,-1)==nil)
    assert(sc.stream.request(-4,-8,0)==0)
    assert(next(sc.stream.get(-4,-8).layers)==nil)
end,update=function()
    local tick=sc.tick()
    if tick<3 then assert(sc.stream.get(1,-1)==nil and sc.stream.get(2,-1)==nil) end
    if tick==1 then assert(not pcall(sc.stream.request,0,-1,1)) end
    if tick==3 then
        assert(sc.stream.get(1,-1).layers["0"][1]==2)
        assert(sc.stream.get(2,-1).layers["0"][1]==3)
        sc.state.set("committed_at",tick)
        sc.stream.release(0,-1)
        assert(sc.stream.request(0,-1,6)==4)
    end
    if tick>=3 and tick<6 then assert(sc.stream.get(0,-1)==nil) end
    if tick==6 then assert(sc.stream.get(0,-1).layers["0"][1]==1) end
    sc.debug.watch("stream",sc.stream.stats())
end}''')

    def test_scheduled_publication_and_recorded_frames(self):
        self.lifecycle_scene()
        first = self.run_game('first')
        second = self.run_game('second')
        self.assertEqual(first['state']['committed_at'], 3)
        self.assertEqual(first['entities'], second['entities'])
        self.assertEqual(first['state'], second['state'])
        self.assertEqual(first['watches'], second['watches'])
        self.assertEqual(first['watches']['stream']['last_sequence'], 4)
        inputs = [json.loads(line) for line in (self.root/'first.input').read_text().splitlines()]
        self.assertEqual([r['frame'] for r in inputs[1:]], list(range(8)))
        traces = [json.loads(line) for line in (self.root/'first.trace').read_text().splitlines()]
        self.assertEqual(len(traces), 8)
        self.assertEqual((self.root/'first.trace').read_bytes(), (self.root/'second.trace').read_bytes())

    def test_failures_identify_the_scheduled_request(self):
        (self.root/'0.json').unlink()
        self.scene('''return {init=function()
            sc.stream.open("index.json"); sc.stream.request(0,-1,0)
        end,update=function() error("must not simulate missing terrain") end}''')
        error = self.run_game(ok=False)
        self.assertIn('"code":"stream"', error)
        self.assertIn('request 1', error)
        self.assertNotIn('must not simulate', error)

    def test_invalid_indices_and_deadlines(self):
        self.index['chunks'][0]['bytes'] = 0
        self.write_index()
        self.scene('return {init=function() sc.stream.open("index.json") end}')
        self.assertIn('outside range', self.run_game(ok=False))

    def large_scene(self):
        entries = []
        for x in range(15):
            data = json.dumps({'x': x, 'y': -1, 'layers': {str(layer): [0]*1024 for layer in range(64)}}, separators=(',', ':'))
            (self.root/f'{x}.json').write_text(data, encoding='utf-8')
            entries.append({'x': x, 'y': -1, 'path': f'{x}.json', 'bytes': len(data.encode())})
        self.index['chunks'] = entries
        self.write_index()
        self.scene('''return {gravity=0,init=function()
            sc.spawn{x=10,y=10,vx=60,color="#40CCFF"}
            sc.stream.open("index.json")
        end,update=function()
            if sc.tick()==2 then for x=0,14 do sc.stream.request(x,-1,3) end end
            if sc.tick()==3 then
                assert(sc.stream.stats().loaded==15)
                sc.state.set("committed_at",sc.tick())
            end
            sc.debug.watch("stream",sc.stream.stats())
        end}''')

    def test_native_and_headless_fields(self):
        if not NATIVE:
            self.skipTest('enable --native with a graphics build')
        self.large_scene()
        headless = self.run_game('headless', replay=True)
        native = self.run_game('native', native=True, replay=True)
        for key in ('state', 'entities', 'hash', 'state_hash', 'watches'):
            self.assertEqual(native[key], headless[key], key)
        from PIL import Image
        with Image.open(self.root/'native.png') as picture:
            self.assertGreater(len(picture.getcolors(picture.width*picture.height)), 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--native', action='store_true')
    args = parser.parse_args()
    BINARY, NATIVE = args.binary.resolve(), args.native
    unittest.main(argv=['stream_integration.py'])
