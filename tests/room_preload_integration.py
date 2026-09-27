"""Room preloads: fixed boundaries, ownership and transactional failure recovery."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from images_integration import png

BINARY = None

ACTIVE = '''local marker
return {width=320,height=180,gravity=0,ambient=1,preload_images={"red"},
init=function()
    sc.state.set("room","old")
    marker=sc.spawn{x=16,y=16,w=16,h=16,sprite="red"}
    sc.state.set("old_handle",marker)
end,
update=function()
    assert(sc.state.get("room")=="old")
    assert(sc.get(marker).x==16)
    if sc.tick()==0 then sc.scene("next.lua") end
end,
ui_update=function()
    if sc.images.stats().pinned>1 then
        assert(sc.tick()==1 and sc.state.get("room")=="old")
        assert(sc.get(marker).x==16)
        sc.log("ACTIVE_UI_DURING_PRELOAD")
    end
end,
draw=function()
    if sc.images.stats().pinned>1 then sc.log("ACTIVE_DRAW_DURING_PRELOAD") end
    sc.image("red",0,0,320,180,{screen=true})
end}
'''

NEXT = '''return {width=320,height=180,gravity=0,ambient=1,preload_images={"green","blue"},
init=function()
    assert(sc.state.get("room")=="old")
    sc.state.set("room","new")
    local saved,reason=pcall(sc.save.write,"candidate")
    assert(not saved and reason:find("update",1,true))
    sc.spawn{x=16,y=16,w=16,h=16,sprite="green"}
end,
update=function()
    assert(sc.tick()==0 and sc.pressed("right"))
    assert(not pcall(sc.get,sc.state.get("old_handle")))
    assert(sc.images.stats().pinned==2)
    sc.debug.watch("ready",true)
end,
draw=function()
    sc.image("green",0,0,320,180,{screen=true})
    sc.image("blue",260,120,32,32,{screen=true})
end}
'''


def project(root, failure=None):
    root.mkdir(parents=True, exist_ok=True)
    for name, color in [('red',(230,75,82,255)),('green',(68,195,144,255)),('blue',(70,110,220,255))]:
        data=png(color)
        (root/f'{name}.png').write_bytes(data[:45] if failure=='decode' and name=='green' else data)
    (root/'project.lua').write_text('''return {id="room-preload",display={width=320,height=180},
modules={"streaming"},rooms={"next.lua"},resources={
red={type="image",path="red.png",stream=true},green={type="image",path="green.png",stream=true},
blue={type="image",path="blue.png",stream=true}}}''')
    (root/'main.lua').write_text(ACTIVE)
    source=NEXT.replace('sc.image("green",0,0,320,180,{screen=true})', 'error("FIRST_DRAW_REJECTED")') if failure=='draw' else NEXT
    (root/'next.lua').write_text(source)
    (root/'input.txt').write_text('0 0\n1 2\n')


def run(root, frames=2, native=False):
    command=[str(BINARY),str(root),'--frames',str(frames),'--replay',str(root/'input.txt'),
             '--trace',str(root/'trace.jsonl'),'--save-dir',str(root/'saves')]
    command+=['--mute','--capture-hidden','--capture',str(root/'final.png')] if native else ['--headless']
    result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=15)
    (root/'stderr.log').write_text(result.stderr,encoding='utf-8')
    if result.returncode==0:
        (root/'final.json').write_text(result.stdout,encoding='utf-8')
    return result


class RoomPreloads(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='shiny-room-preload-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        project(self.root)

    def test_switch_preserves_input_and_releases_old_references(self):
        result=run(self.root)
        self.assertEqual(result.returncode,0,result.stderr)
        final=json.loads(result.stdout)
        self.assertEqual((final['scene'],final['tick'],final['state']['room']),('next.lua',1,'new'))
        self.assertTrue(final['watches']['ready'])
        trace=[json.loads(line) for line in (self.root/'trace.jsonl').read_text().splitlines()]
        self.assertEqual([(f['frames'],f['tick'],f['scene']) for f in trace],[(1,0,'next.lua'),(2,1,'next.lua')])

    def test_frame_limit_finishes_pending_switch(self):
        result=run(self.root,1)
        self.assertEqual(result.returncode,0,result.stderr)
        final=json.loads(result.stdout)
        self.assertEqual((final['frames'],final['scene'],final['tick']),(1,'next.lua',0))
        self.assertEqual(len((self.root/'trace.jsonl').read_text().splitlines()),1)

    def test_failed_candidates_release_resources_and_exit_headlessly(self):
        for failure,expected in [('decode','PNG decode failed'),('draw','FIRST_DRAW_REJECTED')]:
            with self.subTest(failure=failure):
                root=self.root/failure
                project(root,failure)
                result=run(root)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(expected,result.stderr)
                self.assertNotIn('AddressSanitizer',result.stderr)
                self.assertNotIn('runtime error:',result.stderr)

    def test_bad_declarations_and_unprepared_screen_image(self):
        for declaration,expected in [('false','dense name array'),('{"green","green"}','distinct'),
                                     ('{"missing"}','declared resource'),('{1}','image name'),
                                     ('{}','not committed')]:
            with self.subTest(declaration=declaration):
                (self.root/'main.lua').write_text('return {preload_images='+declaration+
                    ',draw=function() sc.image("green",0,0,8,8,{screen=true}) end}')
                result=subprocess.run([str(BINARY),str(self.root),'--headless','--frames','1'],
                    capture_output=True,text=True,encoding='utf-8',timeout=10)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(expected,result.stderr)

    def test_check_all_prepares_declared_rooms(self):
        # Checking must not invoke gameplay or let its state assumptions obscure resource validation.
        (self.root/'next.lua').write_text('return {preload_images={"green"},draw=function() sc.image("green",0,0,8,8) end}')
        result=subprocess.run([str(BINARY),str(self.root),'--check-all'],capture_output=True,text=True,encoding='utf-8',timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)


def native(output):
    from PIL import Image
    output.mkdir(parents=True,exist_ok=False)
    evidence={'engine_sha256':hashlib.sha256(BINARY.read_bytes()).hexdigest(),'cases':[],'visual_review':'pending'}
    for case in ['success','decode','draw']:
        root=output/case
        project(root,None if case=='success' else case)
        result=run(root,native=True)
        assert result.returncode==0,result.stderr
        final=json.loads(result.stdout)
        expected=(68,195,144) if case=='success' else (230,75,82)
        with Image.open(root/'final.png') as image:
            assert image.convert('RGB').getpixel((160,40))==expected
        assert final['scene']==('next.lua' if case=='success' else 'main.lua'),final['scene']
        assert final['state']['room']==('new' if case=='success' else 'old')
        assert 'ACTIVE_DRAW_DURING_PRELOAD' in result.stderr
        if case!='decode': assert 'ACTIVE_UI_DURING_PRELOAD' in result.stderr
        if case=='draw': assert 'FIRST_DRAW_REJECTED' in result.stderr
        if case=='decode': assert 'green' in result.stderr
        assert not list((root/'saves').rglob('*.json')) if (root/'saves').exists() else True
        evidence['cases'].append({'case':case,'sample_rgb':expected,'scene':final['scene'],'frames':final['frames']})
    (output/'manifest.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--native-output',type=Path)
    args=parser.parse_args()
    BINARY=args.binary.resolve()
    if args.native_output: native(args.native_output.resolve())
    else: unittest.main(argv=['room_preload_integration'])
