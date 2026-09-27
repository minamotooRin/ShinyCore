"""Bounded host async checkpoints, deterministic result publication and UI recovery."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import tempfile
import threading

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--native',action='store_true')
parser.add_argument('--output',type=Path)
args=parser.parse_args()
binary=args.binary.resolve()
output=args.output.resolve() if args.output else None
if args.native and output is None: parser.error('--native requires --output')
if output: output.mkdir(parents=True,exist_ok=False)

with tempfile.TemporaryDirectory(prefix='shiny-save-async-') as directory:
    root=Path(directory)
    (root/'project.lua').write_text('return {id="async",data_version=1}',encoding='utf-8')
    (root/'idle.jsonl').write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n',encoding='utf-8')
    def command(frames=4):
        return [str(binary),str(root),'--headless','--frames',str(frames),'--save-dir',str(root/'saves')]
    def run(source,frames=4,ok=True):
        (root/'main.lua').write_text(source,encoding='utf-8')
        result=subprocess.run(command(frames),capture_output=True,text=True,encoding='utf-8',timeout=15)
        assert (result.returncode==0)==ok,result.stderr
        return json.loads(result.stdout) if ok else result.stderr
    (root/'next.lua').write_text('''return {init=function()
        assert(sc.save.read('slot').state.revision==3)
        assert(not pcall(sc.save.write_async,'candidate'))
        sc.debug.watch('complete',true)
    end}''',encoding='utf-8')
    result=run('''local request
local function blocked(f,...) assert(not pcall(f,...)) end
return {init=function()
    blocked(sc.save.write_async,'slot');blocked(sc.save.write_chunks_async,'slot',{})
end,update=function()
    local tick=sc.tick()
    if tick==0 then
        sc.state.set('revision',0);assert(sc.save.write('slot'));assert(sc.save.read('slot'))
        sc.state.set('revision',1)
        local changes={['forest:0:0']={value=1}}
        request=assert(sc.save.write_chunks_async('slot',changes))
        changes['forest:0:0'].value=99;sc.state.set('revision',2)
        assert(sc.save.status(request).status=='pending')
        blocked(sc.save.read,'slot');blocked(sc.save.list);blocked(sc.save.load,'slot')
        blocked(sc.save.read_chunk,'slot','forest:0:0');blocked(sc.save.delete,'slot')
        blocked(sc.save.write,'slot');blocked(sc.save.write_async,'slot')
        blocked(sc.scene,'next.lua');blocked(sc.save.release,request);blocked(sc.save.retry,request)
        for _,id in ipairs({0,-1,1.5,'1',4503599627370496}) do blocked(sc.save.status,id) end
        blocked(sc.save.status,request,nil)
    elseif tick==1 then
        assert(sc.save.status(request).status=='complete')
        local status=sc.save.status(request);status.status='failed'
        assert(sc.save.status(request).status=='complete')
        blocked(sc.save.retry,request);blocked(sc.save.read,'slot')
        assert(sc.save.release(request));blocked(sc.save.status,request)
        assert(sc.save.read('slot').state.revision==1 and sc.state.get('revision')==2)
        assert(sc.save.read_chunk('slot','forest:0:0').value==1)
        request=assert(sc.save.write_chunks_async('slot',{['forest:0:0']=false}))
        sc.state.set('revision',3)
    elseif tick==2 then
        assert(sc.save.status(request).status=='complete');sc.save.release(request)
        assert(sc.save.read_chunk('slot','forest:0:0')==nil)
        request=assert(sc.save.write_async('slot'))
    elseif tick==3 then
        assert(sc.save.status(request).status=='complete');sc.save.release(request)
        sc.scene('next.lua')
        local id,err=sc.save.write_async('slot');assert(id==nil and err)
    end
end,draw=function()
    if request and sc.tick()==1 then assert(sc.save.status(request).status=='pending') end
    if request then blocked(sc.save.release,request);blocked(sc.save.retry,request) end
end}''')
    assert result['watches']['complete'] is True
    # The final accepted write is drained even when the frame limit prevents another update.
    run("return {update=function() sc.state.set('revision',4);assert(sc.save.write_async('slot')) end}",frames=1)
    slot=root/'saves/async/slot.json'
    assert json.loads(slot.read_text())['state']['revision']==4
    blocker=Path(str(slot)+'.tmp');blocker.mkdir()
    failure='''local request,recovered
return {update=function()
    if sc.tick()==0 then
        sc.state.set('revision',5);request=assert(sc.save.write_async('slot'))
    elseif sc.tick()==1 then
        assert(sc.save.status(request).status=='complete');sc.save.release(request);request=nil
        assert(sc.save.read('slot').state.revision==5);recovered=true
    end
    sc.debug.watch('save',{tick=sc.tick(),recovered=recovered==true})
end,ui_update=function()
    if request and sc.save.status(request).status=='failed' then sc.save.retry(request) end
end,draw=function()
    sc.text(recovered and 'ASYNC SAVE RECOVERED' or 'SAVING CHECKPOINT',12,20,16,'#E5EDFA',true)
    sc.text('Fixed updates: '..sc.tick(),12,48,12,'#80E0BC',true)
end}'''
    (root/'main.lua').write_text(failure,encoding='utf-8')
    invocation=command(3)+['--replay',str(root/'idle.jsonl'),'--trace',str(root/'trace.jsonl')]
    if args.native:
        invocation.remove('--headless')
        invocation+=['--mute','--capture-hidden','--capture',str(output/'recovered.png')]
    notices=queue.Queue();errors=[]
    process=subprocess.Popen(invocation,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    def read_errors():
        for line in process.stderr:
            errors.append(line)
            if '"code":"save"' in line: notices.put(line)
    reader=threading.Thread(target=read_errors,daemon=True);reader.start()
    try:
        try: notices.get(timeout=10)
        except queue.Empty: raise AssertionError(f'No save diagnostic; exit={process.poll()}: '+''.join(errors))
        assert json.loads(slot.read_text())['state']['revision']==4
        blocker.rmdir()
        process.wait(timeout=10);reader.join(timeout=1)
        assert process.returncode==0,''.join(errors)
        text=process.stdout.read();snapshot=json.loads(text)
        assert snapshot['frames']==3 and snapshot['watches']['save']==dict(tick=2,recovered=True)
        traces=(root/'trace.jsonl').read_text().splitlines()
        assert len(traces)==3
        if output:
            (output/'snapshot.json').write_text(text,encoding='utf-8')
            (output/'run.log').write_text(''.join(errors),encoding='utf-8')
            (output/'trace.jsonl').write_text('\n'.join(traces)+'\n',encoding='utf-8')
            (output/'manifest.json').write_text(json.dumps(dict(engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                command=invocation,checks='passed',visual_review='pending' if args.native else 'not_applicable'),indent=2)+'\n',encoding='utf-8')
    finally:
        if process.poll() is None:process.kill();process.wait(timeout=5)
        reader.join(timeout=1);process.stdout.close();process.stderr.close()
    blocker.mkdir()
    error=run("return {update=function() assert(sc.save.write_async('slot')) end}",frames=2,ok=False)
    assert '"code":"save"' in error
    # An explicit release handles failure and resumes the existing room without overwriting disk.
    result=run('''local request
return {update=function()
    if sc.tick()==0 then request=assert(sc.save.write_async('slot')) end
    sc.debug.watch('continued',sc.tick())
end,ui_update=function()
    if request and sc.save.status(request).status=='failed' then sc.save.release(request);request=nil end
end}''',frames=2)
    assert result['watches']['continued']==1
    assert json.loads(slot.read_text())['state']['revision']==5
print('Async saves: host boundary, immutable state/chunks, exclusions, shutdown, UI retry and failure release passed')
