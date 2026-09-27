"""Persistent object identity survives reconstruction without persisting runtime handles."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class Identities(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='shiny-identities-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def file(self, name, text):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def run_game(self, frames=0, *options, ok=True):
        result = subprocess.run([str(BINARY), '--headless', str(self.root), '--frames', str(frames), *map(str, options)],
                                capture_output=True, text=True, encoding='utf-8', timeout=20)
        self.assertEqual(result.returncode == 0, ok, result.stderr or result.stdout)
        return json.loads(result.stdout) if ok else result.stderr

    def test_lifecycle_references_and_atomic_patches(self):
        self.file('project.lua', 'return {id="identities",limits={entities=4,identities=2}}')
        self.file('main.lua', '''local id,ref
return {init=function()
    assert(sc.identity.resolve("map/chest").status=="absent")
    sc.identity.declare("map/chest")
    assert(sc.identity.resolve("map/chest").status=="unloaded")
    id=sc.spawn({persistent_id="map/chest",x=5})
    ref=sc.identity.reference(id)
    assert(ref.room=="main.lua" and ref.persistent_id=="map/chest" and ref.id==nil)
    assert(sc.identity.resolve(ref).id==id)
    assert(not pcall(sc.spawn,{persistent_id="map/chest"}))
    sc.set(id,{persistent_id="map/chest"})
    local other=sc.spawn({x=2})
    assert(not pcall(sc.set_many,{{id=other,patch={x=9}},{id=id,patch={persistent_id="renamed"}}}))
    assert(sc.get(other).x==2 and sc.identity.resolve(ref).id==id)
    assert(not pcall(sc.identity.reference,other))
    sc.identity.unload(id)
    assert(not pcall(sc.get,id) and sc.identity.resolve(ref).status=="unloaded")
    local fresh=sc.spawn({persistent_id="map/chest"})
    assert(fresh~=id and sc.identity.resolve(ref).id==fresh)
    sc.destroy(fresh)
    assert(sc.identity.resolve(ref).status=="deleted")
    sc.identity.declare("map/chest")
    assert(sc.identity.resolve(ref).status=="deleted")
    sc.identity.remove("map/door")
    assert(sc.identity.resolve("map/door").status=="deleted")
    assert(not pcall(sc.identity.declare,"third"))
    id=sc.spawn({persistent_id="map/chest"})
    sc.identity.remove("map/chest")
    assert(not pcall(sc.get,id) and sc.identity.resolve(ref).status=="deleted")
end,draw=function()
    assert(sc.identity.resolve(ref).status=="deleted")
    assert(not pcall(sc.identity.declare,"third"))
    assert(not pcall(sc.identity.remove,"map/chest"))
end}''')
        self.run_game()

    def test_atomic_spawn_batch(self):
        self.file('project.lua','return {limits={entities=4,identities=2}}')
        self.file('main.lua','''local ids
return {init=function()
    assert(#sc.spawn_many({})==0)
    for _,batch in ipairs({{{persistent_id="a"},{w=0}},
            {{persistent_id="a"},{persistent_id="a"}},
            {{persistent_id="a"},{persistent_id="b"},{persistent_id="c"}},
            {{persistent_id="a"},{body={type="dynamic",shape="polygon",vertices={}}}}}) do
        assert(not pcall(sc.spawn_many,batch))
        assert(sc.identity.resolve("a").status=="absent")
    end
    for _,batch in ipairs({{[2]={}},setmetatable({{}},{}),{{unknown=true}}}) do
        assert(not pcall(sc.spawn_many,batch))
    end
    ids=sc.spawn_many({{persistent_id="a",x=10},{persistent_id="b",x=20},{},{}})
    assert(#ids==4 and ids[1]%65536==0 and ids[4]%65536==3)
    assert(sc.get(ids[1]).x==10 and sc.get(ids[2]).x==20)
    assert(not pcall(sc.spawn_many,{{}}))
    sc.identity.unload(ids[1]); sc.destroy(ids[2])
    local fresh=sc.spawn_many({{persistent_id="a"},{persistent_id="b"}})
    assert(fresh[1]~=ids[1] and fresh[2]~=ids[2])
    assert(not pcall(sc.get,ids[1]))
    assert(sc.identity.resolve("a").id==fresh[1])
end,draw=function() assert(not pcall(sc.spawn_many,{})) end,
ui_update=function() assert(not pcall(sc.spawn_many,{})) end}''')
        self.run_game(1)

    def test_capacity_disabled_and_invalid_names(self):
        self.file('project.lua', 'return {limits={entities=2,identities=0}}')
        self.file('main.lua', '''return {init=function()
local id=sc.spawn({}); assert(id)
assert(not pcall(sc.spawn,{persistent_id="named"}))
assert(not pcall(sc.identity.declare,"named"))
assert(sc.identity.resolve("named").status=="absent")
end}''')
        self.run_game()
        self.file('project.lua', 'return {limits={entities=1,identities=2}}')
        self.file('main.lua', r'''return {init=function()
for _,name in ipairs({"/a","a/","a//b",".","a/../b","a b","a\\b", "a\0b",string.rep("x",128)}) do
    assert(not pcall(sc.spawn,{persistent_id=name}))
    assert(not pcall(sc.identity.resolve,name))
end
for _,ref in ipairs({{room="../bad.lua",persistent_id="a"},{room="main.lua",persistent_id="a",id=1},
                    {room="main",persistent_id="a"},setmetatable({room="main.lua",persistent_id="a"},{})}) do
    assert(not pcall(sc.identity.resolve,ref))
end
local id=sc.spawn({persistent_id="a"})
assert(not pcall(sc.spawn,{persistent_id="b"}))
assert(sc.identity.resolve("b").status=="absent")
sc.destroy(id)
assert(sc.spawn({persistent_id="b"}))
end}''')
        self.run_game()

    def test_cross_room_and_save_reconstruction(self):
        self.file('project.lua', 'return {id="identities",data_version=1,rooms={"main.lua","other.lua"}}')
        self.file('main.lua', '''local id
return {init=function()
    id=sc.spawn({persistent_id="gate"})
    local ref=sc.state.get("gate")
    if ref then assert(sc.identity.resolve(ref).id==id) else sc.state.set("gate",sc.identity.reference(id)) end
end,update=function()
    local phase=sc.state.get("phase") or 0
    if phase==0 then
        sc.state.set("phase",1); assert(sc.save.write("slot")); sc.scene("other.lua")
    else
        assert(sc.identity.resolve(sc.state.get("gate")).id==id)
        sc.debug.watch("restored",true); sc.app.quit()
    end
end}''')
        self.file('other.lua', '''return {init=function()
    local ref=sc.state.get("gate")
    assert(sc.identity.resolve(ref).status=="room_inactive")
    local id=sc.spawn({persistent_id="gate"})
    assert(sc.identity.resolve("gate").id==id)
end,update=function() assert(sc.save.load("slot")) end}''')
        result = self.run_game(8, '--save-dir', self.root/'data')
        self.assertTrue(result['watches']['restored'])
        reference = result['state']['gate']
        self.assertEqual(reference, {'room': 'main.lua', 'persistent_id': 'gate'})
        self.assertGreater(result['entities'][0]['id'], 2**32)
        # A fresh process restores a persisted reference by rebuilding named entities.
        self.file('main.lua', '''return {init=function()
    local id=sc.spawn({persistent_id="gate"})
    local saved=sc.save.read("slot")
    assert(sc.identity.resolve(saved.state.gate).id==id)
    sc.debug.watch("restored_from_disk",true)
end}''')
        result = self.run_game(0, '--save-dir', self.root/'data')
        self.assertTrue(result['watches']['restored_from_disk'])


if __name__ == '__main__':
    unittest.main()
