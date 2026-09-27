"""Exercise atomic world records through the actual Lua/host/save boundary."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class ChunkSaves(unittest.TestCase):
    def test_api_contract(self):
        api = json.loads(subprocess.check_output([str(BINARY), '--api'], encoding='utf-8'))
        self.assertEqual(api['save'], {'format': 3, 'index_bytes': 4*1024*1024,
                                      'chunk_bytes': 256*1024, 'chunks': 16384, 'world_bytes': 1024**3})
        functions = {f['name']: f for f in api['functions']}
        read = functions['sc.save.read_chunk']['contract']
        write = functions['sc.save.write_chunks']['contract']
        self.assertEqual(read['phases'], ['load', 'init', 'update'])
        self.assertEqual(write['phases'], ['update'])
        self.assertEqual([p['name'] for p in write['parameters']], ['slot', 'changes'])
        self.assertEqual([r['type'] for r in write['returns']], ['boolean|nil', 'string|nil'])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='shiny-chunk-lua-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.file('project.lua', 'return {id="world",rooms={"main.lua","other.lua"}}')

    def file(self, name, text):
        (self.root/name).write_text(text, encoding='utf-8')

    def run_game(self, frames=1, disk=True):
        args = [str(BINARY), '--headless', str(self.root), '--frames', str(frames)]
        if disk:
            args += ['--save-dir', str(self.root/'saves')]
        result = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_saved_objects_reconstruct_across_rooms_and_processes(self):
        self.file('main.lua', '''local chest
return {init=function()
    chest=sc.spawn {persistent_id="chest.7",x=24}
    if sc.state.get("restoring") then
        local chunk=assert(sc.save.read_chunk("slot","main/forest:-1:0"))
        local object=chunk.objects["chest.7"]
        sc.set(chest,{x=object.x})
        for _,name in ipairs(chunk.deleted) do sc.identity.remove(name) end
        assert(sc.identity.resolve("door.2").status=="deleted")
        sc.debug.watch("restored",true)
    end
end,update=function()
    if sc.state.get("restoring") then sc.app.quit(); return end
    sc.state.set("restoring",true)
    assert(sc.save.write_chunks("slot",{["main/forest:-1:0"]={
        objects={["chest.7"]={x=77}},deleted={"door.2"}}}))
    sc.scene("other.lua")
end}''')
        self.file('other.lua', '''return {update=function()
    assert(sc.save.read_chunk("slot","main/forest:-1:0").objects["chest.7"].x==77)
    assert(sc.save.load("slot"))
end}''')
        result = self.run_game(8)
        self.assertTrue(result['watches']['restored'])
        self.assertEqual(result['entities'][0]['x'], 77)
        self.file('main.lua', '''return {init=function()
    local checkpoint=assert(sc.save.read("slot"))
    assert(checkpoint.state.restoring and checkpoint.chunk_count==1 and checkpoint.chunks==nil)
    local chunk=assert(sc.save.read_chunk("slot","main/forest:-1:0"))
    sc.debug.watch("x",chunk.objects["chest.7"].x)
end}''')
        self.assertEqual(self.run_game(0)['watches']['x'], 77)

    def test_chunk_update_delete_bounds_and_phases(self):
        self.file('main.lua', '''return {init=function()
    assert(not pcall(sc.save.write_chunks,"slot",{}))
end,update=function()
    sc.state.set("revision",1)
    assert(sc.save.write_chunks("slot",{a={value=1},b={value=2}}))
    assert(not sc.save.write_chunks("slot",{a={value=9},b=true}))
    assert(sc.save.read_chunk("slot","a").value==1)
    assert(not sc.save.write_chunks("slot",{["../escape"]={value=9}}))
    assert(not sc.save.write_chunks("slot",{a={text=string.rep("x",262144)}}))
    assert(sc.save.read_chunk("slot","a").value==1)
    assert(sc.save.write_chunks("slot",{a={value=3},b=false}))
    assert(sc.save.write("slot"))
    assert(sc.save.read_chunk("slot","a").value==3)
    local missing,error=sc.save.read_chunk("slot","b")
    assert(missing==nil and error==nil)
    assert(not sc.save.read_chunk("slot","../escape"))
    local slots=sc.save.list(); assert(#slots==1 and slots[1].valid)
    assert(sc.save.delete("slot"))
    assert(not sc.save.read_chunk("slot","a"))
end,draw=function()
    assert(not pcall(sc.save.write_chunks,"slot",{}))
    assert(not pcall(sc.save.read_chunk,"slot","a"))
end}''')
        self.run_game()
        self.assertFalse((self.root/'saves/world/slot.json').exists())
        self.assertFalse((self.root/'saves/world/slot.json.chunks').exists())

    def test_corruption_recovers_state_and_all_chunks_together(self):
        self.file('main.lua', '''return {update=function()
    local n=sc.tick()+1; sc.state.set("revision",n)
    assert(sc.save.write_chunks("slot",{a={revision=n},b={revision=n}}))
end}''')
        self.run_game(2)
        path = self.root/'saves/world/slot.json'
        manifest = json.loads(path.read_text(encoding='utf-8'))
        chunk = self.root/'saves/world/slot.json.chunks'/manifest['chunks']['a']['file']
        chunk.write_text('{"revision":9.0}', encoding='utf-8')
        self.file('main.lua', '''return {init=function()
    local saved=assert(sc.save.read("slot"))
    assert(saved.state.revision==1)
    assert(sc.save.read_chunk("slot","a").revision==1)
    assert(sc.save.read_chunk("slot","b").revision==1)
    sc.debug.watch("recovered",true)
end}''')
        self.assertTrue(self.run_game(0)['watches']['recovered'])

    def test_headless_requires_explicit_disk_for_chunks(self):
        self.file('main.lua', '''return {update=function()
    local ok,error=sc.save.write_chunks("slot",{a={x=1}})
    assert(ok==nil and error:find("save%-dir"))
    assert(sc.save.write("ordinary"))
    assert(sc.save.read("ordinary"))
end}''')
        self.run_game(disk=False)


if __name__ == '__main__':
    unittest.main()
