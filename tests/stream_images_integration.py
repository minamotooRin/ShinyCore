"""World/image publication, map edits and failure recovery; optional hidden capture."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import unittest
import images_integration as images

ROOT=Path(__file__).resolve().parents[1]
SCENE='''local World=require("shiny.stream_world")
local world,stage,old,failed= nil,0,nil,false
return {width=192,height=108,gravity=0,ambient=1,
init=function()
    world=World.new{index="index.json",name="map",slot="slot",margin=0,residency=true,
        images={["red.png"]="red",["green.png"]="green",["bad.png"]="bad"},retain_images={"eager"},
        prepare=function(object,saved)
            return {x=saved and saved.x or object.x,y=object.y,w=8,h=8,sprite="green",body=false},{}
        end,export=function(entity) return {x=entity.x} end}
    World.request(world,{{x=8,y=8}},0)
end,
update=function(dt)
    local changed,err,event=World.update(world,dt)
    assert(not err,err)
    if event=="cancelled" then
        assert(failed and stage==1 and world.chunks["0:0"] and not world.chunks["1:0"])
        assert(sc.get(old).x==32 and sc.images.stats().pinned==2)
        assert(sc.physics.ray(4,70,0,-68))
        stage=9
    elseif changed and stage==0 then
        old=world.owners["0:0"].entries[1].id
        sc.set(old,{x=32}); assert(sc.images.stats().pinned==2)
        World.request(world,{{x=264,y=8}},sc.tick()+1); stage=1
    elseif changed and stage==1 then
        assert(failed and not pcall(sc.get,old) and world.chunks["1:0"])
        assert(sc.save.read_chunk("slot","map:0:0").objects["actor:0"].data.x==32)
        World.request(world,{{x=8,y=8}},sc.tick()+1); stage=2
    elseif changed and stage==2 then
        local owner=world.owners["0:0"]
        assert(owner.entries[1].id~=old and sc.get(owner.entries[1].id).x==32)
        local items={}
        for y=0,7 do for x=0,11 do items[#items+1]={x=x,y=y,layer=0,gid=2} end end
        local count,status=World.patch(world,items)
        assert(count==96 and status=="pending" and world.chunks["0:0"].layers["0"][1]==1)
        assert(sc.physics.ray(4,70,0,-68)); stage=3
    elseif event=="patched" then
        assert(stage==3 and world.chunks["0:0"].layers["0"][1]==2)
        assert(not sc.physics.ray(4,70,0,-68) and sc.images.stats().pinned==1)
        World.request(world,{},sc.tick()+1); stage=4
    elseif changed and stage==4 then
        assert(next(world.chunks)==nil and sc.images.stats().pinned==0)
        assert(sc.identity.resolve("actor:0").status=="unloaded")
        stage=9
    end
    sc.debug.watch("world_images",{stage=stage,pinned=sc.images.stats().pinned,failed=failed})
end,
ui_update=function()
    World.ui_update(world)
    local status=World.status(world)
    if status and status.status=="failed" then
        assert(status.phase=="images" and status.error:find("bad",1,true))
        assert(world.chunks["0:0"] and not world.chunks["1:0"] and sc.get(old).x==32)
        failed=true
        RECOVER
    end
end,
draw=function()
    World.draw(world)
    sc.text(failed and "IMAGE FAILURE: WORLD RETAINED" or "STREAMED WORLD",4,80,8,"#FFFFFF",true)
end}
'''


class StreamImages(images.ImageProject):
    def setUp(self):
        super().setUp()
        shutil.copytree(ROOT/'lua/shiny',self.root/'lib/shiny')
        (self.root/'eager.png').write_bytes(images.png((180,180,180,255)))
        project=(self.root/'project.lua').read_text()
        (self.root/'project.lua').write_text(project.replace('resources={','resources={eager={type="image",path="eager.png"},'))
        chunks=[]
        for x,gid in [(0,1),(1,3)]:
            cells=[0]*1024
            for y in range(8):
                for column in range(12):cells[y*32+column]=gid
            data=json.dumps({'x':x,'y':0,'layers':{'0':cells},'objects':[
                {'persistent_id':f'actor:{x}','layer':1,'x':x*256+16,'y':32}]},separators=(',',':'))
            (self.root/f'{x}.json').write_text(data)
            chunks.append({'x':x,'y':0,'path':f'{x}.json','bytes':len(data)})
        tiles=[{'id':i,'image':name+'.png','imagewidth':8,'imageheight':8,
                'properties':[{'name':'collision','value':'solid' if i==0 else 'empty'}]}
               for i,name in enumerate(['red','green','bad'])]
        index={'format':3,'chunk_size':32,'tilewidth':8,'tileheight':8,
               'layers':[{'type':'tilelayer'},{'type':'objectgroup'}],
               'tilesets':[{'firstgid':1,'tilecount':3,'columns':0,'tilewidth':8,'tileheight':8,'tiles':tiles}],
               'chunks':chunks}
        (self.root/'index.json').write_text(json.dumps(index))

    def test_world_retry_edits_and_unload(self):
        result,_=self.run_game(SCENE.replace('RECOVER','sc.log("REPAIR_IMAGE"); World.retry(world)'),28,repair=True)
        self.assertEqual(result['watches']['world_images'],{'stage':9,'pinned':0,'failed':True})

    def test_world_cancel_retains_old_region(self):
        result,_=self.run_game(SCENE.replace('RECOVER','World.cancel(world)'),10)
        self.assertEqual(result['watches']['world_images'],{'stage':9,'pinned':2,'failed':True})

    def test_compound_child_sprite_residency(self):
        result,_=self.run_game('''local World=require("shiny.stream_world")
local world,stage= nil,0
return {init=function()
    world=World.new{index="index.json",name="map",slot="slot",margin=0,residency=true,
        images={["red.png"]="red",["green.png"]="green"},
        prepare=function(object,saved)
            return {entity={x=object.x,y=object.y,w=8,h=8,body=false},
                children={halo={x=8,w=4,h=4,sprite="green",body=false}}},{}
        end,export=function(entity) return {x=entity.x} end}
    World.request(world,{{x=8,y=8}},0)
end,update=function(dt)
    local changed,err=World.update(world,dt); assert(not err,err)
    if changed and stage==0 then
        local entry=world.owners["0:0"].entries[1]
        assert(#entry.ids==2 and sc.get(entry.children.halo.id).sprite~="")
        assert(sc.images.stats().pinned==2)
        World.request(world,{},sc.tick()+1); stage=1
    elseif changed and stage==1 then
        assert(sc.images.stats().pinned==0 and next(world.owners)==nil)
        stage=2
    end
    sc.debug.watch("compound_images",{stage=stage,pinned=sc.images.stats().pinned})
end}''',12)
        self.assertEqual(result['watches']['compound_images'],{'stage':2,'pinned':0})

    def test_animation_and_layer_dependencies(self):
        self.run_game('''local Tiles=require("shiny.stream_tiles")
return {init=function()
    local view=Tiles.new({format=3,chunk_size=32,tilewidth=8,tileheight=8,
        layers={{type="tilelayer"},{type="imagelayer",image="background",imagewidth=8,imageheight=8},
            {type="imagelayer",image="hidden",imagewidth=8,imageheight=8,visible=false}},
        tilesets={{firstgid=1,tilecount=3,columns=0,tilewidth=8,tileheight=8,tiles={
            {id=0,image="frame-a",imagewidth=8,imageheight=8,animation={{tileid=0,duration=100},{tileid=1,duration=100}}},
            {id=1,image="frame-b",imagewidth=8,imageheight=8},{id=2,image="unused",imagewidth=8,imageheight=8}}}}})
    local cells={} for i=1,1024 do cells[i]=i==1 and 1 or 0 end
    local names=Tiles.images(view,Tiles.prepare(view,{{x=-1,y=0,layers={["0"]=cells}}}))
    assert(table.concat(names,",")=="background,frame-a,frame-b")
end}''',1)

    def test_ready_images_do_not_publish_when_entities_fail(self):
        (self.root/'bad.png').write_bytes(images.png((90,110,180,255)))
        project=(self.root/'project.lua').read_text()
        (self.root/'project.lua').write_text(project.replace('id="image-test"','id="image-test",limits={entities=6}'))
        source=SCENE.replace('assert(not err,err)','if err then assert(World.status(world).phase=="publish") end')
        source=source.replace('status.phase=="images" and status.error:find("bad",1,true)','status.phase=="publish"')
        result,_=self.run_game(source.replace('RECOVER','World.cancel(world)'),12)
        self.assertEqual(result['watches']['world_images'],{'stage':9,'pinned':2,'failed':True})

    def test_patch_image_failure_can_cancel_without_region_request(self):
        source=SCENE.replace('World.request(world,{{x=264,y=8}},sc.tick()+1); stage=1',
            'assert(select(2,World.patch(world,{{x=0,y=0,layer=0,gid=3}}))=="pending"); stage=1')
        result,_=self.run_game(source.replace('RECOVER','World.cancel(world)'),8)
        self.assertEqual(result['watches']['world_images'],{'stage':9,'pinned':2,'failed':True})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path);parser.add_argument('--native-output',type=Path)
    args=parser.parse_args();images.BINARY=args.binary.resolve()
    if args.native_output:
        output=args.native_output.resolve();output.mkdir(parents=True,exist_ok=False)
        case=StreamImages();case.setUp()
        try:
            source=SCENE.replace('RECOVER','World.cancel(world)')
            native,log=case.run_game(source,10,native=output/'retained.png')
            # Cancellation occurs before outgoing save submission, so the directory is still fresh.
            headless,_=case.run_game(source,10)
            case.assertEqual(native['watches'],headless['watches'])
            case.assertEqual(native['watches']['world_images'],{'stage':9,'pinned':2,'failed':True})
            (output/'native.json').write_text(json.dumps(native,indent=2))
            (output/'native.log').write_text(log)
            digest=hashlib.sha256(images.BINARY.read_bytes()).hexdigest()
            (output/'manifest.json').write_text(json.dumps({'engine_sha256':digest,'frames':10,
                'capture':'retained.png','headless_watches_match':True,'visual_review':'pending'},indent=2))
        finally:case.doCleanups()
    else:
        suite=unittest.defaultTestLoader.loadTestsFromTestCase(StreamImages)
        result=unittest.TextTestRunner(verbosity=2).run(suite)
        raise SystemExit(not result.wasSuccessful())


if __name__=='__main__':main()
