"""Repair a real missing chunk after its diagnostic; retry preserves the fixed boundary."""
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
args=parser.parse_args();binary=args.binary.resolve()
if args.native and not args.output: parser.error('--native requires --output for visual review')
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
contracts={f['name']:f.get('contract') for f in api['functions']}
assert contracts['sc.stream.failure']['phases']==['load','init','update','draw','ui_update']
assert contracts['sc.stream.retry']['phases']==['load','init','update','ui_update']
assert {f['name'] for f in api['types']['ScStreamFailure']['fields']}=={'sequence','frame','x','y','message'}
output=args.output.resolve() if args.output else None
if output: output.mkdir(parents=True,exist_ok=False)
with tempfile.TemporaryDirectory(prefix='shiny-stream-recovery-') as temporary:
    project=output/'project' if output else Path(temporary)/'project';project.mkdir()
    (project/'project.lua').write_text('return {id="recovery",modules={"streaming"}}',encoding='utf-8')
    chunks=[]
    for x in range(3):
        data=json.dumps(dict(x=x,y=0,layers={},objects=[]))
        name=f'{x}.json'
        (project/(name if x!=1 else 'repair.json')).write_text(data,encoding='utf-8')
        chunks.append(dict(x=x,y=0,path=name,bytes=len(data.encode())))
    (project/'index.json').write_text(json.dumps(dict(format=3,tilewidth=8,tileheight=8,chunk_size=32,layers=[],chunks=chunks)),encoding='utf-8')
    (project/'idle.jsonl').write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n',encoding='utf-8')
    source='''local updates,retries,marker=0,0,nil
return {width=384,height=216,init=function()
    sc.stream.open('index.json');sc.stream.request(0,0,0)
    marker=sc.spawn{x=12,y=24,w=8,h=8}
    assert(sc.stream.failure()==nil and not pcall(sc.stream.retry,1))
end,update=function()
    if sc.tick()==0 then
        sc.stream.request(1,0,1);sc.stream.request(2,0,1)
    else
        assert(retries>0 and sc.stream.failure()==nil)
        assert(sc.stream.get(0,0) and sc.stream.get(1,0) and sc.stream.get(2,0))
        assert(sc.stream.stats().last_sequence==3 and sc.stream.stats().loaded==3)
    end
    updates=updates+1
    sc.debug.watch('recovery',{updates=updates,recovered=retries>0,tick=sc.tick()})
end,ui_update=function()
    local failure=sc.stream.failure()
    if not failure then return end
    assert(sc.tick()==1 and updates==1 and sc.get(marker).x==12)
    assert(failure.sequence==2 and failure.frame==1 and failure.x==1 and failure.y==0)
    assert(type(failure.message)=='string')
    failure.x=99;assert(sc.stream.failure().x==1)
    assert(sc.stream.stats().loaded==1 and sc.stream.stats().queued==2)
    if retries==0 then
        for _,bad in ipairs({0,-1,1,3,2.5,'2',false,2^52}) do assert(not pcall(sc.stream.retry,bad)) end
        assert(not pcall(sc.stream.failure,nil) and not pcall(sc.stream.retry,2,nil))
    end
    retries=retries+1;assert(retries<1000,'external repair did not arrive')
    assert(sc.stream.retry(2) and sc.stream.failure()==nil)
    assert(not pcall(sc.stream.retry,2))
end,draw=function()
    assert(not pcall(sc.stream.retry,2))
    sc.rect(0,0,384,216,'#111C2E',true)
    sc.text('MAP STREAM RECOVERY',22,28,18,'#E6EDF7',true)
    sc.text('Fixed updates: '..updates,22,78,14,'#FFCB77',true)
    sc.text('Active chunks: '..sc.stream.stats().loaded,22,110,14,'#8FDCC8',true)
    sc.text('Old world kept until the full batch was ready.',22,166,10,'#AABBD2',true)
end}
'''
    (project/'main.lua').write_text(source,encoding='utf-8')
    command=[str(binary),str(project),'--frames','3','--mute','--replay',str(project/'idle.jsonl'),
             '--save-dir',str(Path(temporary)/'saves')]
    command+=['--capture-hidden','--capture',str(output/'recovered.png')] if args.native else ['--headless']
    process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    notices=queue.Queue();errors=[]
    def read_errors():
        for line in process.stderr:
            errors.append(line)
            if 'chunk request 2:' in line: notices.put(line)
    reader=threading.Thread(target=read_errors,daemon=True);reader.start()
    try:
        notices.get(timeout=10)
        (project/'repair.json').replace(project/'1.json')
        process.wait(timeout=10)
        text=process.stdout.read();reader.join(timeout=1)
        assert process.returncode==0,''.join(errors)
        result=json.loads(text)
        assert result['watches']['recovery']==dict(updates=3,recovered=True,tick=2)
        assert result['frames']==3
        if output:
            (output/'run.log').write_text(''.join(errors),encoding='utf-8')
            (output/'snapshot.json').write_text(text,encoding='utf-8')
            record=dict(engine_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),command=command,
                        checks='passed',visual_review='pending' if args.native else 'not_applicable')
            (output/'manifest.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    finally:
        if process.poll() is None: process.kill();process.wait(timeout=5)
        reader.join(timeout=1);process.stdout.close();process.stderr.close()
    # Declaring UI alone must not turn an unhandled headless failure into a hang.
    broken=Path(temporary)/'unhandled';broken.mkdir()
    (broken/'project.lua').write_text('return {modules={"streaming"}}',encoding='utf-8')
    (broken/'index.json').write_bytes((project/'index.json').read_bytes())
    (broken/'main.lua').write_text('''return {init=function()
        sc.stream.open('index.json');sc.stream.request(1,0,0)
    end,ui_update=function() sc.stream.failure() end}''',encoding='utf-8')
    failed=subprocess.run([str(binary),str(broken),'--headless','--frames','1'],capture_output=True,text=True,encoding='utf-8',timeout=10)
    assert failed.returncode!=0 and 'chunk request 1:' in failed.stderr,failed.stderr
print('Stream recovery: repaired file, retained world, original request/batch and fixed input boundary passed')
