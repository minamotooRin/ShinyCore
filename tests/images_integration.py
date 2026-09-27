"""Staged images through the real host; native captures are explicit and hidden."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import unittest
import zlib

BINARY = None


def png(color,width=8,height=8):
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data)&0xffffffff)
    rows=(b'\0'+bytes(color)*width)*height
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            +chunk(b'IDAT', zlib.compress(rows))+chunk(b'IEND', b''))


DRAW = '''draw=function()
    sc.rect(0,0,384,216,"#152337",true)
    sc.text(failed and "IMAGE ERROR: OLD SET RETAINED" or "STAGED IMAGE RESOURCES",18,18,12,"#FFFFFF",true)
    if visible then
        sc.image("red",24,58,136,104,{screen=true})
        if visible=="both" then sc.image("green",184,58,136,104,{screen=true}) end
    end
    sc.text("Decode / upload / commit",24,182,10,"#FFFFFF",true)
end'''
FLOW = '''local request,visible,failed
return {width=384,height=216,gravity=0,ambient=1,
init=function() assert(not pcall(sc.images.prepare,{"red"})) end,
update=function()
    local tick=sc.tick()
    if tick==0 then
        request=sc.images.prepare({"red","green"})
        assert(sc.images.status(request).status=="pending")
        assert(not pcall(sc.images.commit,request))
        assert(not pcall(sc.images.prepare,{"red"}))
        assert(not pcall(sc.scene,"next.lua"))
    elseif tick==1 then
        assert(sc.images.status(request).status=="ready")
        assert(sc.images.commit(request)); assert(not pcall(sc.images.status,request))
        visible="both"
    elseif tick==2 then
        request=sc.images.prepare({"red"})
    elseif tick==3 then
        assert(sc.images.status(request).status=="ready")
        assert(sc.images.commit(request)); visible="red"
        assert(sc.images.stats().pinned==1)
    elseif tick==4 then request=sc.images.prepare({})
    elseif tick==5 then assert(sc.images.commit(request)); visible=nil; assert(sc.images.stats().pinned==0)
    end
    sc.debug.watch("images",{visible=visible or "none",tick=tick,pinned=sc.images.stats().pinned})
end,
'''+DRAW+'}'


def failure(retry=False):
    recovery = 'sc.log("REPAIR_IMAGE"); sc.images.retry(request)' if retry else 'sc.images.cancel(request); request=nil'
    return '''local request,visible,failed
return {width=384,height=216,gravity=0,ambient=1,
update=function()
    local tick=sc.tick()
    if tick==0 then request=sc.images.prepare({"red"})
    elseif tick==1 then sc.images.commit(request); visible="red"; request=nil
    elseif tick==2 then request=sc.images.prepare({"red","bad"})
    elseif tick==3 then
        assert(failed)
        if request then assert(sc.images.status(request).status=="ready"); sc.images.commit(request); request=nil end
        sc.debug.watch("recovered",true)
    end
end,
ui_update=function()
    if request and sc.images.status(request).status=="failed" then
        assert(sc.tick()==3); assert(sc.images.status(request).error:find("bad",1,true))
        failed=true; '''+recovery+'''
    end
end,
'''+DRAW+'}'


class ImageProject(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='shiny-images-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        (self.root/'red.png').write_bytes(png((230,75,82,255)))
        (self.root/'green.png').write_bytes(png((68,195,144,255)))
        (self.root/'bad.png').write_bytes(png((68,195,144,255))[:45])
        (self.root/'project.lua').write_text('''return {id="image-test",modules={"streaming"},resources={
red={type="image",path="red.png",stream=true},green={type="image",path="green.png",stream=true},
bad={type="image",path="bad.png",stream=true}}}''',encoding='utf-8')

    def run_game(self, source, frames=6, native=None, repair=False):
        (self.root/'main.lua').write_text(source,encoding='utf-8')
        idle=self.root/'idle.jsonl'; idle.write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n')
        command=[str(BINARY),str(self.root),'--frames',str(frames),'--replay',str(idle),'--save-dir',str(self.root/'saves')]
        command+=['--mute','--capture-hidden','--capture',str(native)] if native else ['--headless']
        if repair:
            process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
            errors=[]; output=[]
            def read_errors():
                for line in process.stderr:
                    errors.append(line)
                    if 'REPAIR_IMAGE' in line:
                        temporary=self.root/'repaired.tmp'; temporary.write_bytes(png((68,195,144,255)))
                        temporary.replace(self.root/'bad.png')
            reader=threading.Thread(target=read_errors); reader.start()
            stdout_reader=threading.Thread(target=lambda: output.append(process.stdout.read())); stdout_reader.start()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait(); raise
            finally:
                reader.join(timeout=5)
                stdout_reader.join(timeout=5)
                process.stdout.close(); process.stderr.close()
            result=subprocess.CompletedProcess(command,process.returncode,''.join(output),''.join(errors))
        else:
            result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        return json.loads(result.stdout),result.stderr

class Images(ImageProject):
    def test_commit_cancel_and_fixed_ticks(self):
        result,_=self.run_game(FLOW)
        self.assertEqual(result['watches']['images'],{'visible':'none','tick':5,'pinned':0})

    def test_failed_decode_keeps_active_set(self):
        result,error=self.run_game(failure(),4)
        self.assertTrue(result['watches']['recovered']); self.assertIn('bad',error)

    def test_retry_repaired_file_at_same_tick(self):
        result,error=self.run_game(failure(True),4,repair=True)
        self.assertTrue(result['watches']['recovered']); self.assertIn('REPAIR_IMAGE',error)

    def test_boundaries_and_room_lifetime(self):
        (self.root/'next.lua').write_text('''local request
return {update=function()
    if sc.tick()==0 then
        assert(sc.images.stats().pinned==0)
        request=sc.images.prepare({"red"})
        assert(not pcall(sc.images.status,sc.state.get("request")))
    elseif sc.tick()==1 then sc.images.commit(request); sc.debug.watch("next_room",true) end
end}''',encoding='utf-8')
        result,_=self.run_game('''local request
return {update=function()
    if sc.tick()==0 then
        for _,names in ipairs({{"red","red"},{1},{"missing"},setmetatable({"red"},{})}) do
            assert(not pcall(sc.images.prepare,names))
        end
        request=sc.images.prepare({"red"}); sc.state.set("request",request)
        assert(not pcall(sc.images.status,tostring(request)))
        assert(sc.images.cancel(request)); assert(not pcall(sc.images.cancel,request))
        request=sc.images.prepare({"red"})
    elseif sc.tick()==1 then sc.images.commit(request); sc.scene("next.lua") end
end}''',4)
        self.assertTrue(result['watches']['next_room'])

    def test_eager_images_and_declared_path_aliases(self):
        (self.root/'eager.png').write_bytes(png((180,180,180,255)))
        project=(self.root/'project.lua').read_text()
        (self.root/'project.lua').write_text(project.replace('resources={','resources={eager={type="image",path="eager.png"},alias={type="image",path="red.png",stream=true},'))
        self.run_game('''local request,visible
return {update=function()
    if sc.tick()==0 then request=sc.images.prepare({"red","red.png","eager"})
    elseif sc.tick()==1 then
        sc.images.commit(request); visible=true; assert(sc.images.stats().pinned==1)
    end
end,draw=function() if visible then sc.image("alias",0,0,8,8) end end}''',2)

    def test_streamed_normal_dependency(self):
        api=json.loads(subprocess.check_output([str(BINARY),'--api'],encoding='utf-8'))
        if not any(entry['name']=='sc.lighting.normal' for entry in api['functions']):
            self.skipTest('advanced render disabled')
        source='''local request,visible
return {width=384,height=216,gravity=0,
init=function() sc.lighting.normal("red","green") end,
update=function()
    if sc.tick()==0 then request=sc.images.prepare(NAMES)
    elseif sc.tick()==1 then sc.images.commit(request); visible=true end
end,
draw=function() if visible then sc.image("red",0,0,8,8) end end}'''
        source=source.replace('visible=true','visible=true; assert(sc.images.stats().pinned==2)')
        self.run_game(source.replace('NAMES','{"red"}'),2)

    def test_check_all_decodes_unused_streamed_images(self):
        (self.root/'main.lua').write_text('return {}')
        result=subprocess.run([str(BINARY),str(self.root),'--check-all'],capture_output=True,text=True,encoding='utf-8',timeout=15)
        self.assertNotEqual(result.returncode,0); self.assertIn('bad',result.stderr)

    def test_contracts_and_uncommitted_drawing(self):
        api=json.loads(subprocess.check_output([str(BINARY),'--api'],encoding='utf-8'))
        functions=[entry for entry in api['functions'] if entry['name'].startswith('sc.images.')]
        self.assertEqual(len(functions),7)
        self.assertTrue(all(entry['contract']['module']=='streaming' for entry in functions))
        self.assertEqual({field['name'] for field in api['types']['ScImageRequestStatus']['fields']},{'request','status','count','error'})
        for draw in ['sc.image("red",0,0,8,8)','sc.spawn({sprite="red"})']:
            source='return {draw=function() '+draw+' end}' if draw.startswith('sc.image') else 'return {init=function() '+draw+' end}'
            (self.root/'main.lua').write_text(source)
            result=subprocess.run([str(BINARY),str(self.root),'--headless','--frames','1'],capture_output=True,text=True,encoding='utf-8',timeout=15)
            self.assertNotEqual(result.returncode,0); self.assertIn('not committed',result.stderr)


def main():
    global BINARY
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path);parser.add_argument('--native-output',type=Path)
    args=parser.parse_args(); BINARY=args.binary.resolve()
    if args.native_output:
        output=args.native_output.resolve(); output.mkdir(parents=True,exist_ok=False)
        report=[]
        for name,source,frames in [('committed',FLOW,3),('failed-old-set',failure(),4)]:
            case=Images();case.setUp()
            try:
                result,log=case.run_game(source,frames,native=output/(name+'.png'))
                headless,_=case.run_game(source,frames)
                case.assertEqual(result['frames'],headless['frames'])
                case.assertEqual(result['watches'],headless['watches'])
                (output/(name+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                (output/(name+'.log')).write_text(log,encoding='utf-8')
                with BINARY.open('rb') as binary: digest=hashlib.file_digest(binary,'sha256').hexdigest()
                report.append({'case':name,'frames':frames,'engine_sha256':digest,'capture':name+'.png','headless_watches_match':True,'visual_review':'pending'})
            finally: case.doCleanups()
        (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    else:
        result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Images))
        raise SystemExit(not result.wasSuccessful())


if __name__=='__main__': main()
