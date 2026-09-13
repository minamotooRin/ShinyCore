"""Device API, deterministic replay and scene continuity through the actual host."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv[1]).resolve()
ROOT = Path(sys.argv[2]).resolve()
sys.argv[1:] = []

def event(frame=0, keys=None, connected=False, buttons=None, axes=None, **edges):
    return dict(frame=frame, keys=keys or [], gamepad=dict(
        connected=connected, buttons=buttons or [], axes=axes or {}), **edges)

class InputTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="shiny-input-")
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.source("return {}")

    def source(self, text, name="main.lua"):
        (self.path / name).write_text(text, encoding="utf-8")

    def invoke(self, *args, ok=True):
        result = subprocess.run([str(BINARY), "--headless", str(self.path), *map(str,args)],
                                capture_output=True, text=True, encoding="utf-8", timeout=20)
        self.assertEqual(result.returncode == 0, ok, result.stderr or result.stdout)
        return json.loads(result.stdout) if ok else result.stderr

    def replay(self, events):
        self.source("\n".join(json.dumps(x) for x in [{"version":2}, *events])+"\n", "input.jsonl")
        return self.path / "input.jsonl"

    def test_full_name_catalog_and_edges(self):
        meta = json.loads(subprocess.check_output([str(BINARY),"--api"], text=True, encoding="utf-8"))
        keys, buttons = meta["keys"], meta["gamepad_buttons"]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(len(buttons),17)
        self.assertTrue({"a","0","f12","escape","left_shift","kp_enter","kb_menu"} <= set(keys))
        for name in keys + buttons + meta["gamepad_axes"]:
            self.assertIn("'"+name+"'", (ROOT / "docs/api.lua").read_text(encoding="utf-8"))
        self.source("local keys={"+",".join(map(json.dumps,keys))+"}; local buttons={"+
                    ",".join(map(json.dumps,buttons))+"}"+'''
return {update=function()
  local t=sc.tick()
  for _,k in ipairs(keys) do
    assert(sc.key_down(k)==(t<2)); assert(sc.key_pressed(k)==(t==0)); assert(sc.key_released(k)==(t==2))
  end
  for _,b in ipairs(buttons) do
    assert(sc.gamepad_down(b)==(t<2)); assert(sc.gamepad_pressed(b)==(t==0)); assert(sc.gamepad_released(b)==(t==2))
  end
  assert(sc.gamepad_connected()==(t<2))
end}''')
        path=self.replay([event(keys=keys,connected=True,buttons=buttons),event(2)])
        self.invoke("--frames",4,"--replay",path)

    def test_axes_short_edges_and_determinism(self):
        self.source('''return {update=function()
 local t=sc.tick()
 if t==0 then
   assert(sc.gamepad_axis('left_x')==0)
   assert(math.abs(sc.gamepad_axis('right_y')+.5)<.000001)
   assert(sc.gamepad_axis('right_trigger')==1)
   assert(math.abs(sc.gamepad_axis('left_trigger',0)-.6)<.000001)
 end
 if t==1 then
   assert(sc.key_pressed('escape') and sc.key_released('escape') and not sc.key_down('escape'))
   assert(sc.gamepad_pressed('south') and sc.gamepad_released('south') and not sc.gamepad_down('south'))
   assert(sc.pressed('jump') and sc.released('jump'))
 end
 if t==2 then assert(not sc.key_pressed('escape') and not sc.gamepad_pressed('south')) end
 if t==3 then assert(not sc.gamepad_connected() and sc.gamepad_axis('right_trigger')==0) end
end}''')
        path=self.replay([event(connected=True,axes=dict(left_x=.2,right_y=-.6,left_trigger=.6,right_trigger=1)),
                          event(1,connected=True,key_pressed=["escape"],key_released=["escape"],
                                button_pressed=["south"],button_released=["south"]),event(3)])
        first=self.invoke("--frames",4,"--replay",path)
        self.assertEqual(first,self.invoke("--frames",4,"--replay",path))
        self.assertFalse(first["input"]["gamepad"]["connected"])

    def test_same_tick_release_repress(self):
        self.source('''return {update=function()
 if sc.tick()==1 then assert(sc.key_down('space') and sc.key_pressed('space') and sc.key_released('space')) end
end}''')
        path=self.replay([event(keys=["space"]),event(1,keys=["space"],key_pressed=["space"],key_released=["space"])])
        self.invoke("--frames",2,"--replay",path)

    def test_scene_inherits_held_without_press(self):
        self.source("return {update=function() assert(sc.key_pressed('a')); sc.scene('next.lua') end}")
        self.source('''return {init=function()
 assert(sc.key_down('a') and not sc.key_pressed('a'))
 assert(sc.gamepad_down('south') and not sc.gamepad_pressed('south'))
end,update=function()
 assert(sc.key_down('a') and not sc.key_pressed('a'))
 assert(sc.down('jump') and not sc.pressed('jump'))
end}''', "next.lua")
        path=self.replay([event(keys=["a"],connected=True,buttons=["south"])])
        result=self.invoke("--frames",3,"--replay",path)
        self.assertEqual(result["scene"],"next.lua")

    def test_legacy_actions_do_not_invent_devices(self):
        self.source('''return {update=function()
 assert(sc.down('jump') and not sc.key_down('space') and not sc.gamepad_connected())
 assert(sc.pressed('jump')==(sc.tick()==0))
end}''')
        self.source("0 16\n", "legacy.txt")
        self.invoke("--frames",3,"--replay",self.path/"legacy.txt")

    def test_invalid_lua_arguments(self):
        for expression in ["sc.key_down('A')","sc.key_down('a',true)","sc.gamepad_connected(1)","sc.gamepad_axis('left_x',0,1)","sc.key_pressed(1)","sc.key_released({})",
                           "sc.gamepad_down('a')","sc.gamepad_axis('bogus')",
                           "sc.gamepad_axis('left_x',1)","sc.gamepad_axis('left_x',-1)",
                           "sc.gamepad_axis('left_x','0.2')","sc.gamepad_axis('left_x',0/0)",
                           "sc.gamepad_axis('left_x',math.huge)","sc.key_down('a'..string.char(0)..'b')"]:
            with self.subTest(expression=expression):
                self.source("return {init=function() "+expression+" end}")
                self.invoke("--frames",0,ok=False)

    def test_malformed_device_replays(self):
        bad=[]
        for key,value in [("extra",1),("frame",.5),("frame",-1),("keys",["a","a"]),
                          ("keys",["bogus"]),("keys",[1]),("keys","a"),("key_pressed",["a","a"])]:
            sample=event(); sample[key]=value; bad.append(sample)
        for axes in [dict(left_x=1.1),dict(left_trigger=-.1),dict(left_y="0"),dict(foo=0)]:
            bad.append(event(connected=True,axes=axes))
        bad.extend([event(buttons=["south"]),event(axes=dict(left_x=.1)),event(button_pressed=["south"]),
                    event(connected=True,buttons=["south","south"]),event(connected="yes")])
        for sample in bad:
            with self.subTest(sample=sample):
                path=self.replay([sample]); error=self.invoke("--frames",1,"--replay",path,ok=False)
                self.assertIn("input.jsonl:2:",error)
        for raw in ['{"version":3}\n','{"version":2,"extra":0}\n',
                    '{"version":2}\n{"frame":0,"frame":1}\n',
                    '{"version":2}\n{"frame":0,"keys":[],"gamepad":{"connected":true,"axes":{"left_x":NaN}}}\n']:
            self.source(raw,"input.jsonl")
            self.invoke("--frames",1,"--replay",self.path/"input.jsonl",ok=False)
        path=self.replay([event(1),event(1)])
        self.assertIn("input.jsonl:3:",self.invoke("--frames",2,"--replay",path,ok=False))

    def test_showcase_and_debug_option(self):
        self.assertIn("--debug-keys",subprocess.check_output([str(BINARY),"--help"],text=True))
        result=subprocess.run([str(BINARY),"--headless",str(ROOT/"examples/input"),"--frames","125",
                               "--replay",str(ROOT/"examples/input/demo.jsonl")],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)

if __name__ == "__main__":
    unittest.main()
