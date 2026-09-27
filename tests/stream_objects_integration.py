"""Persistent streamed objects through real Lua, native handles and disk saves."""
import json
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv[1]).resolve()
sys.argv[1:]=[]
ROOT=Path(__file__).resolve().parents[1]

class Objects(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='shiny-stream-objects-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        shutil.copytree(ROOT/'lua/shiny',self.root/'lib/shiny')
        (self.root/'project.lua').write_text('return {id="objects",limits={entities=4,identities=8}}',encoding='utf-8')

    def run_game(self,source):
        (self.root/'main.lua').write_text('''local Objects=require("shiny.stream_objects")
local chunk={objects={{persistent_id="actors:1",x=10},{persistent_id="actors:2",x=20}}}
local function prepare(object,saved)
    return {x=saved and saved.x or object.x}, {opened=saved and saved.opened or false}
end
local function export(entity,data) return {x=entity.x,opened=data.opened} end
'''+source,encoding='utf-8')
        result=subprocess.run([str(BINARY),'--headless',str(self.root),'--frames','2',
            '--save-dir',str(self.root/'saves')],capture_output=True,text=True,encoding='utf-8',timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)
        return json.loads(result.stdout)

    def test_roundtrip_and_fresh_process(self):
        self.run_game('''local owner,old
return {init=function()
    owner=Objects.load(chunk,nil,prepare); old=owner.entries[1].id
    owner.entries[1].data.opened=true; sc.set(old,{x=73})
    sc.destroy(owner.entries[2].id)
end,update=function()
    if sc.tick()==0 then
        assert(Objects.unload(owner,"slot","forest:0:0",export))
        assert(not pcall(sc.get,old))
        assert(sc.identity.resolve("actors:1").status=="unloaded")
        local saved=assert(sc.save.read_chunk("slot","forest:0:0"))
        owner=Objects.load(chunk,saved,prepare)
        assert(owner.entries[1].id~=old and owner.entries[1].data.opened)
        assert(sc.get(owner.entries[1].id).x==73)
        assert(sc.identity.resolve("actors:2").status=="deleted")
    end
end}''')
        self.run_game('''return {init=function()
    local saved=assert(sc.save.read_chunk("slot","forest:0:0"))
    local owner=Objects.load(chunk,saved,prepare)
    assert(owner.entries[1].data.opened and sc.get(owner.entries[1].id).x==73)
    assert(owner.entries[2].id==nil and sc.identity.resolve("actors:2").status=="deleted")
end}''')

    def test_failed_save_preserves_active_objects(self):
        path=self.root/'saves/objects'
        path.mkdir(parents=True); (path/'slot.json').mkdir()
        self.run_game('''local owner
return {init=function() owner=Objects.load(chunk,nil,prepare) end,update=function()
    local ok,err=Objects.unload(owner,"slot","forest:0:0",export)
    assert(not ok and type(err)=="string" and not owner.unloaded)
    for _,entry in ipairs(owner.entries) do assert(sc.get(entry.id)) end
end}''')

    def test_preparation_and_export_failures(self):
        self.run_game('''return {init=function()
    assert(not pcall(Objects.load,chunk,nil,function(object)
        if object.persistent_id=="actors:2" then error("bad factory") end
        return {x=object.x}
    end))
    assert(sc.identity.resolve("actors:1").status=="absent")
    assert(not pcall(Objects.load,chunk,nil,function(object)
        return {w=object.persistent_id=="actors:2" and 0 or 8}
    end))
    assert(sc.identity.resolve("actors:1").status=="unloaded")
    local owner=Objects.load(chunk,nil,prepare)
    assert(not pcall(Objects.snapshot,owner,function() error("bad export") end))
    assert(not pcall(Objects.snapshot,owner,function() local t={}; t.loop=t; return t end))
    assert(not pcall(Objects.load,chunk,{format=2,objects={}},prepare))
    assert(not pcall(Objects.load,chunk,{format=1,objects={unknown={deleted=true}}},prepare))
    for _,entry in ipairs(owner.entries) do assert(sc.get(entry.id)) end
    local snap=Objects.snapshot(owner,export)
    snap.objects["actors:1"].data.opened=true
    assert(not owner.entries[1].data.opened)
end}''')

    def test_stream_release_and_revisit(self):
        api=json.loads(subprocess.check_output([str(BINARY),'--api'],encoding='utf-8'))
        if not api['modules']['streaming']: self.skipTest('streaming disabled')
        spec=importlib.util.spec_from_file_location('assets',ROOT/'tools/assets.py')
        assets=importlib.util.module_from_spec(spec); spec.loader.exec_module(assets)
        data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,'layers':[
            {'name':'actors','type':'objectgroup','objects':[{'id':1,'x':10,'y':0}]}]}
        (self.root/'map.json').write_text(json.dumps(data),encoding='utf-8')
        (self.root/'assets.json').write_text(json.dumps({'maps':{'world':'map.json'}}),encoding='utf-8')
        built=assets.build(self.root/'assets.json',self.root/'built')
        index=(built/'map-world/index.json').relative_to(self.root).as_posix()
        (self.root/'project.lua').write_text('return {id="objects",modules={"streaming"}}',encoding='utf-8')
        self.run_game('''local old
return {init=function()
    sc.stream.open("'''+index+'''"); sc.stream.request(0,0,0)
end,update=function()
    local chunk=sc.stream.get(0,0)
    if sc.tick()==0 then
        local owner=Objects.load(chunk,nil,prepare)
        old=owner.entries[1].id; sc.set(old,{x=81})
        assert(Objects.unload(owner,"slot","forest:0:0",export))
        sc.stream.release(0,0); assert(sc.stream.stats().pinned==0)
        assert(not pcall(sc.get,old))
        sc.stream.request(0,0,1)
    else
        local saved=assert(sc.save.read_chunk("slot","forest:0:0"))
        local owner=Objects.load(chunk,saved,prepare)
        assert(owner.entries[1].id~=old and sc.get(owner.entries[1].id).x==81)
    end
end}''')

    def test_batch_unload_commit_and_failure(self):
        self.run_game('''local a,b,items
return {init=function()
    a=Objects.load({objects={chunk.objects[1]}},nil,prepare)
    b=Objects.load({objects={chunk.objects[2]}},nil,prepare)
    items={{owner=a,key="forest:0:0"},{owner=b,key="forest:1:0"}}
end,update=function()
    if sc.tick()~=0 then return end
    assert(Objects.unload_many({},"slot",export))
    assert(not pcall(Objects.unload_many,items,"slot",function(entity,data,object)
        if object.persistent_id=="actors:2" then error("second export failed") end
        return export(entity,data)
    end))
    assert(not a.unloaded and not b.unloaded)
    assert(sc.get(a.entries[1].id) and sc.get(b.entries[1].id))
    assert(#sc.save.list()==0)
    local ok,err=Objects.unload_many(items,"slot",function() return {large=string.rep("x",260000)} end)
    assert(not ok and err and not a.unloaded and not b.unloaded)
    assert(#sc.save.list()==0)
    assert(Objects.unload_many(items,"slot",export))
    assert(a.unloaded and b.unloaded)
    assert(not pcall(sc.get,a.entries[1].id) and not pcall(sc.get,b.entries[1].id))
    local first=assert(sc.save.read_chunk("slot","forest:0:0"))
    local second=assert(sc.save.read_chunk("slot","forest:1:0"))
    assert(first.objects["actors:1"].data.x==10 and second.objects["actors:2"].data.x==20)
    assert(not pcall(Objects.unload_many,items,"slot",export))
end}''')

    def test_batch_load_is_atomic_across_chunks(self):
        self.run_game('''return {init=function()
    local items={{chunk={objects={chunk.objects[1]}}},{chunk={objects={chunk.objects[2]}}}}
    assert(not pcall(Objects.load_many,items,function(object)
        if object.persistent_id=="actors:2" then return {w=0} end
        return {x=object.x}
    end))
    assert(sc.identity.resolve("actors:1").status=="unloaded")
    assert(sc.identity.resolve("actors:2").status=="unloaded")
    assert(not pcall(Objects.load_many,{items[1],items[1]},prepare))
    assert(sc.identity.resolve("actors:1").status=="unloaded")
    local occupied=sc.spawn_many({{},{},{}})
    assert(not pcall(Objects.load_many,items,prepare))
    for _,id in ipairs(occupied) do assert(sc.get(id)) end
    assert(sc.identity.resolve("actors:1").status=="unloaded")
    sc.destroy(occupied[3])
    local owners=Objects.load_many(items,prepare)
    assert(#owners==2 and sc.get(owners[1].entries[1].id).x==10)
    assert(sc.get(owners[2].entries[1].id).x==20)
    assert(#Objects.load_many({},prepare)==0)
end}''')

    def test_batch_validation_precedes_export(self):
        self.run_game('''return {init=function()
    local a=Objects.load({objects={chunk.objects[1]}},nil,prepare)
    local b=Objects.load({objects={chunk.objects[2]}},nil,prepare)
    local called=false
    local function forbidden() called=true; error("export must not run") end
    for _,items in ipairs({
        {{owner=a,key="same"},{owner=b,key="same"}},
        {{owner=a,key="a"},{owner=a,key="b"}},
        {[2]={owner=a,key="a"}},
        setmetatable({{owner=a,key="a"}},{}),
        {{owner=a,key="a"},{owner=b,key=false}}
    }) do assert(not pcall(Objects.unload_many,items,"slot",forbidden)) end
    assert(not called and not a.unloaded and not b.unloaded)
    assert(sc.get(a.entries[1].id) and sc.get(b.entries[1].id))
end}''')

    def test_later_export_cannot_invalidate_earlier_owner(self):
        self.run_game('''local a,b
return {init=function()
    a=Objects.load({objects={chunk.objects[1]}},nil,prepare)
    b=Objects.load({objects={chunk.objects[2]}},nil,prepare)
end,update=function()
    if sc.tick()~=0 then return end
    local items={{owner=a,key="a"},{owner=b,key="b"}}
    assert(not pcall(Objects.unload_many,items,"slot",function(entity,data,object)
        if object.persistent_id=="actors:2" then sc.destroy(a.entries[1].id) end
        return export(entity,data)
    end))
    assert(#sc.save.list()==0 and not a.unloaded and not b.unloaded)
    assert(sc.get(b.entries[1].id))
end}''')

    def test_capacity_failure_does_not_partially_load(self):
        self.run_game('''return {init=function()
    local existing=sc.spawn_many({{tag="old"},{},{}})
    assert(not pcall(Objects.load,chunk,nil,prepare))
    assert(sc.identity.resolve("actors:1").status=="unloaded")
    for _,id in ipairs(existing) do assert(sc.get(id)) end
    sc.destroy(existing[3])
    local owner=Objects.load(chunk,nil,prepare)
    assert(#owner.entries==2 and sc.get(owner.entries[2].id))
end}''')

if __name__=='__main__': unittest.main()
