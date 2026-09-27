"""Async chunk reads: fixed-boundary publication, pinned snapshots, failure recovery."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--native-output',type=Path)
args=parser.parse_args();binary=args.binary.resolve()
with tempfile.TemporaryDirectory(prefix='shiny-save-read-') as directory:
    root=Path(directory)
    (root/'project.lua').write_text('return {id="reads",data_version=1}',encoding='utf-8')
    def run(source,frames=5,success=True,disk=True):
        (root/'main.lua').write_text(source,encoding='utf-8')
        command=[str(binary),str(root),'--headless','--frames',str(frames)]
        if disk:command+=['--save-dir',str(root/'saves')]
        result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=15)
        assert (result.returncode==0)==success,result.stderr
        return json.loads(result.stdout) if success else result.stderr
    result=run('''local request
local function blocked(f,...) assert(not pcall(f,...)) end
return {init=function()
    blocked(sc.save.read_chunks_async,'slot',{})
end,update=function()
    local tick=sc.tick()
    if tick==0 then
        sc.state.set('revision',1)
        assert(sc.save.write_chunks('slot',{a={value=10},b={value=20}}))
        for _,keys in ipairs({{a=true},{1},{'a','a'},{'../bad'},{'a'..string.char(0)}}) do
            local id,err=sc.save.read_chunks_async('slot',keys);assert(id==nil and err)
        end
        blocked(sc.save.read_chunks_async,'slot')
        blocked(sc.save.read_chunks_async,'../bad',{})
        local keys={'a','missing'}
        request=assert(sc.save.read_chunks_async('slot',keys));keys[1]='b'
        assert(sc.save.status(request).status=='pending' and sc.save.status(request).operation=='read')
        blocked(sc.save.result,request);blocked(sc.save.release,request);blocked(sc.save.retry,request)
        blocked(sc.save.write_async,'slot');blocked(sc.save.read,'slot');blocked(sc.scene,'other.lua')
        sc.state.set('revision',99)
    elseif tick==1 then
        assert(sc.save.status(request).status=='complete')
        local value=sc.save.result(request)
        assert(value.record.state.revision==1 and value.record.chunk_count==2 and not value.record.chunks)
        assert(value.chunks.a.value==10 and not value.chunks.missing and not value.chunks.b)
        value.chunks.a.value=88;value.record.state.revision=88
        assert(sc.save.result(request).chunks.a.value==10 and sc.state.get('revision')==99)
        sc.save.release(request);blocked(sc.save.result,request)
        request=assert(sc.save.read_chunks_async('slot',{'b'}))
    elseif tick==2 then
        assert(sc.save.result(request).chunks.b.value==20);sc.save.release(request)
        request=assert(sc.save.read_chunks_async('missing',{'a'}))
    elseif tick==3 then
        local absent=sc.save.result(request);assert(not absent.record and next(absent.chunks)==nil)
        sc.save.release(request);request=assert(sc.save.read_chunks_async('slot',{}))
    elseif tick==4 then
        local value=sc.save.result(request);assert(value.record.state.revision==1 and next(value.chunks)==nil)
        sc.save.release(request);request=assert(sc.save.write_async('slot'))
    elseif tick==5 then
        assert(sc.save.status(request).operation=='write');blocked(sc.save.result,request)
        sc.save.release(request);sc.debug.watch('read_complete',true)
    end
end,draw=function() blocked(sc.save.read_chunks_async,'slot',{}) end,
ui_update=function() blocked(sc.save.read_chunks_async,'slot',{}) end}''',frames=6)
    assert result['watches']['read_complete']
    saves=root/'saves/reads'
    record=dict(format=3,project='reads',data_version=1,scene='main.lua',state={'old':True})
    (saves/'backup.json').write_text('{broken',encoding='utf-8')
    (saves/'backup.json.bak').write_text(json.dumps(record),encoding='utf-8')
    result=run('''local request
return {update=function()
    if sc.tick()==0 then request=assert(sc.save.read_chunks_async('backup',{}))
    else assert(sc.save.result(request).record.state.old);sc.save.release(request);sc.app.quit() end
end}''',frames=2)
    (saves/'broken.json').write_text('{broken',encoding='utf-8')
    failure='''local request,handled=false,false
return {width=320,height=180,update=function()
    if sc.tick()==0 then request=assert(sc.save.read_chunks_async('broken',{}))
    else assert(handled and sc.state.get('retained')==42);sc.debug.watch('released',true) end
    sc.state.set('retained',42)
end,ui_update=function()
    if request and sc.save.status(request).status=='failed' then
        assert(sc.tick()==1 and sc.state.get('retained')==42)
        assert(not pcall(sc.save.result,request))
        sc.save.release(request);request=nil;handled=true
    end
end}'''
    assert run(failure,frames=2)['watches']['released']
    fatal="return {update=function() if sc.tick()==0 then assert(sc.save.read_chunks_async('broken',{})) end end}"
    assert '"code":"save"' in run(fatal,frames=2,success=False)
    run("return {update=function() local id,err=sc.save.read_chunks_async('slot',{});assert(id==nil and err:find('disk')) end}",frames=1,disk=False)
    if args.native_output:
        output=args.native_output.resolve();output.mkdir(parents=True,exist_ok=False)
        source='''local request
return {width=320,height=180,update=function()
    if sc.tick()==0 then request=assert(sc.save.read_chunks_async('broken',{})) end
end,ui_update=function()
    if request and sc.save.status(request).status=='failed' then sc.app.quit() end
end,draw=function()
    sc.rect(32,32,256,100,'#18283EFF',true)
    sc.text('CURRENT ROOM RETAINED',42,42,10,'#E6EDF7FF',true)
    sc.text('Recovery controls remain visible',42,76,10,'#E6EDF7FF',true)
end}'''
        (root/'main.lua').write_text(source,encoding='utf-8')
        replay=root/'idle.jsonl';replay.write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n')
        command=[str(binary),str(root),'--frames','2','--capture-hidden','--mute','--replay',str(replay),
                 '--save-dir',str(root/'saves'),'--capture',str(output/'read-error.png')]
        captured=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=20)
        assert captured.returncode!=0 and '"code":"save"' in captured.stderr,captured.stderr
        assert (output/'read-error.png').is_file()
        (output/'read-error.log').write_text(captured.stderr,encoding='utf-8')
        (output/'manifest.json').write_text(json.dumps(dict(command=command,
            engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
            scene=source,visual_review='pending'),indent=2)+'\n',encoding='utf-8')
print('Async save reads: publication, detached results, exclusion, backup and failure release passed')
