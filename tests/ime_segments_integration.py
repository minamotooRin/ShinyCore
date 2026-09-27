"""Bounded IME segment replay, Lua boundary and UI editing checks."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

BINARY=None
ROOT=Path(__file__).resolve().parents[1]
KINDS=['input','target_converted','converted','target_unconverted','error','fixed']
SEGMENTS=[dict(start=1,finish=2,kind='converted'),dict(start=2,finish=3,kind='input'),
          dict(start=3,finish=5,kind='target_unconverted'),dict(start=5,finish=8,kind='error'),
          dict(start=8,finish=9,kind='fixed')]


def event(frame=0,**values):
    return dict(frame=frame,keys=[],gamepad=dict(connected=False),**values)


SOURCE='''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local entry={id='entry',kind='input',value='BASE',w=300,h=60}
local ui=UI.new{id='root',children={entry}}
local previous
return {init=function()
    UI.layout(ui,384,216);ui.focus='entry';entry.editor=Edit.new('BASE');Edit.select_all(entry.editor)
end,ui_update=function(dt)
    UI.update(ui,dt,384,216)
end,update=function()
    local tick=sc.tick()
    if tick==0 then
        local text,preedit,cursor,first,last,segments,truncated=sc.input.text()
        assert(preedit=='Ae\\u{0301}中Z' and cursor==5 and first==3 and last==5)
        assert(#segments==5 and not truncated and segments[3].kind=='target_unconverted')
        segments[3].kind='changed';assert(sc.input.snapshot().composition_segments[3].kind=='target_unconverted')
        local p=entry.text_layout.positions
        assert(#p==5 and p[1].kind=='converted' and p[2].kind=='target_unconverted' and p[2].targeted)
        assert(p[3].kind=='error' and p[4].kind=='fixed')
        assert(entry.value=='BASE' and #entry.editor.undo==0)
        sc.debug.watch('input',sc.input.snapshot())
    elseif tick==1 then
        assert(entry.text_layout~=previous and entry.text_layout.positions[1].kind=='input')
    elseif tick==2 then
        assert(entry.composition_segments_truncated and #entry.composition_segments==0)
        assert(entry.text_layout~=previous and entry.text_layout.positions[2].kind==nil)
        assert(entry.text_layout.positions[2].targeted and entry.value=='BASE')
    elseif tick==3 then assert(entry.value=='Ae\\u{0301}中Z' and #entry.editor.undo==1)
    elseif tick==4 then assert(entry.value=='BASE');sc.debug.watch('restored',true) end
    previous=entry.text_layout
end,draw=function() UI.draw(ui) end}
'''


class Segments(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='shiny-ime-segments-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        shutil.copytree(ROOT/'lua/shiny',self.root/'lib/shiny')
        (self.root/'main.lua').write_text(SOURCE,encoding='utf-8')

    def run_game(self,events,frames):
        replay=self.root/'input.jsonl'
        (self.root/'trace.jsonl').unlink(missing_ok=True)
        replay.write_text('\n'.join(json.dumps(row,ensure_ascii=False) for row in [dict(version=3),*events])+'\n',encoding='utf-8')
        return subprocess.run([str(BINARY),str(self.root),'--headless','--frames',str(frames),'--replay',str(replay),'--trace',str(self.root/'trace.jsonl')],
            capture_output=True,text=True,encoding='utf-8',timeout=10)

    def test_segments_editing_cache_and_snapshot_roundtrip(self):
        first=event(composition='Ae\u0301中Z',composition_edit=dict(cursor=5,start=3,finish=5),composition_segments=SEGMENTS)
        second=copy.deepcopy(first);second['frame']=1;second['composition_segments'][0]['kind']='input'
        third=copy.deepcopy(first);third.update(frame=2,composition_segments=[],composition_segments_truncated=True)
        fourth=event(3,text='Ae\u0301中Z');fourth['keys']=['enter']
        fifth=event(4);fifth['keys']=['left_control','z']
        result=self.run_game([first,second,third,fourth,fifth],5)
        self.assertEqual(result.returncode,0,result.stderr)
        watches=json.loads(result.stdout)['watches'];self.assertTrue(watches['restored'])
        snapshot=json.loads((self.root/'trace.jsonl').read_text(encoding='utf-8').splitlines()[0])['input']
        self.assertEqual(snapshot['composition_segments'],SEGMENTS)
        snapshot['frame']=0
        replayed=self.run_game([snapshot],1)
        self.assertEqual(replayed.returncode,0,replayed.stderr)
        del snapshot['frame']
        self.assertEqual(json.loads(replayed.stdout)['input'],snapshot)

    def test_replay_rejects_malformed_segments(self):
        (self.root/'main.lua').write_text('return {}')
        bad=[]
        for patch in [dict(start=2),dict(finish=1),dict(finish=1.5),dict(kind='unknown'),dict(extra=True)]:
            segments=copy.deepcopy(SEGMENTS);segments[0].update(patch);bad.append(dict(composition_segments=segments))
        split=copy.deepcopy(SEGMENTS);split[2]['finish']=4;split[3]['start']=4
        bad.extend([dict(composition_segments=split),dict(composition_segments=SEGMENTS[:-1]),
                    dict(composition_segments={}),dict(composition_segments=SEGMENTS*26),
                    dict(composition_segments=SEGMENTS,composition_segments_truncated=True),
                    dict(composition_segments_truncated=1)])
        for values in bad:
            with self.subTest(values=values):
                result=self.run_game([event(composition='Ae\u0301中Z',**values)],1)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('input.jsonl:2:',result.stderr)
                self.assertNotIn('AddressSanitizer',result.stderr)
                self.assertNotIn('runtime error:',result.stderr)

    def test_diagnostic_hash_includes_conversion_state(self):
        (self.root/'main.lua').write_text('return {}')
        values=[dict(composition_segments=SEGMENTS),dict(composition_segments=[]),
                dict(composition_segments=[],composition_segments_truncated=True)]
        changed=copy.deepcopy(SEGMENTS);changed[0]['kind']='input'
        values.append(dict(composition_segments=changed))
        hashes=[]
        for value in values:
            result=self.run_game([event(composition='Ae\u0301中Z',**value)],1)
            self.assertEqual(result.returncode,0,result.stderr)
            hashes.append(json.loads(result.stdout)['hash'])
        self.assertEqual(len(set(hashes)),len(values))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path)
    args=parser.parse_args();BINARY=args.binary.resolve()
    unittest.main(argv=['ime_segments_integration'])
